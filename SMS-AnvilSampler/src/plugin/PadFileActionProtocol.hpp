#pragma once

#include "AlternatingResultEvent.hpp"
#include "Configuration.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace midichopper::plugin {

enum class PadFileAction : std::uint8_t {
    exportRaw,
    exportProcessed,
    import,
};

enum class PadFileResultCode : std::uint8_t {
    none,
    importSucceeded,
    rawExportSucceeded,
    processedExportSucceeded,
    failed,
};

inline constexpr std::uint32_t kPadFileResultCount = 4U;

[[nodiscard]] inline constexpr float padFileResultEventValue(
    const PadFileResultCode result, const bool alternateHalf) noexcept
{
    return alternatingResultEventValue<PadFileResultCode, kPadFileResultCount>(
        result, alternateHalf);
}

using PadFileResultEventTracker =
    AlternatingResultEventTracker<PadFileResultCode, kPadFileResultCount>;

struct PadFileRequest {
    PadFileAction action = PadFileAction::import;
    std::uint32_t pad = kPadCount;
    std::string_view path;
};

[[nodiscard]] inline std::string encodePadFileRequest(
    const PadFileAction action, const std::uint32_t pad, const std::string_view path)
{
    return std::to_string(static_cast<unsigned int>(action)) + ";" +
           std::to_string(pad) + ";" + std::string(path);
}

[[nodiscard]] inline bool decodePadFileRequest(
    const std::string_view value, PadFileRequest& request) noexcept
{
    const auto first = value.find(';');
    const auto second = first == std::string_view::npos
        ? std::string_view::npos : value.find(';', first + 1U);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        second + 1U >= value.size())
        return false;

    unsigned int action = 0U;
    unsigned int pad = 0U;
    const auto actionText = value.substr(0U, first);
    const auto padText = value.substr(first + 1U, second - first - 1U);
    const auto actionParse = std::from_chars(
        actionText.data(), actionText.data() + actionText.size(), action);
    const auto padParse = std::from_chars(
        padText.data(), padText.data() + padText.size(), pad);
    if (actionParse.ec != std::errc{} || actionParse.ptr != actionText.data() + actionText.size() ||
        padParse.ec != std::errc{} || padParse.ptr != padText.data() + padText.size() ||
        action > static_cast<unsigned int>(PadFileAction::import) || pad >= kPadCount)
        return false;

    request.action = static_cast<PadFileAction>(action);
    request.pad = pad;
    request.path = value.substr(second + 1U);
    return true;
}

} // namespace midichopper::plugin
