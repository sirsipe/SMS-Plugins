#include "SamplerEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace midichopper {
namespace {
constexpr float kSilence = 0.0f;
constexpr std::uint32_t clampPad(std::uint32_t p) noexcept { return p < kPadCount ? p : kPadCount; }
}

SamplerEngine::SamplerEngine(double sampleRate, double maxRecordSeconds)
    : sample_rate_(sampleRate > 1.0 ? sampleRate : 48000.0),
      max_record_seconds_(maxRecordSeconds > 0.0 ? maxRecordSeconds : 1.0),
      max_frames_(static_cast<std::uint32_t>(std::max(1.0, std::ceil(sample_rate_ * max_record_seconds_)))),
      samples_(static_cast<std::size_t>(kPadCount) * max_frames_ * 2U, 0.0f),
      ringCapacityFrames_(static_cast<std::uint32_t>(std::max(1.0, std::ceil(sample_rate_ * 0.1)))),
      ring_(static_cast<std::size_t>(ringCapacityFrames_) * 2U, 0.0f) {
    for (auto& p : pads_) p.sourceSampleRate = sample_rate_;
    setSettings(settings_);
}

void SamplerEngine::setSampleRate(double sampleRate) {
    if (sampleRate <= 1.0 || sampleRate == sample_rate_) return;

    // A host may change rate while keeping the same plug-in instance. Preserve
    // completed pads and only grow the per-pad storage when the new rate needs
    // more room; shrinking it could truncate restored material.
    if (activePad_ >= 0) {
        finishRecord(static_cast<std::uint32_t>(activePad_));
        activePad_ = -1;
    }
    const auto requiredFrames = static_cast<std::uint32_t>(
        std::max(1.0, std::ceil(sampleRate * max_record_seconds_)));
    if (requiredFrames > max_frames_) {
        std::vector<float> expanded(static_cast<std::size_t>(kPadCount) * requiredFrames * 2U, 0.0f);
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            const auto frames = pads_[pad].publishedFrames.load(std::memory_order_acquire);
            const auto oldBase = static_cast<std::size_t>(pad) * max_frames_ * 2U;
            const auto newBase = static_cast<std::size_t>(pad) * requiredFrames * 2U;
            std::copy_n(samples_.data() + oldBase, static_cast<std::size_t>(frames) * 2U,
                        expanded.data() + newBase);
        }
        samples_.swap(expanded);
        max_frames_ = requiredFrames;
    }

    sample_rate_ = sampleRate;
    ringCapacityFrames_ = static_cast<std::uint32_t>(std::max(1.0, std::ceil(sample_rate_ * 0.1)));
    ring_.assign(static_cast<std::size_t>(ringCapacityFrames_) * 2U, 0.0f);
    ringWritePosition_ = ringCount_ = 0;
    nextPad_ = settings_.startPad;
    sessionComplete_ = false;
    previousArmed_ = false;
    for (auto& p : pads_) {
        p.recording = p.playing = false;
        p.publishedRecording.store(false, std::memory_order_release);
        p.publishedPlaying.store(false, std::memory_order_release);
    }
    setSettings(settings_);
}

void SamplerEngine::reset() noexcept {
    activePad_ = -1;
    lastCommittedPad_ = -1;
    nextPad_ = settings_.startPad < kPadCount ? settings_.startPad : 0;
    sessionComplete_ = false;
    previousArmed_ = settings_.armed;
    ringWritePosition_ = ringCount_ = 0;
    for (auto& p : pads_) {
        p.recording = p.held = p.playing = false;
        p.publishedPlaying.store(false, std::memory_order_release);
        p.recordedFrames = p.recordPosition = 0;
        p.publishedFrames.store(0, std::memory_order_release);
        p.occupied.store(false, std::memory_order_release);
        p.publishedRecording.store(false, std::memory_order_release);
    }
}

void SamplerEngine::setSettings(const EngineSettings& s) noexcept {
    settings_ = s;
    if (settings_.startPad >= kPadCount) settings_.startPad = 0;
    if (settings_.preRollMilliseconds < 0.0f) settings_.preRollMilliseconds = 0.0f;
    if (settings_.preRollMilliseconds > 100.0f) settings_.preRollMilliseconds = 100.0f;
    preRollFrames_ = static_cast<std::uint32_t>(std::clamp(
        std::llround(static_cast<double>(settings_.preRollMilliseconds) * sample_rate_ / 1000.0),
        0LL, static_cast<long long>(ringCapacityFrames_)));
}

std::uint32_t SamplerEngine::noteToPad(std::uint8_t note) const noexcept {
    if (note < settings_.baseNote || note >= static_cast<std::uint16_t>(settings_.baseNote) + kPadCount) return kPadCount;
    return static_cast<std::uint32_t>(note - settings_.baseNote);
}

void SamplerEngine::beginRecord(std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    p.playing = false;
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
    // The ring contains exactly the audio immediately preceding the trigger.
    const auto copyCount = std::min({preRollFrames_, ringCount_, max_frames_});
    const auto start = (ringWritePosition_ + ringCapacityFrames_ - copyCount) % ringCapacityFrames_;
    const auto capacity = max_frames_;
    for (std::uint32_t i = 0; i < copyCount && i < capacity; ++i) {
        const auto ri = (start + i) % ringCapacityFrames_;
        const auto di = static_cast<std::size_t>(i) * 2U;
        const auto si = static_cast<std::size_t>(pad) * max_frames_ * 2U + di;
        samples_[si] = ring_[static_cast<std::size_t>(ri) * 2U];
        samples_[si + 1] = ring_[static_cast<std::size_t>(ri) * 2U + 1U];
        const auto l = samples_[si]; const auto r = samples_[si + 1];
        p.recordPeak = std::max(p.recordPeak, std::max(std::abs(l), std::abs(r)));
        p.recordSumSquares += static_cast<double>(l) * l + static_cast<double>(r) * r;
    }
    p.recordPosition = copyCount;
    p.recordedFrames = copyCount;
    activePad_ = static_cast<std::int32_t>(pad);
}

void SamplerEngine::finishRecord(std::uint32_t pad, std::uint32_t trimFrames) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    if (!p.recording) return;
    if (trimFrames > p.recordedFrames) trimFrames = p.recordedFrames;
    const auto base = static_cast<std::size_t>(pad) * max_frames_ * 2U;
    for (std::uint32_t frame = p.recordedFrames - trimFrames; frame < p.recordedFrames; ++frame) {
        const auto offset = base + static_cast<std::size_t>(frame) * 2U;
        const auto left = samples_[offset];
        const auto right = samples_[offset + 1U];
        p.recordSumSquares -= static_cast<double>(left) * left + static_cast<double>(right) * right;
    }
    p.recordedFrames -= trimFrames;
    p.recordPosition = p.recordedFrames;
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
    lastCommittedPad_ = static_cast<std::int32_t>(pad);
}

void SamplerEngine::writeRecordFrame(std::uint32_t, float left, float right) noexcept {
    if (activePad_ < 0 || activePad_ >= static_cast<std::int32_t>(kPadCount)) return;
    auto& p = pads_[static_cast<std::uint32_t>(activePad_)];
    if (!p.recording || p.recordPosition >= max_frames_) return;
    const auto offset = static_cast<std::size_t>(activePad_) * max_frames_ * 2U +
                        static_cast<std::size_t>(p.recordPosition) * 2U;
    samples_[offset] = left;
    samples_[offset + 1U] = right;
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
    const auto frameCount = p.publishedFrames.load(std::memory_order_acquire);
    auto playback = padPlaybackSettings(pad);
    const auto startFrame = std::min(frameCount - 1U, static_cast<std::uint32_t>(
        std::floor(static_cast<double>(playback.start) * frameCount)));
    const auto endFrame = std::clamp(static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(playback.end) * frameCount)), startFrame + 1U, frameCount);
    p.playPosition = static_cast<double>(startFrame);
    p.voiceEndFrame = endFrame;
    p.velocityGain = static_cast<float>(velocity) / 127.0f;
    const double sourceRate = p.sourceSampleRate.load(std::memory_order_relaxed);
    const float regionSeconds = static_cast<float>(endFrame - startFrame) /
                                static_cast<float>(sourceRate > 1.0 ? sourceRate : sample_rate_);
    playback.releaseSeconds = std::min(playback.releaseSeconds, regionSeconds * 0.5f);
    p.envelope.configure(sample_rate_, playback);
    p.envelope.noteOn();
    p.playing = true;
    p.publishedPlaying.store(true, std::memory_order_release);
}

void SamplerEngine::stopVoice(std::uint32_t pad) noexcept {
    if (pad < kPadCount) {
        auto& voice = pads_[pad];
        voice.envelope.noteOff();
        if (!voice.envelope.active()) {
            voice.playing = false;
            voice.publishedPlaying.store(false, std::memory_order_release);
        }
    }
}

void SamplerEngine::handleEvent(const MidiEvent& event) noexcept {
    const bool on = event.isNoteOn();
    if (!on && event.type != MidiEventType::NoteOff) return;
    if (settings_.armed) {
        if (on && settings_.captureMode == CaptureMode::FixedDuration && !sessionComplete_) {
            if (activePad_ >= 0) finishRecord(static_cast<std::uint32_t>(activePad_));
            if (nextPad_ < kPadCount) beginRecord(nextPad_++);
            else { activePad_ = -1; sessionComplete_ = true; }
        } else if (on && settings_.captureMode == CaptureMode::Sequential && activePad_ < 0 && !sessionComplete_) {
            if (nextPad_ < kPadCount) beginRecord(nextPad_++);
            else sessionComplete_ = true;
        } else if (on && settings_.captureMode == CaptureMode::Sequential && activePad_ >= 0) {
            const auto trim = std::min(preRollFrames_, pads_[static_cast<std::uint32_t>(activePad_)].recordedFrames);
            finishRecord(static_cast<std::uint32_t>(activePad_), trim);
            if (nextPad_ < kPadCount) beginRecord(nextPad_++);
            else { activePad_ = -1; sessionComplete_ = true; }
        }
        return; // note identity and note-off do not affect sequential capture
    }
    const auto pad = noteToPad(event.note);
    if (pad >= kPadCount) return;
    if (on) startVoice(pad, event.velocity);
    else if (settings_.playbackMode == PlaybackMode::Gated) stopVoice(pad);
}

void SamplerEngine::process(const float* inputLeft, const float* inputRight,
                            float* outputLeft, float* outputRight,
                            std::uint32_t frames, const MidiEvent* events,
                            std::uint32_t eventCount) noexcept {
    if (!outputLeft || !outputRight) return;
    if (settings_.armed != previousArmed_) {
        if (settings_.armed) {
            activePad_ = -1; sessionComplete_ = false;
            nextPad_ = settings_.startPad;
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
    std::uint32_t eventIndex = 0;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (events && eventIndex < eventCount && events[eventIndex].frameOffset <= frame) handleEvent(events[eventIndex++]);
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
            const auto base = static_cast<std::size_t>(pad) * max_frames_ * 2U;
            const auto a = base + static_cast<std::size_t>(i) * 2U;
            const auto b = base + static_cast<std::size_t>(j) * 2U;
            const double step = p.sourceSampleRate.load(std::memory_order_relaxed) / sample_rate_;
            const double remainingOutputFrames = (static_cast<double>(n) - pos) / step;
            if (!p.envelope.releasing() && p.envelope.releaseFrames() > 0U &&
                remainingOutputFrames <= static_cast<double>(p.envelope.releaseFrames()))
                p.envelope.noteOff();
            const float envelopeGain = p.envelope.next();
            const float voiceGain = p.velocityGain * envelopeGain;
            outL += ((samples_[a] * (1.0f - frac)) + samples_[b] * frac) * voiceGain;
            outR += ((samples_[a + 1U] * (1.0f - frac)) + samples_[b + 1U] * frac) * voiceGain;
            p.playPosition += step;
            if (p.playPosition >= n || !p.envelope.active()) {
                p.playing = false;
                p.publishedPlaying.store(false, std::memory_order_release);
            }
        }
        outputLeft[frame] = outL * settings_.gain;
        outputRight[frame] = outR * settings_.gain;
        if (ringCapacityFrames_) {
            const auto ri = static_cast<std::size_t>(ringWritePosition_) * 2U;
            ring_[ri] = inL; ring_[ri + 1U] = inR;
            ringWritePosition_ = (ringWritePosition_ + 1U) % ringCapacityFrames_;
            ringCount_ = std::min(ringCount_ + 1U, ringCapacityFrames_);
        }
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

bool SamplerEngine::exportPad(std::uint32_t pad, PadData& destination) const {
    if (pad >= kPadCount) return false;
    const auto& p = pads_[pad];
    const auto n = p.publishedFrames.load(std::memory_order_acquire);
    destination.sampleRate = p.sourceSampleRate.load(std::memory_order_acquire);
    destination.frames = n;
    destination.peak = p.publishedPeak.load(std::memory_order_relaxed);
    destination.rms = p.publishedRms.load(std::memory_order_relaxed);
    destination.stereo.resize(static_cast<std::size_t>(n) * 2U);
    const auto base = static_cast<std::size_t>(pad) * max_frames_ * 2U;
    std::copy_n(samples_.data() + base, destination.stereo.size(), destination.stereo.data());
    return true;
}

bool SamplerEngine::importPad(std::uint32_t pad, const PadData& source) {
    if (pad >= kPadCount || !std::isfinite(source.sampleRate) || source.sampleRate <= 1.0 ||
        source.frames == 0 || !std::isfinite(source.peak) || source.peak < 0.0f ||
        !std::isfinite(source.rms) || source.rms < 0.0f ||
        source.stereo.size() < static_cast<std::size_t>(source.frames) * 2U ||
        !std::all_of(source.stereo.begin(),
                     source.stereo.begin() + static_cast<std::size_t>(source.frames) * 2U,
                     [](const float sample) noexcept { return std::isfinite(sample); }))
        return false;
    auto& p = pads_[pad];
    if (activePad_ == static_cast<std::int32_t>(pad)) {
        activePad_ = -1;
        nextPad_ = pad;
        sessionComplete_ = false;
    }
    p.playing = p.recording = p.held = false;
    p.envelope.reset();
    p.publishedPlaying.store(false, std::memory_order_release);
    p.publishedRecording.store(false, std::memory_order_release);
    const auto base = static_cast<std::size_t>(pad) * max_frames_ * 2U;
    std::uint32_t storedFrames = source.frames;
    double storedRate = source.sampleRate;
    if (source.frames <= max_frames_) {
        std::copy_n(source.stereo.data(), static_cast<std::size_t>(source.frames) * 2U, samples_.data() + base);
    } else {
        const auto convertedFrames = static_cast<std::uint64_t>(std::llround(
            static_cast<double>(source.frames) * sample_rate_ / source.sampleRate));
        if (convertedFrames == 0U || convertedFrames > max_frames_) return false;
        storedFrames = static_cast<std::uint32_t>(convertedFrames);
        storedRate = sample_rate_;
        const double step = source.sampleRate / sample_rate_;
        for (std::uint32_t frame = 0; frame < storedFrames; ++frame) {
            const double position = std::min(static_cast<double>(source.frames - 1U), frame * step);
            const auto first = static_cast<std::uint32_t>(position);
            const auto second = std::min(first + 1U, source.frames - 1U);
            const auto fraction = static_cast<float>(position - first);
            for (std::uint32_t channel = 0; channel < 2U; ++channel) {
                const float a = source.stereo[static_cast<std::size_t>(first) * 2U + channel];
                const float b = source.stereo[static_cast<std::size_t>(second) * 2U + channel];
                samples_[base + static_cast<std::size_t>(frame) * 2U + channel] =
                    a + (b - a) * fraction;
            }
        }
    }
    p.sourceSampleRate = storedRate;
    p.recordedFrames = storedFrames;
    p.publishedPeak.store(source.peak, std::memory_order_relaxed);
    p.publishedRms.store(source.rms, std::memory_order_relaxed);
    p.publishedFrames.store(storedFrames, std::memory_order_release);
    p.occupied.store(true, std::memory_order_release);
    p.generation.fetch_add(1, std::memory_order_release);
    return true;
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
    p.regionStart.store(settings.start, std::memory_order_release);
    p.regionEnd.store(settings.end, std::memory_order_release);
    p.attackSeconds.store(settings.attackSeconds, std::memory_order_release);
    p.decaySeconds.store(settings.decaySeconds, std::memory_order_release);
    p.sustainLevel.store(settings.sustainLevel, std::memory_order_release);
    p.releaseSeconds.store(settings.releaseSeconds, std::memory_order_release);
}

void SamplerEngine::clearPad(std::uint32_t pad) noexcept {
    if (pad >= kPadCount) return;
    auto& p = pads_[pad];
    if (activePad_ == static_cast<std::int32_t>(pad)) {
        activePad_ = -1;
        nextPad_ = pad;
        sessionComplete_ = false;
    }
    p.recording = p.held = p.playing = false;
    p.envelope.reset();
    p.publishedPlaying.store(false, std::memory_order_release);
    p.publishedRecording.store(false, std::memory_order_release);
    p.publishedFrames.store(0, std::memory_order_release);
    p.occupied.store(false, std::memory_order_release);
    p.publishedPeak.store(0.0f, std::memory_order_relaxed);
    p.publishedRms.store(0.0f, std::memory_order_relaxed);
    setPadPlaybackSettings(pad, {});
    p.generation.fetch_add(1, std::memory_order_release);
}

void SamplerEngine::clearAllPads() noexcept {
    for (std::uint32_t i = 0; i < kPadCount; ++i) clearPad(i);
    activePad_ = -1; lastCommittedPad_ = -1; sessionComplete_ = false; nextPad_ = settings_.startPad;
}

void SamplerEngine::finalizeRecording() noexcept { finalizeRequested_.store(true, std::memory_order_release); }
void SamplerEngine::undoLastSlice() noexcept { undoRequested_.store(true, std::memory_order_release); }

} // namespace midichopper
