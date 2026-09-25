#include "SamplerEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace midichopper {
namespace {
constexpr float kSilence = 0.0f;
constexpr std::uint32_t kSampleBlockFrames = 1024;
constexpr std::uint32_t kNoBlock = ~std::uint32_t{0};
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
      blocks_per_pad_(total_blocks_),
      samples_(static_cast<std::size_t>(total_blocks_) * kSampleBlockFrames * 2U, 0.0f),
      pad_blocks_(static_cast<std::size_t>(kPadCount) * blocks_per_pad_, kNoBlock),
      free_blocks_(total_blocks_),
      free_block_count_(total_blocks_),
      ringCapacityFrames_(static_cast<std::uint32_t>(std::max(1.0, std::ceil(sample_rate_ * 0.1)))),
      ring_(static_cast<std::size_t>(ringCapacityFrames_) * 2U, 0.0f) {
    for (std::uint32_t block = 0; block < total_blocks_; ++block)
        free_blocks_[block] = total_blocks_ - block - 1U;
    for (auto& p : pads_) p.sourceSampleRate = sample_rate_;
    setSettings(settings_);
}

void SamplerEngine::setSampleRate(double sampleRate) {
    if (sampleRate <= 1.0 || sampleRate == sample_rate_) return;

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
        blocks_per_pad_ = total_blocks_;
        max_frames_ = total_blocks_ * kSampleBlockFrames;
        samples_.assign(static_cast<std::size_t>(total_blocks_) * kSampleBlockFrames * 2U, 0.0f);
        pad_blocks_.assign(static_cast<std::size_t>(kPadCount) * blocks_per_pad_, kNoBlock);
        free_blocks_.resize(total_blocks_);
        free_block_count_ = total_blocks_;
        for (std::uint32_t block = 0; block < total_blocks_; ++block)
            free_blocks_[block] = total_blocks_ - block - 1U;
        for (auto& pad : pads_) {
            pad.allocatedBlocks = 0;
            pad.recordedFrames = pad.recordPosition = 0;
            pad.publishedFrames.store(0, std::memory_order_release);
            pad.occupied.store(false, std::memory_order_release);
        }
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            if (occupied[pad]) {
                static_cast<void>(importPad(pad, snapshots[pad], false));
                setPadPlaybackSettings(pad, playbackSettings[pad]);
                setPadMixerSettings(pad, mixerSettings[pad]);
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
        releasePadBlocks(pad);
        auto& p = pads_[pad];
        p.recording = p.held = p.playing = false;
        p.voiceOrder = 0;
        p.envelope.reset();
        p.publishedPlaying.store(false, std::memory_order_release);
        p.recordedFrames = p.recordPosition = 0;
        p.publishedFrames.store(0, std::memory_order_release);
        p.occupied.store(false, std::memory_order_release);
        p.publishedRecording.store(false, std::memory_order_release);
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
    settings_.pan = std::clamp(std::isfinite(settings_.pan) ? settings_.pan : 0.0f,
                               -1.0f, 1.0f);
    settings_.tuneSemitones = std::clamp(
        std::isfinite(settings_.tuneSemitones) ? settings_.tuneSemitones : 0.0f,
        -24.0f, 24.0f);
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

bool SamplerEngine::ensurePadBlock(const std::uint32_t pad,
                                   const std::uint32_t block) noexcept {
    if (pad >= kPadCount || block >= blocks_per_pad_)
        return false;
    auto& mapped = pad_blocks_[static_cast<std::size_t>(pad) * blocks_per_pad_ + block];
    if (mapped != kNoBlock)
        return true;
    if (free_block_count_ == 0U)
        return false;
    mapped = free_blocks_[--free_block_count_];
    pads_[pad].allocatedBlocks = std::max(pads_[pad].allocatedBlocks, block + 1U);
    return true;
}

void SamplerEngine::releasePadBlocks(const std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& metadata = pads_[pad];
    for (std::uint32_t block = 0; block < metadata.allocatedBlocks; ++block) {
        auto& mapped = pad_blocks_[static_cast<std::size_t>(pad) * blocks_per_pad_ + block];
        if (mapped != kNoBlock) {
            free_blocks_[free_block_count_++] = mapped;
            mapped = kNoBlock;
        }
    }
    metadata.allocatedBlocks = 0;
}

void SamplerEngine::trimPadBlocks(const std::uint32_t pad,
                                  const std::uint32_t frames) noexcept {
    if (pad >= kPadCount) return;
    auto& metadata = pads_[pad];
    const std::uint32_t blocksToKeep = frames == 0U ? 0U :
        (frames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
    for (std::uint32_t block = blocksToKeep; block < metadata.allocatedBlocks; ++block) {
        auto& mapped = pad_blocks_[static_cast<std::size_t>(pad) * blocks_per_pad_ + block];
        if (mapped != kNoBlock) {
            free_blocks_[free_block_count_++] = mapped;
            mapped = kNoBlock;
        }
    }
    metadata.allocatedBlocks = blocksToKeep;
}

float SamplerEngine::sampleAt(const std::uint32_t pad, const std::uint32_t frame,
                              const std::uint32_t channel) const noexcept {
    if (pad >= kPadCount || frame >= max_frames_ || channel >= 2U)
        return 0.0f;
    const std::uint32_t logicalBlock = frame / kSampleBlockFrames;
    const std::uint32_t mapped =
        pad_blocks_[static_cast<std::size_t>(pad) * blocks_per_pad_ + logicalBlock];
    if (mapped == kNoBlock)
        return 0.0f;
    const std::size_t offset =
        (static_cast<std::size_t>(mapped) * kSampleBlockFrames +
         frame % kSampleBlockFrames) * 2U + channel;
    return samples_[offset];
}

bool SamplerEngine::storeSample(const std::uint32_t pad, const std::uint32_t frame,
                                const float left, const float right) noexcept {
    if (pad >= kPadCount || frame >= max_frames_)
        return false;
    const std::uint32_t logicalBlock = frame / kSampleBlockFrames;
    if (!ensurePadBlock(pad, logicalBlock))
        return false;
    const std::uint32_t mapped =
        pad_blocks_[static_cast<std::size_t>(pad) * blocks_per_pad_ + logicalBlock];
    const std::size_t offset =
        (static_cast<std::size_t>(mapped) * kSampleBlockFrames +
         frame % kSampleBlockFrames) * 2U;
    samples_[offset] = left;
    samples_[offset + 1U] = right;
    return true;
}

void SamplerEngine::beginRecord(std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
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
    setPadPlaybackSettings(pad, {});
    setPadMixerSettings(pad, {});
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
        float outL = settings_.monitorInput ? inL : 0.0f;
        float outR = settings_.monitorInput ? inR : 0.0f;
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
            outL += ((sampleAt(pad, i, 0U) * (1.0f - frac)) +
                     sampleAt(pad, j, 0U) * frac) * voiceGain * p.mixerGainLeft;
            outR += ((sampleAt(pad, i, 1U) * (1.0f - frac)) +
                     sampleAt(pad, j, 1U) * frac) * voiceGain * p.mixerGainRight;
            p.playPosition += step;
            if (p.playPosition >= n || !p.envelope.active()) {
                p.playing = false;
                p.publishedPlaying.store(false, std::memory_order_release);
            }
        }
        mixChopPreview(outL, outR);
        outputLeft[frame] = outL * settings_.gain;
        outputRight[frame] = outR * settings_.gain;
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

bool SamplerEngine::summarizePadRange(const std::uint32_t firstPad,
                                      const std::uint32_t padCount,
                                      const std::uint64_t start,
                                      const std::uint64_t end,
                                      sms::audio::WaveformSummary& result) const noexcept {
    if (padCount == 0U || padCount > 3U || firstPad >= kPadCount ||
        padCount > kPadCount - firstPad || start >= end)
        return false;
    std::array<std::uint64_t, 4> edges{};
    double rate = 0.0;
    for (std::uint32_t i = 0; i < padCount; ++i) {
        const auto& pad = pads_[firstPad + i];
        const auto frames = pad.publishedFrames.load(std::memory_order_acquire);
        edges[i + 1U] = edges[i] + frames;
        if (frames != 0U) {
            const double sourceRate = pad.sourceSampleRate.load(std::memory_order_acquire);
            if (rate != 0.0 && std::abs(rate - sourceRate) > 0.5)
                return false;
            rate = sourceRate;
        }
    }
    if (end > edges[padCount] || end - start > UINT32_MAX)
        return false;
    result = {};
    result.pad = firstPad;
    result.frames = static_cast<std::uint32_t>(end - start);
    result.sampleRate = rate > 1.0 ? rate : sample_rate_;
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const auto first = start + (end - start) * bin / sms::audio::kWaveformBins;
        const auto last = start + (end - start) * (bin + 1U) / sms::audio::kWaveformBins;
        float low = 1.0f;
        float high = -1.0f;
        for (auto frame = first; frame < std::max(first + 1U, last) && frame < end; ++frame) {
            std::uint32_t localPad = 0U;
            while (localPad + 1U < padCount && frame >= edges[localPad + 1U])
                ++localPad;
            const auto localFrame = static_cast<std::uint32_t>(frame - edges[localPad]);
            const float left = sampleAt(firstPad + localPad, localFrame, 0U);
            const float right = sampleAt(firstPad + localPad, localFrame, 1U);
            low = std::min({low, left, right});
            high = std::max({high, left, right});
        }
        result.minimum[bin] = low <= high ? low : 0.0f;
        result.maximum[bin] = low <= high ? high : 0.0f;
    }
    return true;
}

bool SamplerEngine::exportPad(std::uint32_t pad, PadData& destination) const {
    if (pad >= kPadCount) return false;
    const auto& p = pads_[pad];
    const auto n = p.publishedFrames.load(std::memory_order_acquire);
    destination.sampleRate = p.sourceSampleRate.load(std::memory_order_acquire);
    destination.frames = n;
    destination.peak = p.publishedPeak.load(std::memory_order_relaxed);
    destination.rms = p.publishedRms.load(std::memory_order_relaxed);
    destination.stereo.resize(static_cast<std::size_t>(n) * 2U);
    for (std::uint32_t frame = 0; frame < n;) {
        const auto logicalBlock = frame / kSampleBlockFrames;
        const auto blockFrame = frame % kSampleBlockFrames;
        const auto count = std::min(n - frame, kSampleBlockFrames - blockFrame);
        const auto mapped = pad_blocks_[static_cast<std::size_t>(pad) *
                                        blocks_per_pad_ + logicalBlock];
        float* const output = destination.stereo.data() +
            static_cast<std::size_t>(frame) * 2U;
        if (mapped == kNoBlock) {
            std::fill_n(output, static_cast<std::size_t>(count) * 2U, 0.0f);
        } else {
            const float* const source = samples_.data() +
                (static_cast<std::size_t>(mapped) * kSampleBlockFrames + blockFrame) * 2U;
            std::memcpy(output, source, static_cast<std::size_t>(count) * 2U * sizeof(float));
        }
        frame += count;
    }
    return true;
}

bool SamplerEngine::importPad(std::uint32_t pad, const PadData& source,
                              const bool resetEditorSettings) {
    if (pad >= kPadCount || !std::isfinite(source.sampleRate) || source.sampleRate <= 1.0 ||
        source.frames == 0 || !std::isfinite(source.peak) || source.peak < 0.0f ||
        !std::isfinite(source.rms) || source.rms < 0.0f ||
        source.stereo.size() < static_cast<std::size_t>(source.frames) * 2U ||
        !std::all_of(source.stereo.begin(),
                     source.stereo.begin() + static_cast<std::size_t>(source.frames) * 2U,
                     [](const float sample) noexcept { return std::isfinite(sample); }))
        return false;
    std::uint32_t storedFrames = source.frames;
    double storedRate = source.sampleRate;
    if (source.frames > max_frames_) {
        const auto convertedFrames = static_cast<std::uint64_t>(std::llround(
            static_cast<double>(source.frames) * sample_rate_ / source.sampleRate));
        if (convertedFrames == 0U || convertedFrames > max_frames_) return false;
        storedFrames = static_cast<std::uint32_t>(convertedFrames);
        storedRate = sample_rate_;
    }

    auto& p = pads_[pad];
    const std::uint32_t requiredBlocks =
        (storedFrames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
    if (requiredBlocks > free_block_count_ + p.allocatedBlocks)
        return false;
    stopChopPreview();
    if (activePad_ == static_cast<std::int32_t>(pad)) {
        activePad_ = -1;
        nextPad_ = pad;
        sessionComplete_ = false;
    }
    p.playing = p.recording = p.held = false;
    p.voiceOrder = 0;
    p.envelope.reset();
    p.publishedPlaying.store(false, std::memory_order_release);
    p.publishedRecording.store(false, std::memory_order_release);
    releasePadBlocks(pad);
    if (source.frames <= max_frames_) {
        for (std::uint32_t frame = 0; frame < storedFrames;) {
            const auto logicalBlock = frame / kSampleBlockFrames;
            const auto count = std::min(storedFrames - frame, kSampleBlockFrames);
            if (!ensurePadBlock(pad, logicalBlock))
                return false;
            const auto mapped = pad_blocks_[static_cast<std::size_t>(pad) *
                                            blocks_per_pad_ + logicalBlock];
            float* const output = samples_.data() +
                static_cast<std::size_t>(mapped) * kSampleBlockFrames * 2U;
            std::memcpy(output, source.stereo.data() + static_cast<std::size_t>(frame) * 2U,
                        static_cast<std::size_t>(count) * 2U * sizeof(float));
            frame += count;
        }
    } else {
        const double step = source.sampleRate / sample_rate_;
        for (std::uint32_t frame = 0; frame < storedFrames; ++frame) {
            const double position = std::min(static_cast<double>(source.frames - 1U), frame * step);
            const auto first = static_cast<std::uint32_t>(position);
            const auto second = std::min(first + 1U, source.frames - 1U);
            const auto fraction = static_cast<float>(position - first);
            const auto firstOffset = static_cast<std::size_t>(first) * 2U;
            const auto secondOffset = static_cast<std::size_t>(second) * 2U;
            const float left = source.stereo[firstOffset] +
                (source.stereo[secondOffset] - source.stereo[firstOffset]) * fraction;
            const float right = source.stereo[firstOffset + 1U] +
                (source.stereo[secondOffset + 1U] - source.stereo[firstOffset + 1U]) * fraction;
            static_cast<void>(storeSample(pad, frame, left, right));
        }
    }
    p.sourceSampleRate = storedRate;
    p.recordedFrames = p.recordPosition = storedFrames;
    p.publishedPeak.store(source.peak, std::memory_order_relaxed);
    p.publishedRms.store(source.rms, std::memory_order_relaxed);
    p.publishedFrames.store(storedFrames, std::memory_order_release);
    p.occupied.store(true, std::memory_order_release);
    if (resetEditorSettings) {
        setPadPlaybackSettings(pad, {});
        setPadMixerSettings(pad, {});
    }
    p.generation.fetch_add(1, std::memory_order_release);
    return true;
}

bool SamplerEngine::rechopPads(const std::uint32_t firstPad,
                               const std::uint32_t padCount,
                               const std::span<const std::int64_t> boundaryOffsets) {
    if (padCount < 2U || padCount > kPadsPerBank || firstPad >= kPadCount ||
        padCount > kPadCount - firstPad || boundaryOffsets.size() != padCount - 1U ||
        settings_.armed)
        return false;

    std::array<std::uint32_t, kPadsPerBank> originalFrames{};
    std::array<std::uint32_t, kPadsPerBank> newFrames{};
    std::array<sms::dsp::SamplePlaybackSettings, kPadsPerBank> originalSettings{};
    std::array<std::uint64_t, kPadsPerBank + 1U> originalBoundaries{};
    std::array<std::uint64_t, kPadsPerBank + 1U> newBoundaries{};
    double sourceRate = 0.0;
    bool hasSourceAudio = false;
    std::uint32_t releasableBlocks = 0U;
    for (std::uint32_t index = 0; index < padCount; ++index) {
        const auto& pad = pads_[firstPad + index];
        const auto frames = pad.publishedFrames.load(std::memory_order_acquire);
        const double rate = pad.sourceSampleRate.load(std::memory_order_acquire);
        const bool occupied = pad.occupied.load(std::memory_order_acquire);
        if (occupied != (frames != 0U))
            return false;
        if (occupied) {
            if (!std::isfinite(rate) || rate <= 1.0 ||
                (hasSourceAudio && std::abs(rate - sourceRate) > 0.5))
                return false;
            if (!hasSourceAudio)
                sourceRate = rate;
            hasSourceAudio = true;
        }
        originalFrames[index] = frames;
        originalSettings[index] = padPlaybackSettings(firstPad + index);
        originalBoundaries[index + 1U] = originalBoundaries[index] + frames;
        releasableBlocks += pad.allocatedBlocks;
    }
    if (!hasSourceAudio)
        return false;

    for (std::uint32_t boundary = 0; boundary + 1U < padCount; ++boundary) {
        const auto original = static_cast<std::int64_t>(originalBoundaries[boundary + 1U]);
        const auto moved = original + boundaryOffsets[boundary];
        if (moved < static_cast<std::int64_t>(newBoundaries[boundary]) || moved < 0)
            return false;
        newBoundaries[boundary + 1U] = static_cast<std::uint64_t>(moved);
    }
    newBoundaries[padCount] = originalBoundaries[padCount];

    std::uint32_t requiredBlocks = 0U;
    for (std::uint32_t index = 0; index < padCount; ++index) {
        if (newBoundaries[index + 1U] < newBoundaries[index])
            return false;
        const auto length = newBoundaries[index + 1U] - newBoundaries[index];
        if (length > max_frames_)
            return false;
        newFrames[index] = static_cast<std::uint32_t>(length);
        requiredBlocks += (newFrames[index] + kSampleBlockFrames - 1U) / kSampleBlockFrames;
    }
    if (requiredBlocks > free_block_count_ + releasableBlocks)
        return false;

    std::vector<float> source;
    source.resize(static_cast<std::size_t>(originalBoundaries[padCount]) * 2U);
    std::size_t destination = 0U;
    for (std::uint32_t index = 0; index < padCount; ++index) {
        const auto pad = firstPad + index;
        for (std::uint32_t frame = 0; frame < originalFrames[index]; ++frame) {
            source[destination++] = sampleAt(pad, frame, 0U);
            source[destination++] = sampleAt(pad, frame, 1U);
        }
    }

    stopChopPreview();
    for (std::uint32_t index = 0; index < padCount; ++index) {
        auto& pad = pads_[firstPad + index];
        pad.playing = pad.recording = pad.held = false;
        pad.voiceOrder = 0U;
        pad.envelope.reset();
        pad.publishedPlaying.store(false, std::memory_order_release);
        pad.publishedRecording.store(false, std::memory_order_release);
        releasePadBlocks(firstPad + index);
    }

    for (std::uint32_t index = 0; index < padCount; ++index) {
        const auto padIndex = firstPad + index;
        auto& pad = pads_[padIndex];
        float peak = 0.0f;
        double sumSquares = 0.0;
        const auto sourceStart = newBoundaries[index];
        for (std::uint32_t frame = 0; frame < newFrames[index]; ++frame) {
            const auto sourceOffset = static_cast<std::size_t>(sourceStart + frame) * 2U;
            const float left = source[sourceOffset];
            const float right = source[sourceOffset + 1U];
            if (!storeSample(padIndex, frame, left, right))
                return false;
            peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
            sumSquares += static_cast<double>(left) * left +
                          static_cast<double>(right) * right;
        }
        pad.sourceSampleRate.store(sourceRate, std::memory_order_release);
        pad.recordedFrames = pad.recordPosition = newFrames[index];
        pad.recordPeak = peak;
        pad.recordSumSquares = sumSquares;
        pad.publishedPeak.store(peak, std::memory_order_relaxed);
        pad.publishedRms.store(newFrames[index] == 0U ? 0.0f :
            static_cast<float>(std::sqrt(
                sumSquares / (2.0 * static_cast<double>(newFrames[index])))),
            std::memory_order_relaxed);
        pad.publishedFrames.store(newFrames[index], std::memory_order_release);
        pad.occupied.store(newFrames[index] != 0U, std::memory_order_release);
        const bool changed = (index > 0U && boundaryOffsets[index - 1U] != 0) ||
                             (index + 1U < padCount && boundaryOffsets[index] != 0);
        setPadPlaybackSettings(padIndex, newFrames[index] == 0U || changed
            ? sms::dsp::SamplePlaybackSettings{} : originalSettings[index]);
        if (newFrames[index] == 0U)
            setPadMixerSettings(padIndex, {});
        pad.generation.fetch_add(1U, std::memory_order_release);
    }
    return true;
}

void SamplerEngine::movePadContents(const std::uint32_t source,
                                    const std::uint32_t destination) noexcept {
    auto& from = pads_[source];
    auto& to = pads_[destination];
    const auto playback = padPlaybackSettings(source);
    const auto mixer = padMixerSettings(source);

    hardStopVoice(source);
    hardStopVoice(destination);
    to.recording = false;
    from.recording = false;
    to.publishedRecording.store(false, std::memory_order_release);
    from.publishedRecording.store(false, std::memory_order_release);
    for (std::uint32_t block = 0; block < blocks_per_pad_; ++block) {
        std::swap(pad_blocks_[static_cast<std::size_t>(source) * blocks_per_pad_ + block],
                  pad_blocks_[static_cast<std::size_t>(destination) * blocks_per_pad_ + block]);
    }

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
    setPadPlaybackSettings(destination, playback);
    setPadMixerSettings(destination, mixer);

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
    setPadPlaybackSettings(source, {});
    setPadMixerSettings(source, {});
    from.generation.fetch_add(1U, std::memory_order_release);
    to.generation.fetch_add(1U, std::memory_order_release);
}

bool SamplerEngine::collapsePadGap(const std::uint32_t firstPad,
                                   const std::uint32_t padCount,
                                   const std::uint32_t targetPad) noexcept {
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
}

void SamplerEngine::storeSplitHalf(const std::uint32_t pad,
                                   const std::vector<float>& source,
                                   const std::uint32_t sourceStart,
                                   const std::uint32_t frames,
                                   const double sourceRate) noexcept {
    auto& destination = pads_[pad];
    float peak = 0.0f;
    double sumSquares = 0.0;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto sourceOffset = static_cast<std::size_t>(sourceStart + frame) * 2U;
        const float left = source[sourceOffset];
        const float right = source[sourceOffset + 1U];
        static_cast<void>(storeSample(pad, frame, left, right));
        peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
        sumSquares += static_cast<double>(left) * left +
                      static_cast<double>(right) * right;
    }
    destination.sourceSampleRate.store(sourceRate, std::memory_order_release);
    destination.recording = false;
    destination.publishedRecording.store(false, std::memory_order_release);
    destination.recordedFrames = destination.recordPosition = frames;
    destination.recordPeak = peak;
    destination.recordSumSquares = sumSquares;
    destination.publishedPeak.store(peak, std::memory_order_relaxed);
    destination.publishedRms.store(static_cast<float>(std::sqrt(
        sumSquares / (2.0 * static_cast<double>(frames)))), std::memory_order_relaxed);
    destination.publishedFrames.store(frames, std::memory_order_release);
    destination.occupied.store(true, std::memory_order_release);
    destination.generation.fetch_add(1U, std::memory_order_release);
}

bool SamplerEngine::splitPadAndShiftRight(
    const std::uint32_t firstPad, const std::uint32_t emptyPad,
    const std::uint32_t splitFrame,
    const std::span<const std::uint64_t> expectedGenerations) {
    const std::uint32_t visibleFirst = static_cast<std::uint32_t>(settings_.activeBank) *
        bankStride(settings_.padsPerBank, settings_.midiBankMode);
    const std::uint32_t visibleEnd = visibleFirst + settings_.padsPerBank;
    if (settings_.armed || activePad_ >= 0 || firstPad < visibleFirst ||
        firstPad >= visibleEnd || emptyPad >= visibleEnd ||
        firstPad >= kPadCount || emptyPad <= firstPad ||
        emptyPad >= kPadCount || emptyPad - firstPad >= kPadsPerBank ||
        expectedGenerations.size() != emptyPad - firstPad + 1U)
        return false;

    for (std::uint32_t pad = firstPad; pad <= emptyPad; ++pad) {
        const auto& current = pads_[pad];
        if (current.generation.load(std::memory_order_acquire) !=
            expectedGenerations[pad - firstPad])
            return false;
        const bool shouldBeOccupied = pad < emptyPad;
        if (current.occupied.load(std::memory_order_acquire) != shouldBeOccupied)
            return false;
    }

    auto& sourcePad = pads_[firstPad];
    const std::uint32_t sourceFrames =
        sourcePad.publishedFrames.load(std::memory_order_acquire);
    if (splitFrame == 0U || splitFrame >= sourceFrames)
        return false;
    const std::uint32_t suffixFrames = sourceFrames - splitFrame;
    const std::uint32_t requiredBlocks =
        (splitFrame + kSampleBlockFrames - 1U) / kSampleBlockFrames +
        (suffixFrames + kSampleBlockFrames - 1U) / kSampleBlockFrames;
    if (requiredBlocks > free_block_count_ + sourcePad.allocatedBlocks)
        return false;

    std::vector<float> source(static_cast<std::size_t>(sourceFrames) * 2U);
    for (std::uint32_t frame = 0; frame < sourceFrames; ++frame) {
        const auto offset = static_cast<std::size_t>(frame) * 2U;
        source[offset] = sampleAt(firstPad, frame, 0U);
        source[offset + 1U] = sampleAt(firstPad, frame, 1U);
    }
    const double sourceRate =
        sourcePad.sourceSampleRate.load(std::memory_order_acquire);
    const auto mixer = padMixerSettings(firstPad);

    stopChopPreview();
    for (std::uint32_t pad = emptyPad; pad > firstPad + 1U; --pad)
        movePadContents(pad - 1U, pad);

    hardStopVoice(firstPad);
    hardStopVoice(firstPad + 1U);
    releasePadBlocks(firstPad);
    releasePadBlocks(firstPad + 1U);
    sourcePad.publishedFrames.store(0U, std::memory_order_release);
    sourcePad.occupied.store(false, std::memory_order_release);
    storeSplitHalf(firstPad, source, 0U, splitFrame, sourceRate);
    storeSplitHalf(firstPad + 1U, source, splitFrame, suffixFrames, sourceRate);
    setPadPlaybackSettings(firstPad, {});
    setPadPlaybackSettings(firstPad + 1U, {});
    setPadMixerSettings(firstPad, mixer);
    setPadMixerSettings(firstPad + 1U, mixer);
    lastCommittedPad_ = -1;
    playbackPositionOutput_ = 0.0f;
    playbackPositionHoldFrames_ = 0U;
    playbackPositionWasActive_ = false;
    return true;
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

sms::dsp::SamplePlaybackSettings SamplerEngine::padPlaybackSettings(const std::uint32_t pad) const noexcept {
    if (pad >= kPadCount) return {};
    const auto& p = pads_[pad];
    return sms::dsp::sanitize({
        p.regionStart.load(std::memory_order_acquire),
        p.regionEnd.load(std::memory_order_acquire),
        p.attackSeconds.load(std::memory_order_acquire),
        p.decaySeconds.load(std::memory_order_acquire),
        p.sustainLevel.load(std::memory_order_acquire),
        p.releaseSeconds.load(std::memory_order_acquire),
    });
}

void SamplerEngine::setPadPlaybackSettings(
    const std::uint32_t pad, const sms::dsp::SamplePlaybackSettings& requested) noexcept {
    if (pad >= kPadCount) return;
    const auto settings = sms::dsp::sanitize(requested);
    auto& p = pads_[pad];
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
    });
}

void SamplerEngine::setPadMixerSettings(
    const std::uint32_t pad, const sms::dsp::SampleMixerSettings& requested) noexcept
{
    if (pad >= kPadCount) return;
    const auto settings = sms::dsp::sanitize(requested);
    auto& p = pads_[pad];
    const bool changed =
        p.mixerGainDecibels.load(std::memory_order_acquire) != settings.gainDecibels ||
        p.mixerPan.load(std::memory_order_acquire) != settings.pan ||
        p.mixerTuneSemitones.load(std::memory_order_acquire) != settings.tuneSemitones;
    p.mixerGainDecibels.store(settings.gainDecibels, std::memory_order_release);
    p.mixerPan.store(settings.pan, std::memory_order_release);
    p.mixerTuneSemitones.store(settings.tuneSemitones, std::memory_order_release);
    if (changed)
        p.generation.fetch_add(1U, std::memory_order_release);
}

void SamplerEngine::clearPad(std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    stopChopPreview();
    auto& p = pads_[pad];
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
    setPadPlaybackSettings(pad, {});
    setPadMixerSettings(pad, {});
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
