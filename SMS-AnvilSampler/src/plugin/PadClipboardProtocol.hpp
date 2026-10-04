#pragma once

#include "AlternatingResultEvent.hpp"
#include "Configuration.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace midichopper::plugin {

enum class PadClipboardAction : std::uint8_t {
    copy,
    paste,
};

enum class PadClipboardResultCode : std::uint8_t {
    none,
    copied,
    pasted,
    failed,
};

inline constexpr std::uint32_t kPadClipboardResultCount = 3U;

[[nodiscard]] inline constexpr float padClipboardResultEventValue(
    const PadClipboardResultCode result, const bool alternateHalf) noexcept
{
    return alternatingResultEventValue<PadClipboardResultCode, kPadClipboardResultCount>(
        result, alternateHalf);
}

using PadClipboardResultEventTracker =
    AlternatingResultEventTracker<PadClipboardResultCode, kPadClipboardResultCount>;

struct PadClipboardRequest {
    PadClipboardAction action = PadClipboardAction::copy;
    std::uint32_t pad = kPadCount;
};

[[nodiscard]] inline std::string encodePadClipboardRequest(
    const PadClipboardAction action, const std::uint32_t pad)
{
    return std::to_string(static_cast<unsigned int>(action)) + ";" +
           std::to_string(pad);
}

[[nodiscard]] inline bool decodePadClipboardRequest(
    const std::string_view value, PadClipboardRequest& request) noexcept
{
    const auto separator = value.find(';');
    if (separator == std::string_view::npos || separator == 0U ||
        separator + 1U >= value.size() || value.find(';', separator + 1U) != std::string_view::npos)
        return false;

    unsigned int action = 0U;
    unsigned int pad = 0U;
    const auto actionText = value.substr(0U, separator);
    const auto padText = value.substr(separator + 1U);
    const auto actionParse = std::from_chars(
        actionText.data(), actionText.data() + actionText.size(), action);
    const auto padParse = std::from_chars(
        padText.data(), padText.data() + padText.size(), pad);
    if (actionParse.ec != std::errc{} || actionParse.ptr != actionText.data() + actionText.size() ||
        padParse.ec != std::errc{} || padParse.ptr != padText.data() + padText.size() ||
        action > static_cast<unsigned int>(PadClipboardAction::paste) || pad >= kPadCount)
        return false;

    request.action = static_cast<PadClipboardAction>(action);
    request.pad = pad;
    return true;
}

} // namespace midichopper::plugin
