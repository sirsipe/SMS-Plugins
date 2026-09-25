#pragma once

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
};

} // namespace sms::ui::waveform
