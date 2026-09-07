#pragma once

#include "Audio/WaveformSummary.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "UI/Geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sms::ui::waveform {

inline constexpr float kInteractiveRegionMinimum = 0.002f;
inline constexpr float kEnvelopeControlMaximumSeconds = 5.0f;
inline constexpr float kEnvelopeSliderLabelWidth = 90.0f;

enum class EditTarget : int {
    none = -1,
    regionStart,
    regionEnd,
    attackSlider,
    decaySlider,
    sustainSlider,
    releaseSlider,
    attackNode,
    decayNode,
    releaseNode,
};

struct EnvelopeGeometry {
    float left;
    float right;
    float top;
    float bottom;
    float attackX;
    float decayX;
    float releaseX;
    float attackHandleX;
    float decayHandleX;
    float releaseHandleX;
    float sustainY;
    float durationSeconds;
};

[[nodiscard]] inline ui::Rect envelopeSliderTrack(const ui::Rect bounds) noexcept
{
    return {bounds.x + kEnvelopeSliderLabelWidth, bounds.y + 14.0f,
            bounds.width - kEnvelopeSliderLabelWidth, 6.0f};
}

[[nodiscard]] inline float normalizedX(const float x, const ui::Rect bounds) noexcept
{
    return std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
}

[[nodiscard]] inline float envelopeTimeFromNormalized(const float normalized) noexcept
{
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    return clamped * clamped * kEnvelopeControlMaximumSeconds;
}

[[nodiscard]] inline float envelopeTimeToNormalized(const float seconds) noexcept
{
    return std::sqrt(std::clamp(seconds / kEnvelopeControlMaximumSeconds, 0.0f, 1.0f));
}

[[nodiscard]] inline EditTarget nearestRegionHandle(
    const float x, const ui::Rect bounds,
    const dsp::SamplePlaybackSettings& settings) noexcept
{
    const float startX = bounds.x + bounds.width * settings.start;
    const float endX = bounds.x + bounds.width * settings.end;
    return std::abs(x - startX) <= std::abs(x - endX)
        ? EditTarget::regionStart : EditTarget::regionEnd;
}

inline void updateRegion(dsp::SamplePlaybackSettings& settings, const EditTarget target,
                         const float x, const ui::Rect bounds) noexcept
{
    const float normalized = normalizedX(x, bounds);
    if (target == EditTarget::regionStart)
        settings.start = std::min(normalized, settings.end - kInteractiveRegionMinimum);
    else if (target == EditTarget::regionEnd)
        settings.end = std::max(normalized, settings.start + kInteractiveRegionMinimum);
    settings = dsp::sanitize(settings);
}

inline void updateEnvelopeSlider(dsp::SamplePlaybackSettings& settings,
                                 const EditTarget target, const float x,
                                 const ui::Rect bounds) noexcept
{
    const float normalized = normalizedX(x, envelopeSliderTrack(bounds));
    const float seconds = envelopeTimeFromNormalized(normalized);
    switch (target) {
    case EditTarget::attackSlider: settings.attackSeconds = seconds; break;
    case EditTarget::decaySlider: settings.decaySeconds = seconds; break;
    case EditTarget::sustainSlider: settings.sustainLevel = normalized; break;
    case EditTarget::releaseSlider: settings.releaseSeconds = seconds; break;
    default: return;
    }
    settings = dsp::sanitize(settings);
}

[[nodiscard]] inline float displayScale(const audio::WaveformSummary& waveform) noexcept
{
    float peak = 0.0f;
    for (std::size_t bin = 0; bin < audio::kWaveformBins; ++bin)
        peak = std::max({peak, std::abs(waveform.minimum[bin]),
                         std::abs(waveform.maximum[bin])});
    return peak > 1.0e-6f ? 1.0f / peak : 1.0f;
}

[[nodiscard]] inline float regionDuration(const audio::WaveformSummary& waveform,
                                          const dsp::SamplePlaybackSettings& settings) noexcept
{
    if (waveform.frames != 0U && waveform.sampleRate > 1.0)
        return std::max(1.0e-4f, (settings.end - settings.start) *
            static_cast<float>(waveform.frames) / static_cast<float>(waveform.sampleRate));
    return 5.0f;
}

[[nodiscard]] inline EnvelopeGeometry
envelopeGeometry(const ui::Rect bounds, const audio::WaveformSummary& waveform,
                 const dsp::SamplePlaybackSettings& settings) noexcept
{
    const float duration = regionDuration(waveform, settings);
    const float left = bounds.x + 10.0f;
    const float right = bounds.x + bounds.width - 10.0f;
    const float top = bounds.y + 10.0f;
    const float bottom = bounds.y + bounds.height - 16.0f;
    const float release = std::min(settings.releaseSeconds, duration * 0.5f);
    const float releaseStart = duration - release;
    const float attackEnd = std::min(settings.attackSeconds, releaseStart);
    const float decayEnd = std::min(settings.attackSeconds + settings.decaySeconds, releaseStart);
    const auto toX = [left, right, duration](const float seconds) noexcept {
        return left + (right - left) * std::clamp(seconds / duration, 0.0f, 1.0f);
    };
    const float attackX = toX(attackEnd);
    const float decayX = toX(decayEnd);
    const float releaseX = toX(releaseStart);
    constexpr float handleGap = 12.0f;
    const float attackHandleX = std::clamp(attackX, left, right - handleGap * 2.0f);
    const float decayHandleX = std::clamp(decayX, attackHandleX + handleGap,
                                          right - handleGap);
    const float releaseHandleX = std::clamp(releaseX, decayHandleX + handleGap, right);
    return {left, right, top, bottom, attackX, decayX, releaseX,
            attackHandleX, decayHandleX, releaseHandleX,
            bottom - (bottom - top) * settings.sustainLevel, duration};
}

[[nodiscard]] inline EditTarget hitEnvelopeHandle(const ui::Point point,
                                                   const EnvelopeGeometry& geometry) noexcept
{
    const std::array<ui::Point, 3> handles{{
        {geometry.attackHandleX, geometry.top},
        {geometry.decayHandleX, geometry.sustainY},
        {geometry.releaseHandleX, geometry.sustainY},
    }};
    EditTarget nearest = EditTarget::none;
    float nearestDistance = 15.0f * 15.0f;
    for (std::size_t index = 0; index < handles.size(); ++index) {
        const float deltaX = point.x - handles[index].x;
        const float deltaY = point.y - handles[index].y;
        const float distance = deltaX * deltaX + deltaY * deltaY;
        if (distance <= nearestDistance) {
            nearest = static_cast<EditTarget>(
                static_cast<int>(index) + static_cast<int>(EditTarget::attackNode));
            nearestDistance = distance;
        }
    }
    return nearest;
}

inline void updateEnvelopeNode(dsp::SamplePlaybackSettings& settings,
                               const dsp::SamplePlaybackSettings& dragStartSettings,
                               const EditTarget target, const ui::Point point,
                               const ui::Point dragStart,
                               const EnvelopeGeometry& geometry) noexcept
{
    const float secondsDelta = (point.x - dragStart.x) /
                               (geometry.right - geometry.left) * geometry.durationSeconds;
    const float sustainDelta = (dragStart.y - point.y) /
                               (geometry.bottom - geometry.top);
    const float effectiveRelease = std::min(settings.releaseSeconds,
                                            geometry.durationSeconds * 0.5f);
    const float releaseStart = geometry.durationSeconds - effectiveRelease;
    switch (target) {
    case EditTarget::attackNode:
        settings.attackSeconds = std::clamp(
            dragStartSettings.attackSeconds + secondsDelta, 0.0f,
            std::min(dsp::kMaximumEnvelopeStageSeconds, releaseStart));
        break;
    case EditTarget::decayNode:
        settings.decaySeconds = std::clamp(
            dragStartSettings.decaySeconds + secondsDelta, 0.0f,
            std::min(dsp::kMaximumEnvelopeStageSeconds,
                     std::max(0.0f, releaseStart - settings.attackSeconds)));
        settings.sustainLevel = std::clamp(
            dragStartSettings.sustainLevel + sustainDelta, 0.0f, 1.0f);
        break;
    case EditTarget::releaseNode:
        settings.releaseSeconds = std::clamp(
            dragStartSettings.releaseSeconds - secondsDelta, 0.0f,
            std::min(dsp::kMaximumEnvelopeStageSeconds,
                     geometry.durationSeconds * 0.5f));
        settings.sustainLevel = std::clamp(
            dragStartSettings.sustainLevel + sustainDelta, 0.0f, 1.0f);
        break;
    default:
        return;
    }
    settings = dsp::sanitize(settings);
}

[[nodiscard]] inline float envelopeGain(const float sourcePosition,
                                        const audio::WaveformSummary& waveform,
                                        const dsp::SamplePlaybackSettings& settings) noexcept
{
    const float regionWidth = settings.end - settings.start;
    if (regionWidth <= 0.0f || waveform.frames == 0U || waveform.sampleRate <= 1.0)
        return 0.0f;
    const float regionPosition = std::clamp(
        (sourcePosition - settings.start) / regionWidth, 0.0f, 1.0f);
    const float regionSeconds = regionWidth * static_cast<float>(waveform.frames) /
                                static_cast<float>(waveform.sampleRate);
    const float time = regionPosition * regionSeconds;
    const auto adsLevelAt = [&settings](const float stageTime) noexcept {
        if (settings.attackSeconds > 0.0f && stageTime < settings.attackSeconds)
            return std::clamp(stageTime / settings.attackSeconds, 0.0f, 1.0f);
        if (settings.decaySeconds > 0.0f &&
            stageTime < settings.attackSeconds + settings.decaySeconds) {
            const float progress = std::clamp(
                (stageTime - settings.attackSeconds) / settings.decaySeconds, 0.0f, 1.0f);
            return 1.0f + (settings.sustainLevel - 1.0f) * progress;
        }
        return settings.sustainLevel;
    };
    const float release = std::min(settings.releaseSeconds, regionSeconds * 0.5f);
    const float releaseStart = regionSeconds - release;
    if (release > 0.0f && time >= releaseStart) {
        const float releaseLevel = adsLevelAt(releaseStart);
        const float progress = std::clamp((time - releaseStart) / release, 0.0f, 1.0f);
        return releaseLevel * (1.0f - progress);
    }
    return adsLevelAt(time);
}

} // namespace sms::ui::waveform
