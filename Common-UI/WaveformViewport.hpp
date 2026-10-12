#pragma once

#include "Audio/WaveformSummary.hpp"
#include "UI/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sms::ui::waveform {

struct Viewport {
    std::uint64_t total = 0U;
    std::uint64_t start = 0U;
    std::uint64_t end = 0U;

    void reset(const std::uint64_t frames) noexcept
    {
        total = frames;
        start = 0U;
        end = frames;
    }

    [[nodiscard]] bool zoomed() const noexcept { return total != 0U && end - start < total; }

    [[nodiscard]] double fraction(const double frame) const noexcept
    {
        return end > start ? (frame - static_cast<double>(start)) /
            static_cast<double>(end - start) : 0.0;
    }

    [[nodiscard]] double frameAt(const float x, const Rect bounds) const noexcept
    {
        const double position = std::clamp(
            static_cast<double>((x - bounds.x) / bounds.width), 0.0, 1.0);
        return static_cast<double>(start) + position * static_cast<double>(end - start);
    }

    [[nodiscard]] float xForFrame(const double frame, const Rect bounds) const noexcept
    {
        return bounds.x + bounds.width * static_cast<float>(fraction(frame));
    }

    [[nodiscard]] bool zoom(const float delta, const float anchorX, const Rect bounds) noexcept
    {
        if (total < 2U || delta == 0.0f || !std::isfinite(delta))
            return false;
        const auto oldStart = start;
        const auto oldEnd = end;
        const double anchor = frameAt(anchorX, bounds);
        const double cursor = std::clamp(
            static_cast<double>((anchorX - bounds.x) / bounds.width), 0.0, 1.0);
        const double factor = std::pow(2.0, std::clamp(static_cast<double>(delta), -8.0, 8.0));
        const auto length = static_cast<std::uint64_t>(std::clamp(
            std::llround(static_cast<double>(end - start) / factor),
            static_cast<long long>(std::min<std::uint64_t>(16U, total)),
            static_cast<long long>(total)));
        const auto proposed = static_cast<std::int64_t>(std::llround(
            anchor - cursor * static_cast<double>(length)));
        start = static_cast<std::uint64_t>(std::clamp<std::int64_t>(
            proposed, 0, static_cast<std::int64_t>(total - length)));
        end = start + length;
        return start != oldStart || end != oldEnd;
    }

    [[nodiscard]] bool pan(const float delta) noexcept
    {
        if (!zoomed() || delta == 0.0f || !std::isfinite(delta))
            return false;
        const auto oldStart = start;
        const auto length = end - start;
        const auto step = std::max<std::int64_t>(1, static_cast<std::int64_t>(
            std::llround(static_cast<double>(length) * 0.2 * std::abs(delta))));
        const auto proposed = static_cast<std::int64_t>(start) +
            (delta > 0.0f ? -step : step);
        start = static_cast<std::uint64_t>(std::clamp<std::int64_t>(
            proposed, 0, static_cast<std::int64_t>(total - length)));
        end = start + length;
        return start != oldStart;
    }

    /** Zero shows the full source; one shows the smallest supported window. */
    [[nodiscard]] float zoomPosition() const noexcept
    {
        if (total < 2U)
            return 0.0f;
        const double maximum = std::log2(static_cast<double>(total) /
            static_cast<double>(std::min<std::uint64_t>(16U, total)));
        return maximum > 0.0 ? static_cast<float>(
            std::log2(static_cast<double>(total) / (end - start)) / maximum) : 0.0f;
    }

    [[nodiscard]] bool setZoomPosition(const float position) noexcept
    {
        if (total < 2U || !std::isfinite(position))
            return false;
        const auto oldStart = start;
        const auto oldEnd = end;
        const auto minimum = std::min<std::uint64_t>(16U, total);
        const double ratio = static_cast<double>(total) / minimum;
        const auto length = static_cast<std::uint64_t>(std::clamp(
            std::llround(static_cast<double>(total) /
                std::pow(ratio, std::clamp(position, 0.0f, 1.0f))),
            static_cast<long long>(minimum), static_cast<long long>(total)));
        const double center = (static_cast<double>(start) + end) * 0.5;
        start = static_cast<std::uint64_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::llround(center - length * 0.5)),
            0, static_cast<std::int64_t>(total - length)));
        end = start + length;
        return start != oldStart || end != oldEnd;
    }

    /** Position of the left edge of the window along its available travel. */
    [[nodiscard]] float scrollPosition() const noexcept
    {
        return zoomed() ? static_cast<float>(
            static_cast<double>(start) / (total - (end - start))) : 0.0f;
    }

    [[nodiscard]] Rect scrollThumb(const Rect track) const noexcept
    {
        const float width = total != 0U ? std::min(track.width, std::max(12.0f,
            track.width * static_cast<float>(
                static_cast<double>(end - start) / total))) : track.width;
        return {track.x + (track.width - width) * scrollPosition(),
                track.y, width, track.height};
    }

    [[nodiscard]] bool setScrollPosition(const float position) noexcept
    {
        if (!zoomed() || !std::isfinite(position))
            return false;
        const auto oldStart = start;
        const auto length = end - start;
        start = static_cast<std::uint64_t>(std::llround(
            std::clamp(position, 0.0f, 1.0f) * static_cast<double>(total - length)));
        end = start + length;
        return start != oldStart;
    }
};

/** Cached bins retain their source coordinates while the viewport moves. */
struct CachedDetail {
    audio::WaveformSummary waveform;
    std::uint64_t start = 0U;
    std::uint64_t end = 0U;
};

/** Reproject retained extrema; uncovered bins use the coarse whole-source view.
 * This is a display approximation until matching detail arrives, not new PCM. */
[[nodiscard]] inline audio::WaveformSummary visibleSummary(
    const audio::WaveformSummary& overview, const Viewport viewport,
    const CachedDetail* const detail = nullptr) noexcept
{
    if (!viewport.zoomed() || overview.frames == 0U || viewport.end <= viewport.start)
        return overview;
    if (detail != nullptr && detail->start == viewport.start && detail->end == viewport.end)
        return detail->waveform;

    auto visible = overview;
    const auto length = viewport.end - viewport.start;
    visible.frames = static_cast<std::uint32_t>(length);
    for (std::size_t bin = 0; bin < audio::kWaveformBins; ++bin) {
        const auto first = viewport.start + length * bin / audio::kWaveformBins;
        const auto last = std::min(viewport.end, std::max(first + 1U,
            viewport.start + length * (bin + 1U) / audio::kWaveformBins));
        const bool covered = detail != nullptr && detail->start <= first &&
            detail->end >= last && detail->end > detail->start &&
            detail->end - detail->start <= viewport.total;
        const auto& source = covered ? detail->waveform : overview;
        const auto sourceStart = covered ? detail->start : 0U;
        const auto sourceLength = covered ? detail->end - detail->start : viewport.total;
        float low = 1.0f;
        float high = -1.0f;
        for (std::size_t index = 0; index < audio::kWaveformBins; ++index) {
            const auto sourceFirst = sourceStart + sourceLength * index / audio::kWaveformBins;
            const auto sourceLast = std::min(sourceStart + sourceLength,
                std::max(sourceFirst + 1U,
                    sourceStart + sourceLength * (index + 1U) / audio::kWaveformBins));
            if (sourceFirst < last && sourceLast > first) {
                low = std::min(low, source.minimum[index]);
                high = std::max(high, source.maximum[index]);
            }
        }
        visible.minimum[bin] = low <= high ? low : 0.0f;
        visible.maximum[bin] = low <= high ? high : 0.0f;
    }
    return visible;
}

} // namespace sms::ui::waveform
