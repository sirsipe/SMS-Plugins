#include "SamplerEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace midichopper {
namespace {
constexpr float kSilence = 0.0f;
constexpr auto kSampleBlockFrames = SampleStoragePool::kBlockFrames;
constexpr std::uint32_t clampPad(std::uint32_t p) noexcept { return p < kPadCount ? p : kPadCount; }
constexpr std::uint32_t poolBlocks(const std::uint32_t captureFrames) noexcept {
    // Leave room for the partially filled final blocks created by splitting a
    // pool-length recording across all pads in one bank.
    return std::max(kPadCount, kPadsPerBank *
        ((captureFrames + kSampleBlockFrames - 1U) / kSampleBlockFrames) +
        kPadsPerBank);
}
}

SamplerEngine::SamplerEngine(double sampleRate, double maxRecordSeconds)
    : sample_rate_(sampleRate > 1.0 ? sampleRate : 48000.0),
      max_record_seconds_(maxRecordSeconds > 0.0 ? maxRecordSeconds : 1.0),
      total_blocks_(poolBlocks(static_cast<std::uint32_t>(
          std::max(1.0, std::ceil(sample_rate_ * max_record_seconds_))))),
      max_frames_(total_blocks_ * kSampleBlockFrames),
      storagePool_(total_blocks_),
      ringCapacityFrames_(static_cast<std::uint32_t>(std::max(1.0, std::ceil(sample_rate_ * 0.1)))),
      ring_(static_cast<std::size_t>(ringCapacityFrames_) * 2U, 0.0f) {
    for (auto& p : pads_) p.sourceSampleRate = sample_rate_;
    setSettings(settings_);
}

void SamplerEngine::setSampleRate(double sampleRate) {
    const std::lock_guard lock(storageControlMutex_);
    if (sampleRate <= 1.0 || sampleRate == sample_rate_) return;

    struct RestoreDispatcher {
        SamplerEngine& engine;
        decltype(controlDispatch_) previous;
        ~RestoreDispatcher() { engine.controlDispatch_ = previous; }
    } restore{*this, controlDispatch_};
    controlDispatch_ = nullptr;
    applyPendingState();
    stopChopPreview();

    // A host may change rate while keeping the same plug-in instance. Preserve
    // completed pads and only grow the shared pool when the new rate needs
    // more room; shrinking it could truncate restored material.
    if (activePad_ >= 0) {
        finishRecord(static_cast<std::uint32_t>(activePad_));
        activePad_ = -1;
    }
    const auto requiredFrames = static_cast<std::uint32_t>(
        std::max(1.0, std::ceil(sampleRate * max_record_seconds_)));
    const auto requiredBlocks = poolBlocks(requiredFrames);
    if (requiredBlocks > total_blocks_) {
        std::array<PadData, kPadCount> snapshots;
        std::array<sms::dsp::SamplePlaybackSettings, kPadCount> playbackSettings;
        std::array<sms::dsp::SampleMixerSettings, kPadCount> mixerSettings;
        std::array<bool, kPadCount> occupied{};
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            occupied[pad] = pads_[pad].occupied.load(std::memory_order_acquire);
            if (occupied[pad]) {
                static_cast<void>(exportPad(pad, snapshots[pad]));
                playbackSettings[pad] = padPlaybackSettings(pad);
                mixerSettings[pad] = padMixerSettings(pad);
            }
        }

        total_blocks_ = requiredBlocks;
        max_frames_ = total_blocks_ * kSampleBlockFrames;
        for (auto& pad : pads_) {
            if (pad.storage)
                pad.storage->references.fetch_sub(1, std::memory_order_release);
            pad.storage = nullptr;
            pad.publishedStorage.store(nullptr, std::memory_order_release);
        }
        used_blocks_ = 0;
        collectImportedStorage();
        storagePool_.configure(total_blocks_);
        for (auto& pad : pads_) {
            pad.allocatedBlocks = 0;
            pad.recordedFrames = pad.recordPosition = 0;
            pad.publishedFrames.store(0, std::memory_order_release);
            pad.occupied.store(false, std::memory_order_release);
        }
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            if (occupied[pad]) {
                static_cast<void>(importPad(pad, snapshots[pad], false));
                setPadPlaybackSettingsDirect(pad, playbackSettings[pad]);
                setPadMixerSettingsDirect(pad, mixerSettings[pad]);
            }
        }
    }

    sample_rate_ = sampleRate;
    ringCapacityFrames_ = static_cast<std::uint32_t>(std::max(1.0, std::ceil(sample_rate_ * 0.1)));
    ring_.assign(static_cast<std::size_t>(ringCapacityFrames_) * 2U, 0.0f);
    ringWritePosition_ = ringCount_ = 0;
    activePad_ = -1;
    previousArmed_ = false;
    for (auto& p : pads_) {
        p.recording = p.playing = false;
        p.publishedRecording.store(false, std::memory_order_release);
        p.publishedPlaying.store(false, std::memory_order_release);
    }
    resetCaptureTarget(settings_.armed);
    setSettings(settings_);
}

void SamplerEngine::reset() noexcept {
    applyPendingState();
    stopChopPreview();
    chopMidiPreview_ = {};
    activePad_ = -1;
    lastCommittedPad_ = -1;
    previousArmed_ = settings_.armed;
    ringWritePosition_ = ringCount_ = 0;
    nextVoiceOrder_ = 1;
    lastPlaybackTrigger_ = {};
    playbackPositionGeneration_ = 0;
    playbackPositionHoldFrames_ = 0;
    playbackPositionOutput_ = 0.0f;
    playbackPositionWasActive_ = false;
    for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
        auto& p = pads_[pad];
        const PadMutation publication(p);
        releasePadBlocks(pad);
        p.recording = p.held = p.playing = false;
        p.voiceOrder = 0;
        p.envelope.reset();
        p.publishedPlaying.store(false, std::memory_order_release);
        p.recordedFrames = p.recordPosition = 0;
        p.publishedFrames.store(0, std::memory_order_release);
        p.occupied.store(false, std::memory_order_release);
        p.publishedRecording.store(false, std::memory_order_release);
        p.generation.fetch_add(1U, std::memory_order_release);
    }
    resetCaptureTarget(settings_.armed);
}

void SamplerEngine::setSettings(const EngineSettings& s) noexcept {
    const EngineSettings previous = settings_;
    settings_ = s;
    if (settings_.activeBank >= kBankCount) settings_.activeBank = 0;
    settings_.baseNote = effectiveBaseMidiNote(settings_.baseNote, settings_.midiBankMode);
    settings_.padsPerBank = settings_.padsPerBank <= 8U ? 8U :
                            (settings_.padsPerBank <= 12U ? 12U : 16U);
    if (settings_.startPad >= settings_.padsPerBank) settings_.startPad = 0;
    if (settings_.preRollMilliseconds < 0.0f) settings_.preRollMilliseconds = 0.0f;
    if (settings_.preRollMilliseconds > 100.0f) settings_.preRollMilliseconds = 100.0f;
    settings_.gain = std::max(std::isfinite(settings_.gain) ? settings_.gain : 1.0f, 0.0f);
    settings_.monitorGain = std::max(
        std::isfinite(settings_.monitorGain) ? settings_.monitorGain : 1.0f, 0.0f);
    settings_.pan = std::clamp(std::isfinite(settings_.pan) ? settings_.pan : 0.0f,
                               -1.0f, 1.0f);
    settings_.tuneSemitones = std::clamp(
        std::isfinite(settings_.tuneSemitones) ? settings_.tuneSemitones : 0.0f,
        -24.0f, 24.0f);
    const auto globalColor = sms::dsp::sanitize(sms::dsp::SampleMixerSettings{
        0.0f, 0.0f, 0.0f, settings_.lowpass, settings_.highpass,
        settings_.filterSlope, settings_.dirty});
    settings_.lowpass = globalColor.lowpass;
    settings_.highpass = globalColor.highpass;
    settings_.filterSlope = globalColor.filterSlope;
    settings_.dirty = globalColor.dirty;
    settings_.maxVoices = static_cast<std::uint8_t>(
        std::clamp<std::uint32_t>(settings_.maxVoices, 1U, kPadsPerBank));
    if (settings_.armed)
        stopChopPreview();
    enforceVoiceLimit();
    preRollFrames_ = static_cast<std::uint32_t>(std::clamp(
        std::llround(static_cast<double>(settings_.preRollMilliseconds) * sample_rate_ / 1000.0),
        0LL, static_cast<long long>(ringCapacityFrames_)));

    if (settings_.armed && activePad_ < 0) {
        const bool automaticTargetChanged = !previous.armed ||
            settings_.activeBank != previous.activeBank ||
            settings_.padsPerBank != previous.padsPerBank ||
            settings_.midiBankMode != previous.midiBankMode;
        if (automaticTargetChanged)
            resetCaptureTarget(true);
        else if (settings_.startPad != previous.startPad)
            resetCaptureTarget(false);
    }
}

std::uint32_t SamplerEngine::noteToPad(std::uint8_t note) const noexcept {
    return padForMidiNote(note, settings_.baseNote, settings_.activeBank,
                          settings_.padsPerBank, settings_.midiBankMode);
}

std::uint32_t SamplerEngine::firstCapturePad() const noexcept {
    return static_cast<std::uint32_t>(settings_.activeBank) *
               bankStride(settings_.padsPerBank, settings_.midiBankMode) +
           settings_.startPad;
}

std::uint32_t SamplerEngine::firstAvailableCapturePad() const noexcept {
    const std::uint32_t first = static_cast<std::uint32_t>(settings_.activeBank) *
        bankStride(settings_.padsPerBank, settings_.midiBankMode);
    for (std::uint32_t localPad = 0; localPad < settings_.padsPerBank; ++localPad) {
        const std::uint32_t pad = first + localPad;
        if (pad < kPadCount && !pads_[pad].occupied.load(std::memory_order_acquire))
            return pad;
    }
    return first;
}

void SamplerEngine::resetCaptureTarget(const bool preferEmpty) noexcept {
    capturePadsPerBank_ = settings_.padsPerBank;
    captureMidiBankMode_ = settings_.midiBankMode;
    nextPad_ = preferEmpty ? firstAvailableCapturePad() : firstCapturePad();
    sessionComplete_ = false;
}

std::uint32_t SamplerEngine::captureTargetPad() const noexcept {
    if (!settings_.armed)
        return kPadCount;
    if (activePad_ >= 0)
        return static_cast<std::uint32_t>(activePad_);
    return !sessionComplete_ && nextPad_ < kPadCount ? nextPad_ : kPadCount;
}

void SamplerEngine::selectCaptureTarget(const std::uint32_t pad) noexcept {
    if (!settings_.armed || activePad_ >= 0 || pad >= kPadCount ||
        bankForPad(pad, settings_.padsPerBank, settings_.midiBankMode) != settings_.activeBank)
        return;
    const std::uint32_t localPad =
        localPadInBank(pad, settings_.padsPerBank, settings_.midiBankMode);
    if (localPad >= settings_.padsPerBank)
        return;
    settings_.startPad = static_cast<std::uint8_t>(localPad);
    resetCaptureTarget(false);
}

std::uint32_t SamplerEngine::followingCapturePad(const std::uint32_t pad) const noexcept {
    if (pad >= kPadCount) return kPadCount;
    if (captureMidiBankMode_ == MidiBankMode::AllBanks) {
        const std::uint32_t addressablePads =
            static_cast<std::uint32_t>(capturePadsPerBank_) * kBankCount;
        return pad + 1U < addressablePads ? pad + 1U : kPadCount;
    }
    const std::uint32_t bank = pad / kPadsPerBank;
    const std::uint32_t localPad = pad % kPadsPerBank;
    if (localPad + 1U < capturePadsPerBank_)
        return pad + 1U;
    return bank + 1U < kBankCount ? (bank + 1U) * kPadsPerBank : kPadCount;
}

void SamplerEngine::collectImportedStorage() {
    collectPendingState();
    storagePool_.collectImported();
}

void SamplerEngine::collectPendingState() {
    std::erase_if(pendingStateOwners_, [](const auto& state) {
        return state->consumed.load(std::memory_order_acquire);
    });
}

std::unique_ptr<SamplerEngine::PendingState> SamplerEngine::preparePendingState() {
    return std::make_unique<PendingState>();
}

void SamplerEngine::enqueuePendingState(const std::uint32_t pad,
    std::unique_ptr<PendingState> state) {
    auto* update = state.get();
    const auto audio = update->replaceAudio;
    const auto playback = update->replacePlayback;
    const auto mixer = update->replaceMixer;
    Storage* const storage = update->storage;
    const auto playbackSettings = update->playback;
    const auto mixerSettings = update->mixer;
    pendingStateOwners_.push_back(std::move(state));
    for (;;) {
        auto* previous = pendingState_[pad].load(std::memory_order_acquire);
        update->replaceAudio = audio || (previous && previous->replaceAudio);
        update->replacePlayback = playback || (previous && previous->replacePlayback);
        update->replaceMixer = mixer || (previous && previous->replaceMixer);
        update->storage = audio ? storage : previous ? previous->storage : nullptr;
        update->playback = playback ? playbackSettings : previous ? previous->playback : sms::dsp::SamplePlaybackSettings{};
        update->mixer = mixer ? mixerSettings : previous ? previous->mixer : sms::dsp::SampleMixerSettings{};
        if (update->storage) update->storage->references.fetch_add(1U, std::memory_order_relaxed);
        if (pendingState_[pad].compare_exchange_weak(previous, update, std::memory_order_acq_rel)) {
            if (previous) {
                if (previous->storage) previous->storage->references.fetch_sub(1U, std::memory_order_release);
                previous->consumed.store(true, std::memory_order_release);
            }
            return;
        }
        if (update->storage) update->storage->references.fetch_sub(1U, std::memory_order_release);
        // The callback consumed the old update. Rebuild from the current mailbox,
        // rather than replaying old PCM alongside a later settings-only change.
    }
}

bool SamplerEngine::restorePad(const std::uint32_t pad, const PadData* audio) {
    if (pad >= kPadCount) return false;
    const std::lock_guard lock(storageControlMutex_);
    collectImportedStorage();
    auto state = preparePendingState();
    state->storage = audio ? prepareStorage(*audio) : nullptr;
    if (audio && !state->storage) return false;
    state->replaceAudio = true;
    if (!audio) {
        state->replacePlayback = state->replaceMixer = true;
        state->playback = {};
        state->mixer = {};
    }
    enqueuePendingState(pad, std::move(state));
    return true;
}

void SamplerEngine::restorePadPlaybackSettings(const std::uint32_t pad,
    const sms::dsp::SamplePlaybackSettings& settings) {
    if (pad >= kPadCount) return;
    const std::lock_guard lock(storageControlMutex_);
    auto state = preparePendingState();
    state->playback = sms::dsp::sanitize(settings);
    state->replacePlayback = true;
    enqueuePendingState(pad, std::move(state));
    collectImportedStorage();
}

void SamplerEngine::restorePadMixerSettings(const std::uint32_t pad,
    const sms::dsp::SampleMixerSettings& settings) {
    if (pad >= kPadCount) return;
    const std::lock_guard lock(storageControlMutex_);
    auto state = preparePendingState();
    state->mixer = sms::dsp::sanitize(settings);
    state->replaceMixer = true;
    enqueuePendingState(pad, std::move(state));
    collectImportedStorage();
}

void SamplerEngine::applyPendingState() noexcept {
    std::array<PendingState*, kPadCount> updates{};
    std::uint64_t required = used_blocks_;
    bool changed = false;
    for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
        auto* update = pendingState_[pad].exchange(nullptr, std::memory_order_acq_rel);
        updates[pad] = update;
        if (!update) continue;
        changed = true;
        if (update->replaceAudio) {
            required -= pads_[pad].allocatedBlocks;
            if (update->storage)
                required += (update->storage->frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
        }
    }
    if (!changed) return;
    const bool valid = required <= total_blocks_;
    stateRestoreFailed_.store(!valid, std::memory_order_release);
    if (valid) {
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            if (!updates[pad]) continue;
            PadMutation::begin(pads_[pad]);
            if (updates[pad]->replaceAudio) releasePadBlocks(pad);
        }
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            const auto* update = updates[pad];
            if (!update) continue;
            if (update->replaceAudio) publishStorage(pad, update->storage, false);
            if (update->replacePlayback) setPadPlaybackSettingsDirect(pad, update->playback);
            if (update->replaceMixer) setPadMixerSettingsDirect(pad, update->mixer);
        }
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad)
            if (updates[pad]) PadMutation::finish(pads_[pad]);
    }
    for (auto* update : updates) {
        if (!update) continue;
        if (update->storage) update->storage->references.fetch_sub(1U, std::memory_order_release);
        update->consumed.store(true, std::memory_order_release);
    }
}

void SamplerEngine::overlayPendingState(const std::uint32_t pad, Snapshot& snapshot) const noexcept {
    const auto* update = pendingState_[pad].load(std::memory_order_acquire);
    if (!update) return;
    if (update->replaceAudio) {
        if (snapshot.storage) snapshot.storage->references.fetch_sub(1U, std::memory_order_release);
        snapshot.storage = update->storage;
        auto& metadata = snapshot.metadata;
        metadata.frames = update->storage ? update->storage->frames : 0U;
        metadata.sampleRate = update->storage ? update->storage->sampleRate : metadata.sampleRate;
        metadata.peak = update->storage ? update->storage->peak : 0.0f;
        metadata.rms = update->storage ? update->storage->rms : 0.0f;
        metadata.occupied = metadata.frames != 0U;
        metadata.recording = metadata.active = false;
        if (snapshot.storage) snapshot.storage->references.fetch_add(1U, std::memory_order_relaxed);
    }
    if (update->replacePlayback) snapshot.playback = update->playback;
    if (update->replaceMixer) snapshot.mixer = update->mixer;
}

bool SamplerEngine::ensurePadBlock(const std::uint32_t pad,
                                   const std::uint32_t block) noexcept {
    if (pad >= kPadCount || block >= total_blocks_) return false;
    auto& p = pads_[pad];
    if (!p.storage) p.storage = storagePool_.acquireCapture();
    if (!p.storage || !p.storage->isCapture()) return false;
    if (storagePool_.hasCaptureBlock(*p.storage, block)) return true;
    if (used_blocks_ == total_blocks_ || !storagePool_.ensureCaptureBlock(*p.storage, block)) return false;
    ++used_blocks_;
    p.allocatedBlocks = p.storage->allocatedBlocks;
    return true;
}

void SamplerEngine::releasePadBlocks(const std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    used_blocks_ -= p.allocatedBlocks;
    p.allocatedBlocks = 0;
    if (p.storage) {
        p.storage->references.fetch_sub(1, std::memory_order_release);
        p.storage = nullptr;
    }
}

void SamplerEngine::trimPadBlocks(const std::uint32_t pad,
                                  const std::uint32_t frames) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    const std::uint32_t keep = frames == 0U ? 0U :
        (frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
    if (p.storage) {
        used_blocks_ -= storagePool_.trimCapture(*p.storage, keep);
        p.allocatedBlocks = p.storage->allocatedBlocks;
    }
}

float SamplerEngine::sampleAt(const std::uint32_t pad, const std::uint32_t frame,
                              const std::uint32_t channel) const noexcept {
    if (pad >= kPadCount || frame >= max_frames_ || channel >= 2U) return 0.0f;
    return storagePool_.sample(pads_[pad].storage, frame, channel);
}

bool SamplerEngine::storeSample(const std::uint32_t pad, const std::uint32_t frame,
                                const float left, const float right) noexcept {
    if (pad >= kPadCount || frame >= max_frames_ ||
        !ensurePadBlock(pad, frame / kSampleBlockFrames)) return false;
    storagePool_.writeCapture(*pads_[pad].storage, frame, left, right);
    return true;
}

void SamplerEngine::beginRecord(std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    const PadMutation publication(p);
    releasePadBlocks(pad);
    p.playing = false;
    p.voiceOrder = 0;
    p.envelope.reset();
    p.publishedPlaying.store(false, std::memory_order_release);
    p.recording = true;
    p.recordPosition = 0;
    p.recordedFrames = 0;
    p.recordPeak = 0.0f;
    p.recordSumSquares = 0.0;
    p.sourceSampleRate = sample_rate_;
    p.publishedFrames.store(0, std::memory_order_release);
    p.occupied.store(false, std::memory_order_release);
    p.publishedRecording.store(true, std::memory_order_release);
    p.generation.fetch_add(1U, std::memory_order_release);
    setPadPlaybackSettingsDirect(pad, {});
    setPadMixerSettingsDirect(pad, {});
    // The ring contains exactly the audio immediately preceding the trigger.
    const auto copyCount = std::min({preRollFrames_, ringCount_, max_frames_});
    const auto start = (ringWritePosition_ + ringCapacityFrames_ - copyCount) % ringCapacityFrames_;
    for (std::uint32_t i = 0; i < copyCount && i < max_frames_; ++i) {
        const auto ri = (start + i) % ringCapacityFrames_;
        const auto l = ring_[static_cast<std::size_t>(ri) * 2U];
        const auto r = ring_[static_cast<std::size_t>(ri) * 2U + 1U];
        if (!storeSample(pad, i, l, r))
            break;
        p.recordPeak = std::max(p.recordPeak, std::max(std::abs(l), std::abs(r)));
        p.recordSumSquares += static_cast<double>(l) * l + static_cast<double>(r) * r;
        p.recordPosition = i + 1U;
        p.recordedFrames = i + 1U;
    }
    activePad_ = static_cast<std::int32_t>(pad);
}

void SamplerEngine::finishRecord(std::uint32_t pad, std::uint32_t trimFrames) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    if (!p.recording) return;
    const PadMutation publication(p);
    if (trimFrames > p.recordedFrames) trimFrames = p.recordedFrames;
    for (std::uint32_t frame = p.recordedFrames - trimFrames; frame < p.recordedFrames; ++frame) {
        const auto left = sampleAt(pad, frame, 0U);
        const auto right = sampleAt(pad, frame, 1U);
        p.recordSumSquares -= static_cast<double>(left) * left + static_cast<double>(right) * right;
    }
    p.recordedFrames -= trimFrames;
    p.recordPosition = p.recordedFrames;
    trimPadBlocks(pad, p.recordedFrames);
    p.recordSumSquares = std::max(0.0, p.recordSumSquares);
    p.recording = false;
    p.publishedRecording.store(false, std::memory_order_release);
    p.sourceSampleRate = sample_rate_;
    const auto n = p.recordedFrames;
    p.publishedPeak.store(n ? p.recordPeak : 0.0f, std::memory_order_relaxed);
    p.publishedRms.store(n ? static_cast<float>(std::sqrt(p.recordSumSquares / (2.0 * n))) : 0.0f,
                         std::memory_order_relaxed);
    p.publishedFrames.store(n, std::memory_order_release);
    if (p.storage) {
        p.storage->frames = n;
        p.storage->sampleRate = sample_rate_;
        p.storage->peak = p.publishedPeak.load(std::memory_order_relaxed);
        p.storage->rms = p.publishedRms.load(std::memory_order_relaxed);
    }
    p.occupied.store(n != 0, std::memory_order_release);
    p.generation.fetch_add(1, std::memory_order_release);
    if (n != 0U)
        lastCommittedPad_ = static_cast<std::int32_t>(pad);
}

void SamplerEngine::writeRecordFrame(std::uint32_t, float left, float right) noexcept {
    if (activePad_ < 0 || activePad_ >= static_cast<std::int32_t>(kPadCount)) return;
    auto& p = pads_[static_cast<std::uint32_t>(activePad_)];
    if (!p.recording || p.recordPosition >= max_frames_) return;
    if (!storeSample(static_cast<std::uint32_t>(activePad_), p.recordPosition, left, right)) {
        finishRecord(static_cast<std::uint32_t>(activePad_));
        activePad_ = -1;
        sessionComplete_ = true;
        return;
    }
    p.recordPeak = std::max(p.recordPeak, std::max(std::abs(left), std::abs(right)));
    p.recordSumSquares += static_cast<double>(left) * left + static_cast<double>(right) * right;
    ++p.recordPosition;
    p.recordedFrames = p.recordPosition;
    const auto fixed = settings_.captureMode == CaptureMode::FixedDuration
        ? static_cast<std::uint32_t>(std::clamp(std::llround(settings_.fixedLengthSeconds * sample_rate_), 1LL, static_cast<long long>(max_frames_))) : max_frames_;
    if (p.recordPosition >= fixed) {
        finishRecord(static_cast<std::uint32_t>(activePad_));
        activePad_ = -1;
        if (nextPad_ >= kPadCount) sessionComplete_ = true;
    }
}

void SamplerEngine::startVoice(std::uint32_t pad, std::uint8_t velocity) noexcept {
    if (pad >= kPadCount || !pads_[pad].occupied.load(std::memory_order_acquire)) return;
    auto& p = pads_[pad];
    if (!p.playing)
        enforceVoiceLimit(pad);
    const auto frameCount = p.publishedFrames.load(std::memory_order_acquire);
    auto playback = padPlaybackSettings(pad);
    const auto startFrame = std::min(frameCount - 1U, static_cast<std::uint32_t>(
        std::floor(static_cast<double>(playback.start) * frameCount)));
    const auto endFrame = std::clamp(static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(playback.end) * frameCount)), startFrame + 1U, frameCount);
    p.playPosition = static_cast<double>(startFrame);
    p.voiceStartFrame = startFrame;
    p.voiceEndFrame = endFrame;
    p.releaseFromEnd = false;
    p.velocityGain = static_cast<float>(velocity) / 127.0f;
    const double sourceRate = p.sourceSampleRate.load(std::memory_order_relaxed);
    const auto mixer = padMixerSettings(pad);
    const float gain = sms::dsp::sampleGain(mixer);
    const float pan = std::clamp(mixer.pan + settings_.pan, -1.0f, 1.0f);
    const double tuneRatio = std::exp2(
        static_cast<double>(mixer.tuneSemitones + settings_.tuneSemitones) / 12.0);
    p.mixerGainLeft = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    p.mixerGainRight = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    p.dirtyPlayback = mixer.dirty >= 0.5f || settings_.dirty >= 0.5f;
    p.padFilters.reset();
    p.globalFilters.reset();
    p.padFilters.configure(mixer, sample_rate_);
    p.globalFilters.configure({0.0f, 0.0f, 0.0f, settings_.lowpass,
        settings_.highpass, settings_.filterSlope, settings_.dirty}, sample_rate_);
    p.playStep = (sourceRate / sample_rate_) * tuneRatio;
    const float regionSeconds = static_cast<float>(endFrame - startFrame) /
        static_cast<float>((sourceRate > 1.0 ? sourceRate : sample_rate_) *
                           tuneRatio);
    playback.releaseSeconds = std::min(playback.releaseSeconds, regionSeconds * 0.5f);
    p.envelope.configure(sample_rate_, playback);
    p.envelope.noteOn();
    p.playing = true;
    p.voiceOrder = nextVoiceOrder_++;
    lastPlaybackTrigger_.pad = pad;
    ++lastPlaybackTrigger_.generation;
    p.publishedPlaying.store(true, std::memory_order_release);
}

void SamplerEngine::stopVoice(std::uint32_t pad) noexcept {
    if (pad < kPadCount) {
        auto& voice = pads_[pad];
        voice.releaseFromEnd = false;
        voice.envelope.noteOff();
        if (!voice.envelope.active()) {
            voice.playing = false;
            voice.publishedPlaying.store(false, std::memory_order_release);
        }
    }
}

void SamplerEngine::hardStopVoice(const std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& voice = pads_[pad];
    voice.playing = false;
    voice.held = false;
    voice.voiceOrder = 0;
    voice.releaseFromEnd = false;
    voice.envelope.reset();
    voice.publishedPlaying.store(false, std::memory_order_release);
}

void SamplerEngine::stopAllPlayback() noexcept {
    for (std::uint32_t pad = 0; pad < kPadCount; ++pad)
        hardStopVoice(pad);
    stopChopPreview();
    playbackPositionOutput_ = 0.0f;
    playbackPositionHoldFrames_ = 0U;
    playbackPositionWasActive_ = false;
}

bool SamplerEngine::anyPlaybackActive() const noexcept {
    if (chopPreviewActive_)
        return true;
    for (const auto& pad : pads_)
        if (pad.playing)
            return true;
    return false;
}

void SamplerEngine::togglePlayback(const std::uint32_t selectedPad) noexcept {
    if (anyPlaybackActive()) {
        stopAllPlayback();
    } else if (!settings_.armed && selectedPad < kPadCount) {
        startVoice(selectedPad, 127U);
    }
}

void SamplerEngine::refreshActiveVoiceSettings(const std::uint32_t pad) noexcept {
    if (pad >= kPadCount || !pads_[pad].playing)
        return;
    auto& voice = pads_[pad];
    const auto frames = voice.publishedFrames.load(std::memory_order_acquire);
    if (frames == 0U) {
        hardStopVoice(pad);
        return;
    }

    auto playback = padPlaybackSettings(pad);
    voice.voiceEndFrame = std::clamp(static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(playback.end) * frames)),
        std::min(voice.voiceStartFrame + 1U, frames), frames);
    if (voice.playPosition >= static_cast<double>(voice.voiceEndFrame)) {
        hardStopVoice(pad);
        return;
    }

    const auto mixer = padMixerSettings(pad);
    const float gain = sms::dsp::sampleGain(mixer);
    const float pan = std::clamp(mixer.pan + settings_.pan, -1.0f, 1.0f);
    const double tuneRatio = std::exp2(
        static_cast<double>(mixer.tuneSemitones + settings_.tuneSemitones) / 12.0);
    voice.mixerGainLeft = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    voice.mixerGainRight = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    voice.dirtyPlayback = mixer.dirty >= 0.5f || settings_.dirty >= 0.5f;
    voice.padFilters.configure(mixer, sample_rate_);
    voice.globalFilters.configure({0.0f, 0.0f, 0.0f, settings_.lowpass,
        settings_.highpass, settings_.filterSlope, settings_.dirty}, sample_rate_);
    const double sourceRate = voice.sourceSampleRate.load(std::memory_order_relaxed);
    voice.playStep = (sourceRate / sample_rate_) * tuneRatio;
    const float regionSeconds = static_cast<float>(voice.voiceEndFrame - voice.voiceStartFrame) /
        static_cast<float>((sourceRate > 1.0 ? sourceRate : sample_rate_) *
                           tuneRatio);
    playback.releaseSeconds = std::min(playback.releaseSeconds, regionSeconds * 0.5f);
    voice.envelope.updateSettings(playback);

    const double remainingOutputFrames =
        (static_cast<double>(voice.voiceEndFrame) - voice.playPosition) / voice.playStep;
    if (voice.releaseFromEnd &&
        remainingOutputFrames > static_cast<double>(voice.envelope.releaseFrames())) {
        voice.envelope.resumeAfterAutomaticRelease();
        voice.releaseFromEnd = false;
    }
}

void SamplerEngine::enforceVoiceLimit(const std::uint32_t excludedPad) noexcept {
    std::uint32_t activeVoices = 0;
    for (const auto& pad : pads_)
        activeVoices += pad.playing ? 1U : 0U;

    const std::uint32_t allowedVoices = settings_.maxVoices -
        ((excludedPad < kPadCount && !pads_[excludedPad].playing) ? 1U : 0U);
    while (activeVoices > allowedVoices) {
        std::uint32_t oldestPad = kPadCount;
        std::uint64_t oldestOrder = ~std::uint64_t{0};
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            if (pad == excludedPad || !pads_[pad].playing)
                continue;
            if (pads_[pad].voiceOrder < oldestOrder) {
                oldestOrder = pads_[pad].voiceOrder;
                oldestPad = pad;
            }
        }
        if (oldestPad >= kPadCount)
            break;
        hardStopVoice(oldestPad);
        --activeVoices;
    }
}

void SamplerEngine::handleEvent(const MidiEvent& event) noexcept {
    const bool on = event.isNoteOn();
    if (!on && event.type != MidiEventType::NoteOff) return;
    if (settings_.armed) {
        if (on && settings_.captureMode == CaptureMode::FixedDuration && !sessionComplete_) {
            if (activePad_ >= 0) finishRecord(static_cast<std::uint32_t>(activePad_));
            if (nextPad_ < kPadCount) {
                const auto pad = nextPad_;
                nextPad_ = followingCapturePad(pad);
                beginRecord(pad);
            }
            else { activePad_ = -1; sessionComplete_ = true; }
        } else if (on && settings_.captureMode == CaptureMode::Sequential && activePad_ < 0 && !sessionComplete_) {
            if (nextPad_ < kPadCount) {
                const auto pad = nextPad_;
                nextPad_ = followingCapturePad(pad);
                beginRecord(pad);
            }
            else sessionComplete_ = true;
        } else if (on && settings_.captureMode == CaptureMode::Sequential && activePad_ >= 0) {
            const auto trim = std::min(preRollFrames_, pads_[static_cast<std::uint32_t>(activePad_)].recordedFrames);
            finishRecord(static_cast<std::uint32_t>(activePad_), trim);
            if (nextPad_ < kPadCount) {
                const auto pad = nextPad_;
                nextPad_ = followingCapturePad(pad);
                beginRecord(pad);
            }
            else { activePad_ = -1; sessionComplete_ = true; }
        }
        return; // note identity and note-off do not affect sequential capture
    }
    const auto pad = noteToPad(event.note);
    if (pad >= kPadCount) return;
    if (chopMidiPreview_.active) {
        if (!on || pad < chopMidiPreview_.firstPad)
            return;
        const auto previewPad = pad - chopMidiPreview_.firstPad;
        if (previewPad >= chopMidiPreview_.previewPadCount)
            return;
        const auto sourceFrame = chopMidiPreview_.sourceFrames[previewPad];
        const auto sourceEndFrame = chopMidiPreview_.sourceEndFrames[previewPad];
        if (sourceEndFrame > sourceFrame) {
            startChopPreview(chopMidiPreview_.firstPad,
                             chopMidiPreview_.sourcePadCount,
                             sourceFrame, sourceEndFrame);
        }
        return;
    }
    if (on) startVoice(pad, event.velocity);
    else if (settings_.playbackMode == PlaybackMode::Gated) stopVoice(pad);
}

void SamplerEngine::process(const float* inputLeft, const float* inputRight,
                            float* outputLeft, float* outputRight,
                            std::uint32_t frames, const MidiEvent* events,
                            std::uint32_t eventCount) noexcept {
    struct Cursor {
        const MidiEvent* events;
        std::uint32_t count;
        std::uint32_t index = 0;
    } cursor{events, eventCount};
    process(inputLeft, inputRight, outputLeft, outputRight, frames,
        MidiEventSource{&cursor, [](void* const opaque, MidiEvent& event) noexcept {
            auto& source = *static_cast<Cursor*>(opaque);
            if (source.events == nullptr || source.index >= source.count)
                return false;
            event = source.events[source.index++];
            return true;
        }});
}

void SamplerEngine::process(const float* inputLeft, const float* inputRight,
                            float* outputLeft, float* outputRight,
                            std::uint32_t frames, MidiEventSource events) noexcept {
    if (!outputLeft || !outputRight) return;
    applyPendingState();
    storagePool_.reclaimCapture(1024U);
    if (settings_.armed != previousArmed_) {
        if (settings_.armed) {
            // setSettings() already prepared either the automatic empty target
            // or a later explicit idle retarget before this audio block.
            activePad_ = -1;
        } else if (activePad_ >= 0) {
            finishRecord(static_cast<std::uint32_t>(activePad_));
            activePad_ = -1;
        }
        previousArmed_ = settings_.armed;
    }
    if (finalizeRequested_.exchange(false, std::memory_order_acq_rel) && activePad_ >= 0) {
        finishRecord(static_cast<std::uint32_t>(activePad_)); activePad_ = -1;
    }
    if (undoRequested_.exchange(false, std::memory_order_acq_rel) &&
        (activePad_ >= 0 || lastCommittedPad_ >= 0)) {
        if (activePad_ >= 0) {
            const auto active = static_cast<std::uint32_t>(activePad_);
            clearPad(active);
            nextPad_ = active;
            activePad_ = -1;
        } else {
            clearPad(static_cast<std::uint32_t>(lastCommittedPad_));
            nextPad_ = static_cast<std::uint32_t>(lastCommittedPad_);
            lastCommittedPad_ = -1;
        }
        sessionComplete_ = false;
    }
    for (std::uint32_t pad = 0; pad < kPadCount; ++pad)
        refreshActiveVoiceSettings(pad);
    MidiEvent nextEvent{};
    bool hasEvent = events.next != nullptr && events.next(events.context, nextEvent);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (hasEvent && nextEvent.frameOffset <= frame) {
            handleEvent(nextEvent);
            hasEvent = events.next(events.context, nextEvent);
        }
        const float inL = inputLeft ? inputLeft[frame] : kSilence;
        const float inR = inputRight ? inputRight[frame] : kSilence;
        if (activePad_ >= 0) writeRecordFrame(frame, inL, inR);
        float outL = 0.0f;
        float outR = 0.0f;
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            auto& p = pads_[pad];
            if (!p.playing) continue;
            const auto n = std::min(p.publishedFrames.load(std::memory_order_acquire), p.voiceEndFrame);
            if (n == 0 || !p.envelope.active()) { p.playing = false; p.publishedPlaying.store(false, std::memory_order_release); continue; }
            const auto pos = p.playPosition;
            const auto i = static_cast<std::uint32_t>(pos);
            if (i >= n) { p.playing = false; p.publishedPlaying.store(false, std::memory_order_release); continue; }
            const auto j = (i + 1U < n) ? i + 1U : i;
            const auto frac = static_cast<float>(pos - static_cast<double>(i));
            const double step = p.playStep;
            const double remainingOutputFrames = (static_cast<double>(n) - pos) / step;
            if (!p.envelope.releasing() && p.envelope.releaseFrames() > 0U &&
                remainingOutputFrames <= static_cast<double>(p.envelope.releaseFrames())) {
                p.envelope.noteOff();
                p.releaseFromEnd = true;
            }
            const float envelopeGain = p.envelope.next();
            const float voiceGain = p.velocityGain * envelopeGain;
            float sampleL, sampleR;
            if (p.dirtyPlayback) {
                const auto sourceRate = p.sourceSampleRate.load(std::memory_order_relaxed);
                const auto sourceI = sms::dsp::dirtySourceFrame(i, sourceRate);
                const auto sourceJ = sms::dsp::dirtySourceFrame(j, sourceRate);
                const float first = sms::dsp::dirtyMono(
                    sampleAt(pad, sourceI, 0U), sampleAt(pad, sourceI, 1U));
                const float second = sms::dsp::dirtyMono(
                    sampleAt(pad, sourceJ, 0U), sampleAt(pad, sourceJ, 1U));
                sampleL = sampleR = first * (1.0f - frac) + second * frac;
            } else {
                sampleL = sampleAt(pad, i, 0U) * (1.0f - frac) +
                          sampleAt(pad, j, 0U) * frac;
                sampleR = sampleAt(pad, i, 1U) * (1.0f - frac) +
                          sampleAt(pad, j, 1U) * frac;
            }
            p.padFilters.process(sampleL, sampleR);
            p.globalFilters.process(sampleL, sampleR);
            outL += sampleL * voiceGain * p.mixerGainLeft;
            outR += sampleR * voiceGain * p.mixerGainRight;
            p.playPosition += step;
            if (p.playPosition >= n || !p.envelope.active()) {
                p.playing = false;
                p.publishedPlaying.store(false, std::memory_order_release);
            }
        }
        mixChopPreview(outL, outR);
        outputLeft[frame] = outL * settings_.gain +
            (settings_.monitorInput ? inL * settings_.monitorGain : 0.0f);
        outputRight[frame] = outR * settings_.gain +
            (settings_.monitorInput ? inR * settings_.monitorGain : 0.0f);
        if (ringCapacityFrames_) {
            const auto ri = static_cast<std::size_t>(ringWritePosition_) * 2U;
            ring_[ri] = inL; ring_[ri + 1U] = inR;
            ringWritePosition_ = (ringWritePosition_ + 1U) % ringCapacityFrames_;
            ringCount_ = std::min(ringCount_ + 1U, ringCapacityFrames_);
        }
    }
    updatePlaybackPositionOutput(frames);
}

float SamplerEngine::playbackPosition() const noexcept {
    return playbackPositionOutput_;
}

void SamplerEngine::updatePlaybackPositionOutput(const std::uint32_t processedFrames) noexcept {
    const auto pad = lastPlaybackTrigger_.pad;
    const bool valid = pad < kPadCount;
    const bool active = valid && pads_[pad].playing;
    const bool newlyTriggered =
        lastPlaybackTrigger_.generation != playbackPositionGeneration_;
    if (valid && (active || newlyTriggered || playbackPositionWasActive_)) {
        const auto frames = pads_[pad].publishedFrames.load(std::memory_order_acquire);
        if (frames != 0U) {
            const float fraction = std::clamp(
                static_cast<float>(pads_[pad].playPosition / static_cast<double>(frames)),
                0.0f, active ? 0.999999f : 0.985f);
            playbackPositionOutput_ = static_cast<float>(pad + 1U) + fraction;
            playbackPositionHoldFrames_ = static_cast<std::uint32_t>(std::clamp(
                std::llround(sample_rate_ * 0.1), 1LL,
                static_cast<long long>(0xffffffffU)));
        }
        playbackPositionGeneration_ = lastPlaybackTrigger_.generation;
        playbackPositionWasActive_ = active;
        return;
    }
    if (playbackPositionHoldFrames_ > processedFrames) {
        playbackPositionHoldFrames_ -= processedFrames;
    } else {
        playbackPositionHoldFrames_ = 0U;
        playbackPositionOutput_ = 0.0f;
    }
}

void SamplerEngine::process(const float* inputLeft, const float* inputRight,
                            float* outputLeft, float* outputRight,
                            std::uint32_t frames,
                            std::span<const MidiEvent> events) noexcept {
    process(inputLeft, inputRight, outputLeft, outputRight,
            frames, events.data(),
            static_cast<std::uint32_t>(events.size()));
}

PadMetadata SamplerEngine::padMetadata(std::uint32_t pad) const noexcept {
    PadMetadata m;
    if (pad >= kPadCount) return m;
    const auto& p = pads_[pad];
    m.sampleRate = p.sourceSampleRate.load(std::memory_order_acquire);
    m.frames = p.publishedFrames.load(std::memory_order_acquire);
    m.peak = p.publishedPeak.load(std::memory_order_relaxed);
    m.rms = p.publishedRms.load(std::memory_order_relaxed);
    m.generation = p.generation.load(std::memory_order_acquire);
    m.occupied = p.occupied.load(std::memory_order_acquire);
    m.recording = p.publishedRecording.load(std::memory_order_acquire);
    m.active = p.publishedPlaying.load(std::memory_order_acquire);
    return m;
}

void SamplerEngine::captureSnapshot(const std::uint32_t pad, Snapshot& result) const noexcept {
    result.metadata = padMetadata(pad);
    result.playback = padPlaybackSettings(pad);
    result.mixer = padMixerSettings(pad);
    // A live recording has no published PCM; never pin its mutable descriptor.
    if (result.metadata.frames != 0U && !result.metadata.recording) {
        result.storage = pads_[pad].storage;
        if (result.storage) result.storage->references.fetch_add(1, std::memory_order_relaxed);
    }
}

float SamplerEngine::snapshotSample(const Snapshot& snapshot, const std::uint32_t frame,
                                    const std::uint32_t channel) const noexcept {
    if (frame >= snapshot.metadata.frames) return 0.0f;
    return storagePool_.sample(snapshot.storage, frame, channel);
}

bool SamplerEngine::summarizePadRange(const std::uint32_t firstPad,
    const std::uint32_t padCount, const std::uint64_t start, const std::uint64_t end,
    sms::audio::WaveformSummary& result) const {
    if (padCount == 0U || padCount > 3U || firstPad >= kPadCount ||
        padCount > kPadCount - firstPad || start >= end) return false;
    const std::lock_guard lock(storageControlMutex_);
    std::array<Snapshot, 3> snapshots;
    dispatchControl([&]() noexcept {
        for (std::uint32_t i = 0; i < padCount; ++i) captureSnapshot(firstPad + i, snapshots[i]);
    });
    std::array<std::uint64_t, 4> edges{};
    double rate = 0.0;
    for (std::uint32_t i = 0; i < padCount; ++i) {
        const auto& metadata = snapshots[i].metadata;
        edges[i + 1U] = edges[i] + metadata.frames;
        if (metadata.frames != 0U) {
            if (rate != 0.0 && std::abs(rate - metadata.sampleRate) > 0.5) return false;
            rate = metadata.sampleRate;
        }
    }
    if (end > edges[padCount] || end - start > UINT32_MAX) return false;
    result = {};
    result.pad = firstPad;
    result.frames = static_cast<std::uint32_t>(end - start);
    result.sampleRate = rate > 1.0 ? rate : 48000.0;
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const auto first = start + (end - start) * bin / sms::audio::kWaveformBins;
        const auto last = start + (end - start) * (bin + 1U) / sms::audio::kWaveformBins;
        float low = 1.0f, high = -1.0f;
        for (auto frame = first; frame < std::max(first + 1U, last) && frame < end; ++frame) {
            std::uint32_t local = 0;
            while (local + 1U < padCount && frame >= edges[local + 1U]) ++local;
            const auto offset = static_cast<std::uint32_t>(frame - edges[local]);
            low = std::min({low, snapshotSample(snapshots[local], offset, 0), snapshotSample(snapshots[local], offset, 1)});
            high = std::max({high, snapshotSample(snapshots[local], offset, 0), snapshotSample(snapshots[local], offset, 1)});
        }
        result.minimum[bin] = low <= high ? low : 0.0f;
        result.maximum[bin] = low <= high ? high : 0.0f;
    }
    return true;
}

bool SamplerEngine::getChopSnapshot(const std::uint32_t firstPad,
    const std::uint32_t padCount, std::span<sms::audio::WaveformSummary> summaries,
    std::span<std::uint64_t> generations) const {
    if (padCount != 3U || firstPad >= kPadCount || padCount > kPadCount - firstPad ||
        summaries.size() != padCount || generations.size() != padCount) return false;
    const std::lock_guard lock(storageControlMutex_);
    std::array<Snapshot, 3> snapshots;
    bool armed = false;
    dispatchControl([&]() noexcept {
        armed = settings_.armed;
        for (std::uint32_t i = 0; i < padCount; ++i) captureSnapshot(firstPad + i, snapshots[i]);
    });
    if (armed) return false;
    for (std::uint32_t i = 0; i < padCount; ++i) {
        const auto& snapshot = snapshots[i];
        if (snapshot.metadata.recording) return false;
        generations[i] = snapshot.metadata.generation;
        auto& result = summaries[i];
        result = {};
        result.pad = firstPad + i;
        result.frames = snapshot.metadata.frames;
        result.sampleRate = snapshot.metadata.sampleRate;
        for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
            const auto first = static_cast<std::uint32_t>(static_cast<std::uint64_t>(result.frames) * bin / sms::audio::kWaveformBins);
            const auto last = static_cast<std::uint32_t>(static_cast<std::uint64_t>(result.frames) * (bin + 1U) / sms::audio::kWaveformBins);
            float low = 1.0f, high = -1.0f;
            for (auto frame = first; frame < std::max(first + 1U, last) && frame < result.frames; ++frame) {
                low = std::min({low, snapshotSample(snapshot, frame, 0), snapshotSample(snapshot, frame, 1)});
                high = std::max({high, snapshotSample(snapshot, frame, 0), snapshotSample(snapshot, frame, 1)});
            }
            result.minimum[bin] = low <= high ? low : 0.0f;
            result.maximum[bin] = low <= high ? high : 0.0f;
        }
    }
    return true;
}

bool SamplerEngine::exportPad(const std::uint32_t pad, PadData& destination,
    sms::dsp::SamplePlaybackSettings* playback, sms::dsp::SampleMixerSettings* mixer) const {
    if (pad >= kPadCount) return false;
    const std::lock_guard lock(storageControlMutex_);
    Snapshot snapshot;
    dispatchControl([&]() noexcept { captureSnapshot(pad, snapshot); });
    copySnapshot(snapshot, destination, playback, mixer);
    return true;
}

void SamplerEngine::capturePublishedSnapshot(const std::uint32_t pad, Snapshot& result) const {
    const auto& p = pads_[pad];
    for (;;) {
        const auto before = p.publicationSequence.load(std::memory_order_acquire);
        if ((before & 1U) != 0U) { std::this_thread::yield(); continue; }
        result.metadata = padMetadata(pad);
        result.playback = padPlaybackSettings(pad);
        result.mixer = padMixerSettings(pad);
        Storage* storage = p.publishedStorage.load(std::memory_order_acquire);
        if (result.metadata.frames == 0U || result.metadata.recording) storage = nullptr;
        // Imported allocations cannot be deleted while storageControlMutex_ is held.
        // Capture descriptors are permanent. CAS acquisition prevents their reuse
        // while this reference is being validated against the publication sequence.
        if (storage) {
            auto references = storage->references.load(std::memory_order_acquire);
            while (references != 0U && !storage->references.compare_exchange_weak(
                references, references + 1U, std::memory_order_acq_rel)) {}
            if (references == 0U) continue; // retired descriptors must never be resurrected
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.publicationSequence.load(std::memory_order_acquire) == before) {
            result.storage = storage;
            return;
        }
        if (storage) storage->references.fetch_sub(1U, std::memory_order_release);
    }
}

bool SamplerEngine::exportPublishedPad(const std::uint32_t pad, PadData& destination,
    sms::dsp::SamplePlaybackSettings* playback, sms::dsp::SampleMixerSettings* mixer) const {
    if (pad >= kPadCount) return false;
    const std::lock_guard lock(storageControlMutex_);
    Snapshot snapshot;
    capturePublishedSnapshot(pad, snapshot);
    overlayPendingState(pad, snapshot);
    copySnapshot(snapshot, destination, playback, mixer);
    return true;
}

void SamplerEngine::snapshotPadSettings(const std::uint32_t pad,
    sms::dsp::SamplePlaybackSettings& playback, sms::dsp::SampleMixerSettings& mixer) const {
    if (pad >= kPadCount) { playback = {}; mixer = {}; return; }
    const std::lock_guard lock(storageControlMutex_);
    Snapshot snapshot;
    capturePublishedSnapshot(pad, snapshot);
    overlayPendingState(pad, snapshot);
    playback = snapshot.playback;
    mixer = snapshot.mixer;
}

void SamplerEngine::copySnapshot(const Snapshot& snapshot, PadData& destination,
    sms::dsp::SamplePlaybackSettings* playback, sms::dsp::SampleMixerSettings* mixer) const {
    destination.sampleRate = snapshot.metadata.sampleRate;
    destination.frames = snapshot.metadata.frames;
    destination.peak = snapshot.metadata.peak;
    destination.rms = snapshot.metadata.rms;
    destination.generation = snapshot.metadata.generation;
    if (playback) *playback = snapshot.playback;
    if (mixer) *mixer = snapshot.mixer;
    destination.stereo.resize(static_cast<std::size_t>(destination.frames) * 2U);
    storagePool_.copySamples(snapshot.storage, destination.stereo);
}

SamplerEngine::Storage* SamplerEngine::prepareStorage(const PadData& source) {
    return storagePool_.prepareImport(source, sample_rate_, max_frames_);
}

void SamplerEngine::publishStorage(const std::uint32_t pad, Storage* storage,
                                   const bool resetSettings) noexcept {
    if (!storage) { clearPad(pad); return; }
    auto& p = pads_[pad];
    const PadMutation publication(p);
    stopChopPreview();
    if (activePad_ == static_cast<std::int32_t>(pad)) {
        activePad_ = -1; nextPad_ = pad; sessionComplete_ = false;
    }
    hardStopVoice(pad);
    p.recording = p.held = false;
    p.publishedRecording.store(false, std::memory_order_release);
    releasePadBlocks(pad);
    storage->references.fetch_add(1, std::memory_order_relaxed);
    p.storage = storage;
    p.allocatedBlocks = (storage->frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
    used_blocks_ += p.allocatedBlocks;
    p.sourceSampleRate.store(storage->sampleRate, std::memory_order_release);
    p.recordedFrames = p.recordPosition = storage->frames;
    p.publishedFrames.store(storage->frames, std::memory_order_release);
    p.publishedPeak.store(storage->peak, std::memory_order_relaxed);
    p.publishedRms.store(storage->rms, std::memory_order_relaxed);
    p.occupied.store(true, std::memory_order_release);
    if (resetSettings) { setPadPlaybackSettingsDirect(pad, {}); setPadMixerSettingsDirect(pad, {}); }
    p.generation.fetch_add(1, std::memory_order_release);
}

bool SamplerEngine::importPad(const std::uint32_t pad, const PadData& source,
                              const bool resetEditorSettings,
    const sms::dsp::SamplePlaybackSettings* playback,
    const sms::dsp::SampleMixerSettings* mixer,
    const std::optional<std::uint64_t> expectedGeneration) {
    if (pad >= kPadCount) return false;
    const std::lock_guard lock(storageControlMutex_);
    collectImportedStorage();
    std::uint64_t baseline = 0;
    bool recording = false;
    dispatchControl([&]() noexcept { const auto m = padMetadata(pad); baseline = m.generation; recording = m.recording; });
    if (recording || (expectedGeneration && *expectedGeneration != baseline)) return false;
    auto* storage = prepareStorage(source);
    if (!storage) return false;
    bool imported = false;
    dispatchControl([&]() noexcept {
        const auto required = (storage->frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
        if (pads_[pad].generation.load(std::memory_order_acquire) != baseline ||
            pads_[pad].recording || required > total_blocks_ - used_blocks_ + pads_[pad].allocatedBlocks) return;
        const PadMutation publication(pads_[pad]);
        publishStorage(pad, storage, resetEditorSettings);
        if (playback) setPadPlaybackSettingsDirect(pad, *playback);
        if (mixer) setPadMixerSettingsDirect(pad, *mixer);
        imported = true;
    });
    collectImportedStorage();
    return imported;
}

bool SamplerEngine::rechopPads(const std::uint32_t firstPad, const std::uint32_t padCount,
    const std::span<const std::int64_t> boundaryOffsets,
    const std::span<const std::uint64_t> expectedGenerations) {
    if (padCount < 2U || padCount > kPadsPerBank || firstPad >= kPadCount ||
        padCount > kPadCount - firstPad || boundaryOffsets.size() != padCount - 1U ||
        (!expectedGenerations.empty() && expectedGenerations.size() != padCount)) return false;
    const std::lock_guard lock(storageControlMutex_);
    collectImportedStorage();
    std::array<Snapshot, kPadsPerBank> snapshots;
    bool armed = false;
    dispatchControl([&]() noexcept {
        armed = settings_.armed;
        for (std::uint32_t i = 0; i < padCount; ++i) captureSnapshot(firstPad + i, snapshots[i]);
    });
    if (armed) return false;
    std::array<std::uint64_t, kPadsPerBank + 1U> edges{}, moved{};
    double rate = 0.0;
    for (std::uint32_t i = 0; i < padCount; ++i) {
        const auto& m = snapshots[i].metadata;
        if (m.recording || (!expectedGenerations.empty() && expectedGenerations[i] != m.generation)) return false;
        if (m.frames) {
            if (rate != 0.0 && std::abs(rate - m.sampleRate) > 0.5) return false;
            rate = m.sampleRate;
        }
        edges[i + 1U] = edges[i] + m.frames;
    }
    if (rate == 0.0) return false;
    for (std::uint32_t i = 1; i < padCount; ++i) {
        const auto boundary = static_cast<std::int64_t>(edges[i]) + boundaryOffsets[i - 1U];
        if (boundary < 0 || static_cast<std::uint64_t>(boundary) < moved[i - 1U]) return false;
        moved[i] = static_cast<std::uint64_t>(boundary);
    }
    moved[padCount] = edges[padCount];
    std::array<Storage*, kPadsPerBank> replacements{};
    std::uint32_t required = 0;
    for (std::uint32_t i = 0; i < padCount; ++i) {
        if (moved[i + 1U] < moved[i] || moved[i + 1U] - moved[i] > max_frames_) return false;
        PadData staged;
        staged.sampleRate = rate;
        staged.frames = static_cast<std::uint32_t>(moved[i + 1U] - moved[i]);
        required += (staged.frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
        if (!staged.frames) continue;
        staged.stereo.resize(static_cast<std::size_t>(staged.frames) * 2U);
        double squares = 0;
        std::uint32_t sourcePad = 0;
        for (std::uint32_t frame = 0; frame < staged.frames; ++frame) {
            const auto sourceFrame = moved[i] + frame;
            while (sourcePad + 1U < padCount && sourceFrame >= edges[sourcePad + 1U]) ++sourcePad;
            for (std::uint32_t channel = 0; channel < 2U; ++channel) {
                const float sample = snapshotSample(snapshots[sourcePad], static_cast<std::uint32_t>(sourceFrame - edges[sourcePad]), channel);
                staged.stereo[static_cast<std::size_t>(frame) * 2U + channel] = sample;
                staged.peak = std::max(staged.peak, std::abs(sample));
                squares += static_cast<double>(sample) * sample;
            }
        }
        staged.rms = static_cast<float>(std::sqrt(squares / (static_cast<double>(staged.frames) * 2.0)));
        replacements[i] = prepareStorage(staged);
        if (!replacements[i]) return false;
    }
    bool applied = false;
    dispatchControl([&]() noexcept {
        if (settings_.armed) return;
        std::uint32_t oldBlocks = 0;
        for (std::uint32_t i = 0; i < padCount; ++i) {
            if (pads_[firstPad + i].generation.load(std::memory_order_acquire) != snapshots[i].metadata.generation) return;
            oldBlocks += pads_[firstPad + i].allocatedBlocks;
        }
        if (required > total_blocks_ - used_blocks_ + oldBlocks) return;
        // Release the whole transaction's capacity before publishing any replacement.
        for (std::uint32_t i = 0; i < padCount; ++i) PadMutation::begin(pads_[firstPad + i]);
        for (std::uint32_t i = 0; i < padCount; ++i) releasePadBlocks(firstPad + i);
        for (std::uint32_t i = 0; i < padCount; ++i) {
            publishStorage(firstPad + i, replacements[i], false);
            const bool changed = (i > 0U && boundaryOffsets[i - 1U] != 0) || (i + 1U < padCount && boundaryOffsets[i] != 0);
            setPadPlaybackSettingsDirect(firstPad + i, !replacements[i] || changed ? sms::dsp::SamplePlaybackSettings{} : snapshots[i].playback);
            if (replacements[i]) setPadMixerSettingsDirect(firstPad + i, snapshots[i].mixer);
        }
        for (std::uint32_t i = 0; i < padCount; ++i) PadMutation::finish(pads_[firstPad + i]);
        applied = true;
    });
    collectImportedStorage();
    return applied;
}

void SamplerEngine::movePadContents(const std::uint32_t source,
                                    const std::uint32_t destination) noexcept {
    auto& from = pads_[source];
    auto& to = pads_[destination];
    const PadMutation sourcePublication(from), destinationPublication(to);
    const auto playback = padPlaybackSettings(source);
    const auto mixer = padMixerSettings(source);

    hardStopVoice(source);
    hardStopVoice(destination);
    to.recording = false;
    from.recording = false;
    to.publishedRecording.store(false, std::memory_order_release);
    from.publishedRecording.store(false, std::memory_order_release);
    releasePadBlocks(destination);
    to.storage = from.storage;
    from.storage = nullptr;

    to.recordedFrames = from.recordedFrames;
    to.recordPosition = from.recordPosition;
    to.allocatedBlocks = from.allocatedBlocks;
    to.recordPeak = from.recordPeak;
    to.recordSumSquares = from.recordSumSquares;
    to.sourceSampleRate.store(
        from.sourceSampleRate.load(std::memory_order_acquire), std::memory_order_release);
    to.publishedFrames.store(
        from.publishedFrames.load(std::memory_order_acquire), std::memory_order_release);
    to.publishedPeak.store(
        from.publishedPeak.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.publishedRms.store(
        from.publishedRms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.occupied.store(true, std::memory_order_release);
    setPadPlaybackSettingsDirect(destination, playback);
    setPadMixerSettingsDirect(destination, mixer);

    from.recordedFrames = 0U;
    from.recordPosition = 0U;
    from.allocatedBlocks = 0U;
    from.recordPeak = 0.0f;
    from.recordSumSquares = 0.0;
    from.sourceSampleRate.store(sample_rate_, std::memory_order_release);
    from.publishedFrames.store(0U, std::memory_order_release);
    from.publishedPeak.store(0.0f, std::memory_order_relaxed);
    from.publishedRms.store(0.0f, std::memory_order_relaxed);
    from.occupied.store(false, std::memory_order_release);
    setPadPlaybackSettingsDirect(source, {});
    setPadMixerSettingsDirect(source, {});
    from.generation.fetch_add(1U, std::memory_order_release);
    to.generation.fetch_add(1U, std::memory_order_release);
}

bool SamplerEngine::collapsePadGap(const std::uint32_t firstPad,
                                   const std::uint32_t padCount,
                                   const std::uint32_t targetPad) {
    const std::lock_guard lock(storageControlMutex_);
    bool result = false;
    dispatchControl([&]() noexcept {
    auto collapse = [&]() noexcept -> bool {
    const std::uint32_t visibleFirst = static_cast<std::uint32_t>(settings_.activeBank) *
        bankStride(settings_.padsPerBank, settings_.midiBankMode);
    if (settings_.armed || activePad_ >= 0 || firstPad != visibleFirst ||
        padCount != settings_.padsPerBank || padCount == 0U || padCount > kPadsPerBank ||
        firstPad >= kPadCount || padCount > kPadCount - firstPad ||
        targetPad < firstPad || targetPad >= firstPad + padCount ||
        pads_[targetPad].occupied.load(std::memory_order_acquire))
        return false;

    std::uint32_t gapStart = targetPad;
    while (gapStart > firstPad &&
           !pads_[gapStart - 1U].occupied.load(std::memory_order_acquire))
        --gapStart;
    std::uint32_t runStart = targetPad + 1U;
    const std::uint32_t end = firstPad + padCount;
    while (runStart < end &&
           !pads_[runStart].occupied.load(std::memory_order_acquire))
        ++runStart;
    if (runStart == end)
        return false;
    std::uint32_t runEnd = runStart;
    while (runEnd < end && pads_[runEnd].occupied.load(std::memory_order_acquire))
        ++runEnd;

    stopChopPreview();
    const std::uint32_t gapSize = runStart - gapStart;
    for (std::uint32_t source = runStart; source < runEnd; ++source)
        movePadContents(source, source - gapSize);
    lastCommittedPad_ = -1;
    playbackPositionOutput_ = 0.0f;
    playbackPositionHoldFrames_ = 0U;
    playbackPositionWasActive_ = false;
    return true;
    };
    result = collapse();
    });
    return result;
}

bool SamplerEngine::splitPadAndShiftRight(const std::uint32_t firstPad,
    const std::uint32_t emptyPad, const std::uint32_t splitFrame,
    const std::span<const std::uint64_t> expectedGenerations) {
    if (firstPad >= kPadCount || emptyPad <= firstPad || emptyPad >= kPadCount ||
        emptyPad - firstPad >= kPadsPerBank || expectedGenerations.size() != emptyPad - firstPad + 1U) return false;
    const std::lock_guard lock(storageControlMutex_);
    collectImportedStorage();
    Snapshot snapshot;
    EngineSettings baselineSettings;
    bool valid = false;
    auto validate = [&]() noexcept {
        const auto first = static_cast<std::uint32_t>(settings_.activeBank) * bankStride(settings_.padsPerBank, settings_.midiBankMode);
        if (settings_.armed || activePad_ >= 0 || firstPad < first || emptyPad >= first + settings_.padsPerBank) return false;
        for (std::uint32_t pad = firstPad; pad <= emptyPad; ++pad) {
            const auto& p = pads_[pad];
            if (p.generation.load(std::memory_order_acquire) != expectedGenerations[pad - firstPad] ||
                p.occupied.load(std::memory_order_acquire) != (pad < emptyPad)) return false;
        }
        return true;
    };
    dispatchControl([&]() noexcept { valid = validate(); baselineSettings = settings_; if (valid) captureSnapshot(firstPad, snapshot); });
    if (!valid || splitFrame == 0U || splitFrame >= snapshot.metadata.frames) return false;
    std::array<Storage*, 2> halves{};
    std::uint32_t required = 0;
    for (std::uint32_t half = 0; half < 2U; ++half) {
        PadData staged;
        staged.sampleRate = snapshot.metadata.sampleRate;
        const auto start = half == 0U ? 0U : splitFrame;
        staged.frames = half == 0U ? splitFrame : snapshot.metadata.frames - splitFrame;
        required += (staged.frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
        staged.stereo.resize(static_cast<std::size_t>(staged.frames) * 2U);
        double squares = 0;
        for (std::uint32_t frame = 0; frame < staged.frames; ++frame) {
            for (std::uint32_t channel = 0; channel < 2U; ++channel) {
                const auto sample = snapshotSample(snapshot, start + frame, channel);
                staged.stereo[static_cast<std::size_t>(frame) * 2U + channel] = sample;
                staged.peak = std::max(staged.peak, std::abs(sample));
                squares += static_cast<double>(sample) * sample;
            }
        }
        staged.rms = static_cast<float>(std::sqrt(squares / (static_cast<double>(staged.frames) * 2.0)));
        halves[half] = prepareStorage(staged);
    }
    bool applied = false;
    dispatchControl([&]() noexcept {
        if (settings_.activeBank != baselineSettings.activeBank ||
            settings_.padsPerBank != baselineSettings.padsPerBank ||
            settings_.midiBankMode != baselineSettings.midiBankMode ||
            !validate() || required > total_blocks_ - used_blocks_ + pads_[firstPad].allocatedBlocks) return;
        for (std::uint32_t pad = firstPad; pad <= emptyPad; ++pad) PadMutation::begin(pads_[pad]);
        stopChopPreview();
        for (std::uint32_t pad = emptyPad; pad > firstPad + 1U; --pad) movePadContents(pad - 1U, pad);
        releasePadBlocks(firstPad);
        publishStorage(firstPad, halves[0], true);
        publishStorage(firstPad + 1U, halves[1], true);
        setPadMixerSettingsDirect(firstPad, snapshot.mixer);
        setPadMixerSettingsDirect(firstPad + 1U, snapshot.mixer);
        for (std::uint32_t pad = firstPad; pad <= emptyPad; ++pad) PadMutation::finish(pads_[pad]);
        lastCommittedPad_ = -1;
        playbackPositionOutput_ = 0.0f;
        playbackPositionHoldFrames_ = 0U;
        playbackPositionWasActive_ = false;
        applied = true;
    });
    collectImportedStorage();
    return applied;
}

void SamplerEngine::startChopPreview(const std::uint32_t firstPad,
                                     const std::uint32_t padCount,
                                     const std::uint64_t sourceFrame,
                                     const std::uint64_t sourceEndFrame) noexcept {
    stopChopPreview();
    if (settings_.armed || padCount == 0U || firstPad >= kPadCount ||
        padCount > kPadCount - firstPad || sourceEndFrame <= sourceFrame)
        return;
    for (std::uint32_t pad = 0; pad < kPadCount; ++pad)
        hardStopVoice(pad);
    std::uint64_t remaining = sourceFrame;
    for (std::uint32_t index = 0; index < padCount; ++index) {
        const auto frames = pads_[firstPad + index].publishedFrames.load(std::memory_order_acquire);
        if (frames == 0U)
            continue;
        if (remaining < frames) {
            chopPreviewFirstPad_ = firstPad;
            chopPreviewPadCount_ = padCount;
            chopPreviewPad_ = firstPad + index;
            chopPreviewFrame_ = static_cast<double>(remaining);
            chopPreviewRemainingFrames_ = static_cast<double>(sourceEndFrame - sourceFrame);
            chopPreviewActive_ = true;
            return;
        }
        remaining -= frames;
    }
}

void SamplerEngine::stopChopPreview() noexcept {
    chopPreviewActive_ = false;
    chopPreviewPadCount_ = 0U;
    chopPreviewFrame_ = 0.0;
    chopPreviewRemainingFrames_ = 0.0;
}

void SamplerEngine::setChopMidiPreview(const ChopMidiPreview& preview) noexcept {
    const bool valid = preview.active && preview.firstPad < kPadCount &&
        preview.sourcePadCount > 0U && preview.sourcePadCount <= 3U &&
        preview.sourcePadCount <= kPadCount - preview.firstPad &&
        preview.previewPadCount <= 3U &&
        preview.previewPadCount <= kPadCount - preview.firstPad;
    if (!valid) {
        chopMidiPreview_ = {};
        stopChopPreview();
        return;
    }
    if (!chopMidiPreview_.active) {
        stopChopPreview();
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad)
            hardStopVoice(pad);
    }
    chopMidiPreview_ = preview;
}

float SamplerEngine::chopPreviewPosition() const noexcept {
    if (!chopPreviewActive_ || chopPreviewRemainingFrames_ <= 0.0 ||
        chopPreviewPad_ >= kPadCount)
        return 0.0f;
    const auto frames = pads_[chopPreviewPad_].publishedFrames.load(std::memory_order_acquire);
    if (frames == 0U ||
        (chopPreviewPad_ + 1U >= chopPreviewFirstPad_ + chopPreviewPadCount_ &&
         chopPreviewFrame_ >= frames))
        return 0.0f;
    return static_cast<float>(chopPreviewPad_ + 1U) +
           static_cast<float>(std::clamp(chopPreviewFrame_ / frames, 0.0, 0.999999));
}

void SamplerEngine::mixChopPreview(float& left, float& right) noexcept {
    if (!chopPreviewActive_ || chopPreviewRemainingFrames_ <= 0.0) {
        stopChopPreview();
        return;
    }
    const auto endPad = chopPreviewFirstPad_ + chopPreviewPadCount_;
    while (chopPreviewPad_ < endPad) {
        const auto frames = pads_[chopPreviewPad_].publishedFrames.load(std::memory_order_acquire);
        if (frames == 0U) {
            ++chopPreviewPad_;
            continue;
        }
        if (chopPreviewFrame_ < frames)
            break;
        chopPreviewFrame_ -= frames;
        ++chopPreviewPad_;
    }
    if (chopPreviewPad_ >= endPad) {
        stopChopPreview();
        return;
    }

    const auto frames = pads_[chopPreviewPad_].publishedFrames.load(std::memory_order_acquire);
    const auto first = static_cast<std::uint32_t>(chopPreviewFrame_);
    const auto second = std::min(first + 1U, frames - 1U);
    const auto fraction = static_cast<float>(chopPreviewFrame_ - first);
    left += sampleAt(chopPreviewPad_, first, 0U) * (1.0f - fraction) +
            sampleAt(chopPreviewPad_, second, 0U) * fraction;
    right += sampleAt(chopPreviewPad_, first, 1U) * (1.0f - fraction) +
             sampleAt(chopPreviewPad_, second, 1U) * fraction;
    const double sourceRate =
        pads_[chopPreviewPad_].sourceSampleRate.load(std::memory_order_relaxed);
    const double advance = sourceRate / sample_rate_;
    chopPreviewFrame_ += advance;
    chopPreviewRemainingFrames_ -= advance;
    if (chopPreviewRemainingFrames_ <= 0.0)
        stopChopPreview();
}

void SamplerEngine::setPadPlaybackSettings(const std::uint32_t pad,
    const sms::dsp::SamplePlaybackSettings& settings) {
    dispatchControl([&]() noexcept { setPadPlaybackSettingsDirect(pad, settings); });
}

void SamplerEngine::setPadMixerSettings(const std::uint32_t pad,
    const sms::dsp::SampleMixerSettings& settings) {
    dispatchControl([&]() noexcept { setPadMixerSettingsDirect(pad, settings); });
}

sms::dsp::SamplePlaybackSettings SamplerEngine::padPlaybackSettings(const std::uint32_t pad) const noexcept {
    if (pad >= kPadCount) return {};
    const auto& p = pads_[pad];
    return sms::dsp::sanitize(sms::dsp::SamplePlaybackSettings{
        p.regionStart.load(std::memory_order_acquire),
        p.regionEnd.load(std::memory_order_acquire),
        p.attackSeconds.load(std::memory_order_acquire),
        p.decaySeconds.load(std::memory_order_acquire),
        p.sustainLevel.load(std::memory_order_acquire),
        p.releaseSeconds.load(std::memory_order_acquire),
    });
}

void SamplerEngine::setPadPlaybackSettingsDirect(
    const std::uint32_t pad, const sms::dsp::SamplePlaybackSettings& requested) noexcept {
    if (pad >= kPadCount) return;
    const auto settings = sms::dsp::sanitize(requested);
    auto& p = pads_[pad];
    const PadMutation publication(p);
    const bool changed =
        p.regionStart.load(std::memory_order_acquire) != settings.start ||
        p.regionEnd.load(std::memory_order_acquire) != settings.end ||
        p.attackSeconds.load(std::memory_order_acquire) != settings.attackSeconds ||
        p.decaySeconds.load(std::memory_order_acquire) != settings.decaySeconds ||
        p.sustainLevel.load(std::memory_order_acquire) != settings.sustainLevel ||
        p.releaseSeconds.load(std::memory_order_acquire) != settings.releaseSeconds;
    p.regionStart.store(settings.start, std::memory_order_release);
    p.regionEnd.store(settings.end, std::memory_order_release);
    p.attackSeconds.store(settings.attackSeconds, std::memory_order_release);
    p.decaySeconds.store(settings.decaySeconds, std::memory_order_release);
    p.sustainLevel.store(settings.sustainLevel, std::memory_order_release);
    p.releaseSeconds.store(settings.releaseSeconds, std::memory_order_release);
    if (changed)
        p.generation.fetch_add(1U, std::memory_order_release);
}

sms::dsp::SampleMixerSettings SamplerEngine::padMixerSettings(
    const std::uint32_t pad) const noexcept
{
    if (pad >= kPadCount) return {};
    const auto& p = pads_[pad];
    return sms::dsp::sanitize(sms::dsp::SampleMixerSettings{
        p.mixerGainDecibels.load(std::memory_order_acquire),
        p.mixerPan.load(std::memory_order_acquire),
        p.mixerTuneSemitones.load(std::memory_order_acquire),
        p.mixerLowpass.load(std::memory_order_acquire),
        p.mixerHighpass.load(std::memory_order_acquire),
        p.mixerFilterSlope.load(std::memory_order_acquire),
        p.mixerDirty.load(std::memory_order_acquire),
    });
}

void SamplerEngine::setPadMixerSettingsDirect(
    const std::uint32_t pad, const sms::dsp::SampleMixerSettings& requested) noexcept
{
    if (pad >= kPadCount) return;
    const auto settings = sms::dsp::sanitize(requested);
    auto& p = pads_[pad];
    const PadMutation publication(p);
    const bool changed =
        p.mixerGainDecibels.load(std::memory_order_acquire) != settings.gainDecibels ||
        p.mixerPan.load(std::memory_order_acquire) != settings.pan ||
        p.mixerTuneSemitones.load(std::memory_order_acquire) != settings.tuneSemitones ||
        p.mixerLowpass.load(std::memory_order_acquire) != settings.lowpass ||
        p.mixerHighpass.load(std::memory_order_acquire) != settings.highpass ||
        p.mixerFilterSlope.load(std::memory_order_acquire) != settings.filterSlope ||
        p.mixerDirty.load(std::memory_order_acquire) != settings.dirty;
    p.mixerGainDecibels.store(settings.gainDecibels, std::memory_order_release);
    p.mixerPan.store(settings.pan, std::memory_order_release);
    p.mixerTuneSemitones.store(settings.tuneSemitones, std::memory_order_release);
    p.mixerLowpass.store(settings.lowpass, std::memory_order_release);
    p.mixerHighpass.store(settings.highpass, std::memory_order_release);
    p.mixerFilterSlope.store(settings.filterSlope, std::memory_order_release);
    p.mixerDirty.store(settings.dirty, std::memory_order_release);
    if (changed)
        p.generation.fetch_add(1U, std::memory_order_release);
}

void SamplerEngine::clearPad(std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    stopChopPreview();
    auto& p = pads_[pad];
    const PadMutation publication(p);
    if (activePad_ == static_cast<std::int32_t>(pad)) {
        activePad_ = -1;
        nextPad_ = pad;
        sessionComplete_ = false;
    }
    p.recording = p.held = p.playing = false;
    p.voiceOrder = 0;
    p.envelope.reset();
    p.publishedPlaying.store(false, std::memory_order_release);
    p.publishedRecording.store(false, std::memory_order_release);
    releasePadBlocks(pad);
    p.publishedFrames.store(0, std::memory_order_release);
    p.occupied.store(false, std::memory_order_release);
    p.publishedPeak.store(0.0f, std::memory_order_relaxed);
    p.publishedRms.store(0.0f, std::memory_order_relaxed);
    setPadPlaybackSettingsDirect(pad, {});
    setPadMixerSettingsDirect(pad, {});
    p.generation.fetch_add(1, std::memory_order_release);
}

void SamplerEngine::clearAllPads() noexcept {
    for (std::uint32_t i = 0; i < kPadCount; ++i) clearPad(i);
    activePad_ = -1; lastCommittedPad_ = -1;
    resetCaptureTarget(settings_.armed);
}

void SamplerEngine::finalizeRecording() noexcept { finalizeRequested_.store(true, std::memory_order_release); }
void SamplerEngine::undoLastSlice() noexcept { undoRequested_.store(true, std::memory_order_release); }

} // namespace midichopper
