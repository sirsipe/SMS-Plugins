#pragma once

#include <cstdint>
#include <string_view>

namespace midichopper::plugin {

inline constexpr const char* kCaptureActionRequestKey = "capture_action_request";

/** Match the existing capture trigger bits; empty restored state is inert. */
[[nodiscard]] constexpr std::uint32_t captureActionRequestMask(
    const std::string_view value) noexcept
{
    return value == "finalize" ? 0x1U
         : value == "undo" ? 0x2U
         : value == "clear" ? 0x4U : 0U;
}

} // namespace midichopper::plugin
