#pragma once

#include <algorithm>
#include <cmath>

namespace sms::dsp {

inline constexpr float kMinimumSampleGainDecibels = -60.0f;
inline constexpr float kMaximumSampleGainDecibels = 12.0f;
inline constexpr float kMinimumSamplePan = -1.0f;
inline constexpr float kMaximumSamplePan = 1.0f;
inline constexpr float kMinimumTuneSemitones = -24.0f;
inline constexpr float kMaximumTuneSemitones = 24.0f;
inline constexpr float kMinimumFilterAmount = 0.0f;
inline constexpr float kMaximumFilterAmount = 1.0f;

/** Per-sample level, balance, varispeed, and color settings. */
struct SampleMixerSettings {
    float gainDecibels = 0.0f;
    float pan = 0.0f;
    float tuneSemitones = 0.0f;
    float lowpass = 0.0f;
    float highpass = 0.0f;
    float filterSlope = 0.0f; // 0 = 6, 1 = 12, 2 = 24 dB/octave
    float dirty = 0.0f;
};

[[nodiscard]] inline SampleMixerSettings sanitize(SampleMixerSettings value) noexcept
{
    const auto finiteOr = [](const float candidate, const float fallback) noexcept {
        return std::isfinite(candidate) ? candidate : fallback;
    };
    value.gainDecibels = std::clamp(finiteOr(value.gainDecibels, 0.0f),
                                    kMinimumSampleGainDecibels,
                                    kMaximumSampleGainDecibels);
    value.pan = std::clamp(finiteOr(value.pan, 0.0f),
                           kMinimumSamplePan, kMaximumSamplePan);
    value.tuneSemitones = std::clamp(finiteOr(value.tuneSemitones, 0.0f),
                                     kMinimumTuneSemitones,
                                     kMaximumTuneSemitones);
    value.lowpass = std::clamp(finiteOr(value.lowpass, 0.0f),
                               kMinimumFilterAmount, kMaximumFilterAmount);
    value.highpass = std::clamp(finiteOr(value.highpass, 0.0f),
                                kMinimumFilterAmount, kMaximumFilterAmount);
    value.filterSlope = std::clamp(std::round(finiteOr(value.filterSlope, 0.0f)), 0.0f, 2.0f);
    value.dirty = finiteOr(value.dirty, 0.0f) >= 0.5f ? 1.0f : 0.0f;
    return value;
}

[[nodiscard]] inline float sampleGain(const SampleMixerSettings& requested) noexcept
{
    return std::pow(10.0f, sanitize(requested).gainDecibels / 20.0f);
}

[[nodiscard]] inline double tuneRatio(const SampleMixerSettings& requested) noexcept
{
    return std::exp2(static_cast<double>(sanitize(requested).tuneSemitones) / 12.0);
}

} // namespace sms::dsp
