#pragma once

#include <algorithm>
#include <cmath>

namespace sms::dsp {

inline constexpr float kMinimumPlaybackRegion = 1.0e-5f;
inline constexpr float kMaximumEnvelopeStageSeconds = 30.0f;

/** Non-destructive sample-region and amplitude-envelope settings. */
struct SamplePlaybackSettings {
    float start = 0.0f;
    float end = 1.0f;
    float attackSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f;
    float releaseSeconds = 0.0f;
};

[[nodiscard]] inline SamplePlaybackSettings sanitize(SamplePlaybackSettings value) noexcept
{
    const auto finiteOr = [](const float candidate, const float fallback) noexcept {
        return std::isfinite(candidate) ? candidate : fallback;
    };
    value.start = std::clamp(finiteOr(value.start, 0.0f),
                             0.0f, 1.0f - kMinimumPlaybackRegion);
    value.end = std::clamp(finiteOr(value.end, 1.0f),
                           value.start + kMinimumPlaybackRegion, 1.0f);
    value.attackSeconds = std::clamp(finiteOr(value.attackSeconds, 0.0f),
                                     0.0f, kMaximumEnvelopeStageSeconds);
    value.decaySeconds = std::clamp(finiteOr(value.decaySeconds, 0.0f),
                                    0.0f, kMaximumEnvelopeStageSeconds);
    value.sustainLevel = std::clamp(finiteOr(value.sustainLevel, 1.0f), 0.0f, 1.0f);
    value.releaseSeconds = std::clamp(finiteOr(value.releaseSeconds, 0.0f),
                                      0.0f, kMaximumEnvelopeStageSeconds);
    return value;
}

} // namespace sms::dsp
