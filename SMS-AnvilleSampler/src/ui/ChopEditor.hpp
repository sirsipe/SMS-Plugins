#pragma once

#include "Audio/WaveformSummary.hpp"
#include "UI/Geometry.hpp"
#include "WaveformViewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace midichopper::ui::chop {

inline constexpr std::uint32_t kPadCount = 3U;
inline constexpr std::uint32_t kBoundaryCount = kPadCount - 1U;

/** Return the adjacent center-pad slot, or -1 when its window would cross a bank edge. */
[[nodiscard]] constexpr int navigationTarget(const int currentPad,
                                             const int direction,
                                             const int visiblePadCount) noexcept
{
    if (direction == 0 || visiblePadCount < static_cast<int>(kPadCount))
        return -1;
    const int next = currentPad + (direction < 0 ? -1 : 1);
    return next > 0 && next + 1 < visiblePadCount ? next : -1;
}

/** Center the two new split pads in a normal three-pad editor window. */
[[nodiscard]] constexpr int postSplitEditorTarget(const int sourcePad,
                                                  const int visiblePadCount) noexcept
{
    if (sourcePad < 0 || sourcePad + 1 >= visiblePadCount)
        return -1;
    if (sourcePad + 2 < visiblePadCount)
        return sourcePad + 1;
    return sourcePad > 0 ? sourcePad : -1;
}

[[nodiscard]] inline bool ready(
    const std::span<const sms::audio::WaveformSummary> waveforms) noexcept
{
    if (waveforms.size() != kPadCount)
        return false;

    double audioRate = 0.0;
    bool hasAudio = false;
    for (const auto& waveform : waveforms) {
        // Empty summaries still carry the engine rate, which distinguishes a
        // received response from a default-initialized summary.
        if (!std::isfinite(waveform.sampleRate) || waveform.sampleRate <= 1.0)
            return false;
        if (waveform.frames == 0U)
            continue;
        if (hasAudio && std::abs(waveform.sampleRate - audioRate) > 0.5)
            return false;
        audioRate = waveform.sampleRate;
        hasAudio = true;
    }
    return hasAudio;
}

[[nodiscard]] inline double sampleRate(
    const std::span<const sms::audio::WaveformSummary> waveforms) noexcept
{
    for (const auto& waveform : waveforms) {
        if (waveform.frames != 0U && std::isfinite(waveform.sampleRate) &&
            waveform.sampleRate > 1.0)
            return waveform.sampleRate;
    }
    return 0.0;
}

[[nodiscard]] inline std::uint64_t totalFrames(
    const std::span<const sms::audio::WaveformSummary> waveforms) noexcept
{
    std::uint64_t result = 0U;
    for (const auto& waveform : waveforms)
        result += waveform.frames;
    return result;
}

[[nodiscard]] inline std::uint64_t originalBoundary(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const int boundary) noexcept
{
    std::uint64_t result = 0U;
    for (int pad = 0; pad <= boundary && pad < static_cast<int>(waveforms.size()); ++pad)
        result += waveforms[static_cast<std::size_t>(pad)].frames;
    return result;
}

[[nodiscard]] inline std::int64_t adjustedBoundary(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary) noexcept
{
    const auto original = static_cast<std::int64_t>(originalBoundary(waveforms, boundary));
    return original + (boundary >= 0 && boundary < static_cast<int>(offsets.size())
        ? offsets[static_cast<std::size_t>(boundary)] : 0);
}

[[nodiscard]] inline float boundaryX(
    const sms::ui::Rect waveformBounds,
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary) noexcept
{
    const auto total = totalFrames(waveforms);
    if (total == 0U)
        return waveformBounds.x;
    return waveformBounds.x + waveformBounds.width * static_cast<float>(
        static_cast<double>(adjustedBoundary(waveforms, offsets, boundary)) / total);
}

[[nodiscard]] inline float boundaryX(
    const sms::ui::Rect waveformBounds,
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary,
    const sms::ui::waveform::Viewport viewport) noexcept
{
    if (!viewport.zoomed())
        return boundaryX(waveformBounds, waveforms, offsets, boundary);
    return viewport.xForFrame(
        static_cast<double>(adjustedBoundary(waveforms, offsets, boundary)),
        waveformBounds);
}

[[nodiscard]] inline sms::ui::Rect boundaryHandle(
    const sms::ui::Rect waveformBounds,
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary) noexcept
{
    return {boundaryX(waveformBounds, waveforms, offsets, boundary) - 9.0f,
            waveformBounds.y, 18.0f, waveformBounds.height};
}

[[nodiscard]] inline int boundaryAt(
    const sms::ui::Point point, const sms::ui::Rect waveformBounds,
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets) noexcept
{
    if (!ready(waveforms))
        return -1;
    for (int boundary = 0; boundary < static_cast<int>(kBoundaryCount); ++boundary) {
        if (boundaryHandle(waveformBounds, waveforms, offsets, boundary).contains(point))
            return boundary;
    }
    return -1;
}

[[nodiscard]] inline int boundaryAt(
    const sms::ui::Point point, const sms::ui::Rect waveformBounds,
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets,
    const sms::ui::waveform::Viewport viewport) noexcept
{
    if (!ready(waveforms) || !waveformBounds.contains(point))
        return -1;
    for (int boundary = 0; boundary < static_cast<int>(kBoundaryCount); ++boundary) {
        const auto x = boundaryX(waveformBounds, waveforms, offsets, boundary, viewport);
        if (std::abs(point.x - x) <= 9.0f)
            return boundary;
    }
    return -1;
}

[[nodiscard]] inline std::int64_t clampBoundaryOffset(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary,
    const std::int64_t requested) noexcept
{
    if (boundary < 0 || boundary + 1 >= static_cast<int>(waveforms.size()))
        return 0;
    const auto original = static_cast<std::int64_t>(originalBoundary(waveforms, boundary));
    const auto previous = boundary == 0 ? std::int64_t{0} :
        adjustedBoundary(waveforms, offsets, boundary - 1);
    const auto next = boundary + 1 == static_cast<int>(waveforms.size()) - 1
        ? static_cast<std::int64_t>(totalFrames(waveforms))
        : adjustedBoundary(waveforms, offsets, boundary + 1);
    return std::clamp(requested,
        previous - original, next - original);
}

[[nodiscard]] inline std::int64_t wheelAdjustedBoundaryOffset(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary,
    const float verticalDelta, const sms::ui::Rect waveformBounds,
    const sms::ui::waveform::Viewport viewport = {}) noexcept
{
    if (verticalDelta == 0.0f || !std::isfinite(verticalDelta) ||
        waveformBounds.width <= 0.0f || boundary < 0 ||
        boundary >= static_cast<int>(offsets.size()))
        return boundary >= 0 && boundary < static_cast<int>(offsets.size())
            ? offsets[static_cast<std::size_t>(boundary)] : 0;
    const auto step = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(
        static_cast<double>(viewport.zoomed() ? viewport.end - viewport.start :
            totalFrames(waveforms)) / waveformBounds.width)));
    const auto requested = offsets[static_cast<std::size_t>(boundary)] +
        (verticalDelta > 0.0f ? step : -step);
    return clampBoundaryOffset(waveforms, offsets, boundary, requested);
}

[[nodiscard]] inline std::uint64_t adjustedStartFrame(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int pad) noexcept
{
    if (pad <= 0)
        return 0U;
    return static_cast<std::uint64_t>(std::max<std::int64_t>(
        adjustedBoundary(waveforms, offsets, pad - 1), 0));
}

[[nodiscard]] inline std::uint32_t adjustedFrames(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int pad) noexcept
{
    if (pad < 0 || pad >= static_cast<int>(waveforms.size()))
        return 0U;
    const auto start = static_cast<std::int64_t>(adjustedStartFrame(waveforms, offsets, pad));
    const auto end = pad + 1 == static_cast<int>(waveforms.size())
        ? static_cast<std::int64_t>(totalFrames(waveforms))
        : adjustedBoundary(waveforms, offsets, pad);
    return static_cast<std::uint32_t>(std::max<std::int64_t>(end - start, 0));
}

[[nodiscard]] inline double sourceFrameForPlayhead(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const int originalPad, const float originalFraction) noexcept
{
    if (originalPad < 0 || originalPad >= static_cast<int>(waveforms.size()))
        return -1.0;
    double result = 0.0;
    for (int pad = 0; pad < originalPad; ++pad)
        result += waveforms[static_cast<std::size_t>(pad)].frames;
    return result + std::clamp(originalFraction, 0.0f, 0.999999f) *
        waveforms[static_cast<std::size_t>(originalPad)].frames;
}

[[nodiscard]] inline sms::audio::WaveformSummary combinedWaveform(
    const std::span<const sms::audio::WaveformSummary> waveforms) noexcept
{
    sms::audio::WaveformSummary result;
    if (waveforms.empty())
        return result;
    result.pad = waveforms.front().pad;
    result.sampleRate = sampleRate(waveforms);
    const auto total = totalFrames(waveforms);
    result.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        total, std::numeric_limits<std::uint32_t>::max()));
    if (total == 0U)
        return result;

    std::array<std::uint64_t, kPadCount + 1U> starts{};
    for (std::size_t pad = 0; pad < waveforms.size() && pad < kPadCount; ++pad)
        starts[pad + 1U] = starts[pad] + waveforms[pad].frames;
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const auto frame = static_cast<std::uint64_t>(
            (static_cast<double>(bin) + 0.5) * total / sms::audio::kWaveformBins);
        std::size_t pad = 0U;
        while (pad + 1U < waveforms.size() && frame >= starts[pad + 1U])
            ++pad;
        if (pad >= waveforms.size() || waveforms[pad].frames == 0U)
            continue;
        const auto localFrame = frame - starts[pad];
        const auto sourceBin = std::min<std::size_t>(sms::audio::kWaveformBins - 1U,
            static_cast<std::size_t>(localFrame * sms::audio::kWaveformBins /
                                     waveforms[pad].frames));
        result.minimum[bin] = waveforms[pad].minimum[sourceBin];
        result.maximum[bin] = waveforms[pad].maximum[sourceBin];
    }
    return result;
}

} // namespace midichopper::ui::chop
