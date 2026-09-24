#pragma once

#include "Configuration.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace midichopper::plugin {

struct ChopApplyRequest {
    std::uint32_t firstPad = 0;
    std::uint32_t padCount = 0;
    std::array<std::int64_t, kPadsPerBank - 1U> boundaryOffsets{};
};

struct ChopPreviewRequest {
    bool play = false;
    std::uint32_t firstPad = 0;
    std::uint32_t padCount = 0;
    std::uint64_t sourceFrame = 0;
    std::uint64_t sourceEndFrame = 0;
};

inline constexpr std::size_t kChopMidiPreviewPadCount = 3U;

struct ChopMidiPreviewRequest {
    bool active = false;
    std::uint32_t firstPad = 0;
    std::uint32_t sourcePadCount = 0;
    std::uint32_t previewPadCount = 0;
    std::array<std::uint64_t, kChopMidiPreviewPadCount> sourceFrames{};
    std::array<std::uint64_t, kChopMidiPreviewPadCount> sourceEndFrames{};
};

template <class Integer>
[[nodiscard]] inline bool parseChopInteger(std::string_view token, Integer& value) noexcept
{
    if (token.empty())
        return false;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
}

[[nodiscard]] inline std::string encodeChopApplyRequest(const ChopApplyRequest& request)
{
    std::string encoded = "CH1;" + std::to_string(request.firstPad) + ";" +
                          std::to_string(request.padCount);
    for (std::uint32_t index = 0; index + 1U < request.padCount; ++index)
        encoded += ";" + std::to_string(request.boundaryOffsets[index]);
    return encoded;
}

[[nodiscard]] inline bool decodeChopApplyRequest(
    const std::string_view encoded, ChopApplyRequest& request) noexcept
{
    if (!encoded.starts_with("CH1;"))
        return false;
    ChopApplyRequest decoded;
    std::size_t cursor = 4U;
    auto nextToken = [&encoded, &cursor](std::string_view& token) noexcept {
        if (cursor > encoded.size())
            return false;
        const auto end = encoded.find(';', cursor);
        token = encoded.substr(cursor, end == std::string_view::npos
            ? encoded.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? encoded.size() + 1U : end + 1U;
        return true;
    };
    std::string_view token;
    if (!nextToken(token) || !parseChopInteger(token, decoded.firstPad) ||
        !nextToken(token) || !parseChopInteger(token, decoded.padCount) ||
        decoded.padCount < 2U || decoded.padCount > kPadsPerBank ||
        decoded.firstPad >= kPadCount || decoded.padCount > kPadCount - decoded.firstPad)
        return false;
    for (std::uint32_t index = 0; index + 1U < decoded.padCount; ++index) {
        if (!nextToken(token) || !parseChopInteger(token, decoded.boundaryOffsets[index]))
            return false;
    }
    if (cursor <= encoded.size())
        return false;
    request = decoded;
    return true;
}

[[nodiscard]] inline std::string encodeChopPreviewRequest(const ChopPreviewRequest& request)
{
    return std::string("CP1;") + (request.play ? "1;" : "0;") +
           std::to_string(request.firstPad) + ";" + std::to_string(request.padCount) + ";" +
           std::to_string(request.sourceFrame) + ";" + std::to_string(request.sourceEndFrame);
}

[[nodiscard]] inline bool decodeChopPreviewRequest(
    const std::string_view encoded, ChopPreviewRequest& request) noexcept
{
    if (!encoded.starts_with("CP1;"))
        return false;
    ChopPreviewRequest decoded;
    std::array<std::string_view, 5> tokens{};
    std::size_t cursor = 4U;
    for (auto& token : tokens) {
        if (cursor > encoded.size())
            return false;
        const auto end = encoded.find(';', cursor);
        token = encoded.substr(cursor, end == std::string_view::npos
            ? encoded.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? encoded.size() + 1U : end + 1U;
    }
    unsigned play = 0U;
    if (cursor <= encoded.size() || !parseChopInteger(tokens[0], play) || play > 1U ||
        !parseChopInteger(tokens[1], decoded.firstPad) ||
        !parseChopInteger(tokens[2], decoded.padCount) ||
        !parseChopInteger(tokens[3], decoded.sourceFrame) ||
        !parseChopInteger(tokens[4], decoded.sourceEndFrame) ||
        decoded.padCount == 0U || decoded.padCount > kPadsPerBank ||
        decoded.firstPad >= kPadCount || decoded.padCount > kPadCount - decoded.firstPad ||
        (play != 0U && decoded.sourceEndFrame <= decoded.sourceFrame))
        return false;
    decoded.play = play != 0U;
    request = decoded;
    return true;
}

[[nodiscard]] inline std::string encodeChopMidiPreviewRequest(
    const ChopMidiPreviewRequest& request)
{
    if (!request.active)
        return "CM1;0";
    std::string encoded = "CM1;1;" + std::to_string(request.firstPad) + ";" +
                          std::to_string(request.sourcePadCount) + ";" +
                          std::to_string(request.previewPadCount);
    for (std::uint32_t index = 0; index < request.previewPadCount &&
         index < kChopMidiPreviewPadCount; ++index) {
        encoded += ";" + std::to_string(request.sourceFrames[index]) + ";" +
                   std::to_string(request.sourceEndFrames[index]);
    }
    return encoded;
}

[[nodiscard]] inline bool decodeChopMidiPreviewRequest(
    const std::string_view encoded, ChopMidiPreviewRequest& request) noexcept
{
    if (!encoded.starts_with("CM1;"))
        return false;
    ChopMidiPreviewRequest decoded;
    std::size_t cursor = 4U;
    auto nextToken = [&encoded, &cursor](std::string_view& token) noexcept {
        if (cursor > encoded.size())
            return false;
        const auto end = encoded.find(';', cursor);
        token = encoded.substr(cursor, end == std::string_view::npos
            ? encoded.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? encoded.size() + 1U : end + 1U;
        return true;
    };
    std::string_view token;
    unsigned active = 0U;
    if (!nextToken(token) || !parseChopInteger(token, active) || active > 1U)
        return false;
    if (active == 0U) {
        if (cursor <= encoded.size())
            return false;
        request = decoded;
        return true;
    }
    if (!nextToken(token) || !parseChopInteger(token, decoded.firstPad) ||
        !nextToken(token) || !parseChopInteger(token, decoded.sourcePadCount) ||
        !nextToken(token) || !parseChopInteger(token, decoded.previewPadCount) ||
        decoded.sourcePadCount == 0U ||
        decoded.sourcePadCount > kChopMidiPreviewPadCount ||
        decoded.previewPadCount > kChopMidiPreviewPadCount ||
        decoded.firstPad >= kPadCount ||
        decoded.sourcePadCount > kPadCount - decoded.firstPad ||
        decoded.previewPadCount > kPadCount - decoded.firstPad)
        return false;
    for (std::uint32_t index = 0; index < decoded.previewPadCount; ++index) {
        if (!nextToken(token) ||
            !parseChopInteger(token, decoded.sourceFrames[index]) ||
            !nextToken(token) ||
            !parseChopInteger(token, decoded.sourceEndFrames[index]) ||
            decoded.sourceEndFrames[index] < decoded.sourceFrames[index])
            return false;
    }
    if (cursor <= encoded.size())
        return false;
    decoded.active = true;
    request = decoded;
    return true;
}

} // namespace midichopper::plugin
