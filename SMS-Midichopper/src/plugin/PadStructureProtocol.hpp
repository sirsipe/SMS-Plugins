#pragma once

#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace midichopper::plugin {

enum class PadStructureAction : std::uint8_t {
    collapse,
    prepareSplit,
    applySplit,
    cancelSplit,
};

struct PadStructureRequest {
    PadStructureAction action = PadStructureAction::collapse;
    std::uint32_t firstPad = 0U;
    std::uint32_t padCount = 0U;
    std::uint32_t targetPad = 0U;
    std::uint64_t planId = 0U;
    std::uint32_t splitFrame = 0U;
};

struct SplitPlanReady {
    std::uint64_t planId = 0U;
    std::uint32_t targetPad = 0U;
    std::uint32_t emptyPad = 0U;
    sms::audio::WaveformSummary waveform{};
};

template <class Integer>
[[nodiscard]] inline bool parsePadStructureInteger(
    const std::string_view text, Integer& value) noexcept
{
    if (text.empty())
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

[[nodiscard]] inline std::string encodePadStructureRequest(
    const PadStructureRequest& request)
{
    switch (request.action) {
    case PadStructureAction::collapse:
        return "PS1;C;" + std::to_string(request.firstPad) + ";" +
            std::to_string(request.padCount) + ";" + std::to_string(request.targetPad);
    case PadStructureAction::prepareSplit:
        return "PS1;P;" + std::to_string(request.firstPad) + ";" +
            std::to_string(request.padCount) + ";" + std::to_string(request.targetPad);
    case PadStructureAction::applySplit:
        return "PS1;A;" + std::to_string(request.planId) + ";" +
            std::to_string(request.splitFrame);
    case PadStructureAction::cancelSplit:
        return "PS1;X;" + std::to_string(request.planId);
    }
    return {};
}

[[nodiscard]] inline bool decodePadStructureRequest(
    const std::string_view encoded, PadStructureRequest& request) noexcept
{
    if (!encoded.starts_with("PS1;") || encoded.size() < 6U || encoded.back() == ';')
        return false;
    PadStructureRequest decoded;
    const char action = encoded[4];
    if (encoded[5] != ';')
        return false;
    std::string_view rest = encoded.substr(6U);
    auto token = [&rest](std::string_view& result) noexcept {
        const auto separator = rest.find(';');
        result = rest.substr(0U, separator);
        rest = separator == std::string_view::npos
            ? std::string_view{} : rest.substr(separator + 1U);
        return !result.empty();
    };
    std::string_view first;
    std::string_view second;
    std::string_view third;
    if (!token(first))
        return false;
    if (action == 'C' || action == 'P') {
        if (!token(second) || !token(third) || !rest.empty() ||
            !parsePadStructureInteger(first, decoded.firstPad) ||
            !parsePadStructureInteger(second, decoded.padCount) ||
            !parsePadStructureInteger(third, decoded.targetPad) ||
            decoded.padCount == 0U || decoded.padCount > kPadsPerBank ||
            decoded.firstPad >= kPadCount || decoded.padCount > kPadCount - decoded.firstPad ||
            decoded.targetPad < decoded.firstPad ||
            decoded.targetPad >= decoded.firstPad + decoded.padCount)
            return false;
        decoded.action = action == 'C'
            ? PadStructureAction::collapse : PadStructureAction::prepareSplit;
    } else if (action == 'A') {
        if (!token(second) || !rest.empty() ||
            !parsePadStructureInteger(first, decoded.planId) || decoded.planId == 0U ||
            !parsePadStructureInteger(second, decoded.splitFrame) || decoded.splitFrame == 0U)
            return false;
        decoded.action = PadStructureAction::applySplit;
    } else if (action == 'X') {
        if (!rest.empty() || !parsePadStructureInteger(first, decoded.planId) ||
            decoded.planId == 0U)
            return false;
        decoded.action = PadStructureAction::cancelSplit;
    } else {
        return false;
    }
    request = decoded;
    return true;
}

[[nodiscard]] inline std::string encodeSplitPlanReady(const SplitPlanReady& plan)
{
    return "PS1;R;" + std::to_string(plan.planId) + ";" +
        std::to_string(plan.targetPad) + ";" + std::to_string(plan.emptyPad) + ";" +
        sms::audio::encodeWaveformSummary(plan.waveform);
}

[[nodiscard]] inline bool decodeSplitPlanReady(
    const std::string_view encoded, SplitPlanReady& plan) noexcept
{
    if (!encoded.starts_with("PS1;R;"))
        return false;
    const auto firstEnd = encoded.find(';', 6U);
    const auto secondEnd = firstEnd == std::string_view::npos
        ? std::string_view::npos : encoded.find(';', firstEnd + 1U);
    const auto thirdEnd = secondEnd == std::string_view::npos
        ? std::string_view::npos : encoded.find(';', secondEnd + 1U);
    if (firstEnd == std::string_view::npos || secondEnd == std::string_view::npos ||
        thirdEnd == std::string_view::npos)
        return false;
    SplitPlanReady decoded;
    if (!parsePadStructureInteger(encoded.substr(6U, firstEnd - 6U), decoded.planId) ||
        !parsePadStructureInteger(encoded.substr(firstEnd + 1U, secondEnd - firstEnd - 1U),
                                  decoded.targetPad) ||
        !parsePadStructureInteger(
            encoded.substr(secondEnd + 1U, thirdEnd - secondEnd - 1U), decoded.emptyPad) ||
        !sms::audio::decodeWaveformSummary(encoded.substr(thirdEnd + 1U), decoded.waveform) ||
        decoded.planId == 0U || decoded.targetPad >= kPadCount ||
        decoded.emptyPad <= decoded.targetPad || decoded.emptyPad >= kPadCount ||
        decoded.emptyPad - decoded.targetPad >= kPadsPerBank ||
        decoded.waveform.pad != decoded.targetPad || decoded.waveform.frames < 2U)
        return false;
    plan = decoded;
    return true;
}

} // namespace midichopper::plugin
