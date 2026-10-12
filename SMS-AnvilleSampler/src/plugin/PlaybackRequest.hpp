#pragma once

#include "Configuration.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>

namespace midichopper::plugin {

enum class PlaybackAction : std::uint32_t { Toggle, ToggleLoop, Play, Loop, StopAudition };

struct PlaybackRequest {
    PlaybackAction action = PlaybackAction::Toggle;
    std::uint32_t pad = kPadCount;
};

// Keep the original 0..64 toggle payloads, including zero's no-selection sentinel.
inline constexpr std::uint32_t kPlaybackRequestStride = 65U;

[[nodiscard]] constexpr std::uint32_t encodePlaybackRequest(
    const PlaybackRequest request) noexcept
{
    return static_cast<std::uint32_t>(request.action) * kPlaybackRequestStride +
        (request.pad < kPadCount ? request.pad + 1U : 0U);
}

[[nodiscard]] inline bool decodePlaybackRequest(
    const std::uint32_t value, PlaybackRequest& destination) noexcept
{
    const auto action = value / kPlaybackRequestStride;
    const auto selected = value % kPlaybackRequestStride;
    if (action > static_cast<std::uint32_t>(PlaybackAction::StopAudition) ||
        selected > kPadCount ||
        ((action == static_cast<std::uint32_t>(PlaybackAction::Play) ||
          action == static_cast<std::uint32_t>(PlaybackAction::Loop)) && selected == 0U) ||
        (action == static_cast<std::uint32_t>(PlaybackAction::StopAudition) && selected != 0U))
        return false;
    destination = {static_cast<PlaybackAction>(action), selected != 0U ? selected - 1U : kPadCount};
    return true;
}

[[nodiscard]] inline bool decodePlaybackRequest(
    const std::string_view text, PlaybackRequest& destination) noexcept
{
    if (text.empty())
        return false;
    std::uint32_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
        decodePlaybackRequest(value, destination);
}

} // namespace midichopper::plugin
