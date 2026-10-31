#include "PlaybackVoice.hpp"

#include <algorithm>
#include <cmath>

namespace midichopper {

void PlaybackVoice::start(const Parameters& parameters, const std::uint8_t velocity,
                          const Mode mode, const std::int16_t triggerNote,
                          const std::uint64_t order) noexcept {
    const auto& global = parameters.global;
    const auto sampleRate = parameters.sampleRate;
    mode_ = mode;
    triggerNote_ = triggerNote;
    const auto frameCount = parameters.frames;
    const auto& mixer = parameters.mixer;
    const bool globalMixer = mixer.globalMixer >= 0.5f;
    auto playback = parameters.playback;
    if (globalMixer && global.reverse) playback = sms::dsp::mirroredRegion(playback);
    const auto startFrame = std::min(frameCount - 1U, static_cast<std::uint32_t>(
        std::floor(static_cast<double>(playback.start) * frameCount)));
    const auto endFrame = std::clamp(static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(playback.end) * frameCount)), startFrame + 1U, frameCount);
    startFrame_ = startFrame;
    endFrame_ = endFrame;
    releaseFromEnd_ = false;
    noteReleased_ = false;
    velocityGain_ = static_cast<float>(velocity) / 127.0f;
    const double sourceRate = parameters.sourceSampleRate;
    reverse_ = (mixer.reverse >= 0.5f) != (globalMixer && global.reverse);
    position_ = static_cast<double>(startFrame);
    const float gain = sms::dsp::sampleGain(mixer);
    const float pan = std::clamp(mixer.pan + (globalMixer ? global.pan : 0.0f), -1.0f, 1.0f);
    const double tuneRatio = std::exp2(
        static_cast<double>(mixer.tuneSemitones + (globalMixer ? global.tuneSemitones : 0.0f)) / 12.0);
    gainLeft_ = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    gainRight_ = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    dirty_ = mixer.dirty >= 0.5f || (globalMixer && global.dirty >= 0.5f);
    padFilters_.reset();
    globalFilters_.reset();
    padFilters_.configure(mixer, sampleRate);
    globalFilters_.configure({0.0f, 0.0f, 0.0f,
        globalMixer ? global.lowpass : 0.0f,
        globalMixer ? global.highpass : 0.0f, global.filterSlope}, sampleRate);
    step_ = (sourceRate / sampleRate) * tuneRatio;
    const float regionSeconds = static_cast<float>(endFrame - startFrame) /
        static_cast<float>((sourceRate > 1.0 ? sourceRate : sampleRate) *
                           tuneRatio);
    playback.releaseSeconds = std::min(playback.releaseSeconds, regionSeconds * 0.5f);
    envelope_.configure(sampleRate, playback);
    envelope_.noteOn();
    playing_ = true;
    order_ = order;
}

bool PlaybackVoice::refresh(const Parameters& parameters) noexcept {
    const auto& global = parameters.global;
    const auto sampleRate = parameters.sampleRate;
    const auto frames = parameters.frames;
    const auto& mixer = parameters.mixer;
    const bool globalMixer = mixer.globalMixer >= 0.5f;
    auto playback = parameters.playback;
    if (globalMixer && global.reverse) playback = sms::dsp::mirroredRegion(playback);
    const bool looping = mode_ != Mode::OneShot;
    if (looping) {
        // Loop turns and seam interpolation must use the latest Start, too.
        // One-shot voices keep their trigger-time Start while End stays live.
        startFrame_ = std::min(frames - 1U, static_cast<std::uint32_t>(
            std::floor(static_cast<double>(playback.start) * frames)));
    }
    endFrame_ = std::clamp(static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(playback.end) * frames)),
        std::min(startFrame_ + 1U, frames), frames);
    if (position_ >= static_cast<double>(endFrame_) ||
        (looping && position_ < static_cast<double>(startFrame_))) {
        if (!looping) {
            return false;
        }
        const double length = static_cast<double>(endFrame_ - startFrame_);
        double phase = std::fmod(position_ - startFrame_, length);
        if (phase < 0.0) phase += length;
        position_ = startFrame_ + phase;
    }

    const float gain = sms::dsp::sampleGain(mixer);
    const float pan = std::clamp(mixer.pan + (globalMixer ? global.pan : 0.0f), -1.0f, 1.0f);
    const double tuneRatio = std::exp2(
        static_cast<double>(mixer.tuneSemitones + (globalMixer ? global.tuneSemitones : 0.0f)) / 12.0);
    gainLeft_ = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    gainRight_ = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    dirty_ = mixer.dirty >= 0.5f || (globalMixer && global.dirty >= 0.5f);
    padFilters_.configure(mixer, sampleRate);
    globalFilters_.configure({0.0f, 0.0f, 0.0f,
        globalMixer ? global.lowpass : 0.0f,
        globalMixer ? global.highpass : 0.0f, global.filterSlope}, sampleRate);
    const double sourceRate = parameters.sourceSampleRate;
    step_ = (sourceRate / sampleRate) * tuneRatio;
    const float regionSeconds = static_cast<float>(endFrame_ - startFrame_) /
        static_cast<float>((sourceRate > 1.0 ? sourceRate : sampleRate) *
                           tuneRatio);
    if (!noteReleased_)
        playback.releaseSeconds = std::min(playback.releaseSeconds, regionSeconds * 0.5f);
    envelope_.updateSettings(playback);

    const double remainingOutputFrames =
        (static_cast<double>(endFrame_) - position_) / step_;
    if (releaseFromEnd_ &&
        remainingOutputFrames > static_cast<double>(envelope_.releaseFrames())) {
        envelope_.resumeAfterAutomaticRelease();
        releaseFromEnd_ = false;
    }
    return true;
}

bool PlaybackVoice::release(const sms::dsp::SamplePlaybackSettings& playback) noexcept {
    if (noteReleased_)
        return false;
    releaseFromEnd_ = false;
    noteReleased_ = true;
    // Hold mode's note-off release is independent of its per-pass end fade.
    envelope_.updateSettings(playback);
    envelope_.noteOff(true);
    if (!envelope_.active()) {
        playing_ = false;
        return true;
    }
    return false;
}

void PlaybackVoice::reset() noexcept {
    playing_ = false;
    order_ = 0;
    envelope_.reset();
}

void PlaybackVoice::hardStop() noexcept {
    playing_ = false;
    triggerNote_ = -1;
    order_ = 0;
    releaseFromEnd_ = false;
    envelope_.reset();
}

void PlaybackVoice::redirect(const std::uint32_t frames, const bool reverse) noexcept {
    if (!playing_ || reverse_ == reverse)
        return;
    if (frames == 0U) return;
    const auto start = frames - endFrame_;
    const auto end = frames - startFrame_;
    double position = static_cast<double>(frames) - 1.0 - position_;
    if (mode_ == Mode::OneShot) {
        position = std::clamp(position, static_cast<double>(start),
                             static_cast<double>(end - 1U));
    } else {
        const double length = static_cast<double>(end - start);
        double phase = std::fmod(position - start, length);
        if (phase < 0.0) phase += length;
        position = start + phase;
    }
    startFrame_ = start;
    endFrame_ = end;
    position_ = position;
    reverse_ = reverse;
    // Continue the current envelope, including an existing release. A turn is
    // neither a new note nor a reason to resume a completed automatic fade.
    releaseFromEnd_ = false;
}

bool PlaybackVoice::render(const SampleStoragePool& pool,
    const SampleStoragePool::Storage* storage, const std::uint32_t frameCount,
    const double sourceSampleRate, float& outL, float& outR) noexcept {
    const auto n = std::min(frameCount, endFrame_);
    const bool looping = mode_ != Mode::OneShot;
    if (n == 0 || (!envelope_.active() && (!looping || noteReleased_))) {
        playing_ = false;
        return false;
    }
    const auto pos = position_;
    // The cursor, cut points and envelope use left-to-right playback
    // coordinates. Reverse virtually flips the full source before trimming.
    const auto i = static_cast<std::uint32_t>(pos);
    if (i >= n) { playing_ = false; return false; }
    const auto j = (i + 1U < n) ? i + 1U : looping ? startFrame_ : i;
    const auto frac = static_cast<float>(pos - static_cast<double>(i));
    const auto sourceFrame = [&](const std::uint32_t index) noexcept {
        return reverse_ ? frameCount - 1U - index : index;
    };
    const double step = step_;
    const double remainingOutputFrames = (static_cast<double>(n) - pos) / step;
    if (!noteReleased_ && !envelope_.releasing() && envelope_.releaseFrames() > 0U &&
        remainingOutputFrames <= static_cast<double>(envelope_.releaseFrames())) {
        envelope_.noteOff();
        releaseFromEnd_ = true;
    }
    const float envelopeGain = envelope_.next();
    const float voiceGain = velocityGain_ * envelopeGain;
    float sampleL, sampleR;
    if (dirty_) {
        const auto sourceRate = sourceSampleRate;
        const auto sourceI = sourceFrame(sms::dsp::dirtySourceFrame(i, sourceRate));
        const auto sourceJ = sourceFrame(sms::dsp::dirtySourceFrame(j, sourceRate));
        const float first = sms::dsp::dirtyMono(
            pool.sample(storage, sourceI, 0U), pool.sample(storage, sourceI, 1U));
        const float second = sms::dsp::dirtyMono(
            pool.sample(storage, sourceJ, 0U), pool.sample(storage, sourceJ, 1U));
        sampleL = sampleR = first * (1.0f - frac) + second * frac;
    } else {
        sampleL = pool.sample(storage, sourceFrame(i), 0U) * (1.0f - frac) +
                  pool.sample(storage, sourceFrame(j), 0U) * frac;
        sampleR = pool.sample(storage, sourceFrame(i), 1U) * (1.0f - frac) +
                  pool.sample(storage, sourceFrame(j), 1U) * frac;
    }
    padFilters_.process(sampleL, sampleR);
    globalFilters_.process(sampleL, sampleR);
    outL += sampleL * voiceGain * gainLeft_;
    outR += sampleR * voiceGain * gainRight_;
    position_ += step;
    const bool reachedEnd = position_ >= n;
    if (looping && reachedEnd) {
        position_ = startFrame_ + std::fmod(
            position_ - startFrame_,
            static_cast<double>(n - startFrame_));
        if (!noteReleased_) {
            envelope_.noteOn();
            releaseFromEnd_ = false;
        }
    }
    if ((!looping && reachedEnd) || (!envelope_.active() && (!looping || noteReleased_))) {
        playing_ = false;
    }
    return playing_;
}

} // namespace midichopper
