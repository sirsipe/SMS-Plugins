#pragma once

#include "UI/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sms::ui::meter {

inline constexpr float floorDb = -60.0f;
inline constexpr float yellowStartDb = -18.0f;
inline constexpr float redStartDb = -6.0f;
inline constexpr float maximumDb = 0.0f;
inline constexpr std::uint32_t defaultSegmentCount = 30U;

enum class Zone : std::uint8_t { green, yellow, red };

[[nodiscard]] inline float amplitudeToDb(const float amplitude) noexcept
{
    if (std::isnan(amplitude) || amplitude <= 0.0f)
        return floorDb;
    if (std::isinf(amplitude))
        return maximumDb;
    return std::clamp(20.0f * std::log10(amplitude), floorDb, maximumDb);
}

[[nodiscard]] inline float normalizedLevel(const float amplitude) noexcept
{
    return (amplitudeToDb(amplitude) - floorDb) / (maximumDb - floorDb);
}

[[nodiscard]] inline std::uint32_t activeSegmentCount(
    const float amplitude, const std::uint32_t segments = defaultSegmentCount) noexcept
{
    if (segments == 0U || std::isnan(amplitude) || amplitude <= 0.0f)
        return 0U;
    if (std::isinf(amplitude))
        return segments;
    return std::min(segments, static_cast<std::uint32_t>(
        std::ceil(normalizedLevel(amplitude) * static_cast<float>(segments))));
}

[[nodiscard]] inline bool visibleLevelChanged(
    const float previousAmplitude, const float nextAmplitude,
    const std::uint32_t segments = defaultSegmentCount) noexcept
{
    return activeSegmentCount(previousAmplitude, segments) !=
           activeSegmentCount(nextAmplitude, segments);
}

[[nodiscard]] inline Zone segmentZone(
    const std::uint32_t segment, const std::uint32_t segments = defaultSegmentCount) noexcept
{
    if (segments == 0U)
        return Zone::green;
    const float topDb = floorDb + (maximumDb - floorDb) *
        static_cast<float>(std::min(segment + 1U, segments)) /
        static_cast<float>(segments);
    if (topDb >= redStartDb)
        return Zone::red;
    if (topDb >= yellowStartDb)
        return Zone::yellow;
    return Zone::green;
}

/** Geometry for two vertical LED columns inside any positive-sized bounds. */
class StereoGeometry {
public:
    explicit constexpr StereoGeometry(
        const Rect bounds, const std::uint32_t segments = defaultSegmentCount) noexcept
        : bounds_(bounds), segments_(segments) {}

    [[nodiscard]] constexpr Rect channel(const std::uint32_t channelIndex) const noexcept
    {
        const float padding = std::min(2.0f, bounds_.width * 0.1f);
        const float gap = std::min(3.0f, bounds_.width * 0.12f);
        const float width = std::max(
            0.0f, (bounds_.width - padding * 2.0f - gap) * 0.5f);
        return {bounds_.x + padding + (channelIndex == 0U ? 0.0f : width + gap),
                bounds_.y + padding, width,
                std::max(0.0f, bounds_.height - padding * 2.0f)};
    }

    [[nodiscard]] constexpr Rect segment(const std::uint32_t channelIndex,
                                         const std::uint32_t segmentIndex) const noexcept
    {
        const Rect column = channel(channelIndex);
        if (segments_ == 0U)
            return {column.x, column.y + column.height, column.width, 0.0f};
        const float gap = std::min(3.0f,
            std::max(0.0f, column.height / (static_cast<float>(segments_) * 6.0f)));
        const float height = std::max(0.0f,
            (column.height - gap * static_cast<float>(segments_ - 1U)) /
            static_cast<float>(segments_));
        const std::uint32_t index = std::min(segmentIndex, segments_ - 1U);
        return {column.x,
                column.y + column.height - height -
                    static_cast<float>(index) * (height + gap),
                column.width, height};
    }

private:
    Rect bounds_;
    std::uint32_t segments_;
};

} // namespace sms::ui::meter
