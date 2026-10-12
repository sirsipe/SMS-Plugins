#pragma once

#include "Configuration.hpp"
#include "Audio/WaveformSummary.hpp"

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
    std::uint64_t sequence = 0;
    std::array<std::uint64_t, kPadsPerBank> expectedGenerations{};
    bool revisionChecked = false;
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
    std::string encoded = request.revisionChecked
        ? "CH2;" + std::to_string(request.sequence) + ";"
        : "CH1;";
    encoded += std::to_string(request.firstPad) + ";" + std::to_string(request.padCount);
    if (request.revisionChecked) {
        for (std::uint32_t index = 0; index < request.padCount && index < kPadsPerBank; ++index)
            encoded += ";" + std::to_string(request.expectedGenerations[index]);
    }
    for (std::uint32_t index = 0; index + 1U < request.padCount && index < request.boundaryOffsets.size(); ++index)
        encoded += ";" + std::to_string(request.boundaryOffsets[index]);
    return encoded;
}

[[nodiscard]] inline bool decodeChopApplyRequest(
    const std::string_view encoded, ChopApplyRequest& request) noexcept
{
    if (!encoded.starts_with("CH1;") && !encoded.starts_with("CH2;"))
        return false;
    ChopApplyRequest decoded;
    decoded.revisionChecked = encoded.starts_with("CH2;");
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
    if (decoded.revisionChecked &&
        (!nextToken(token) || !parseChopInteger(token, decoded.sequence) || decoded.sequence == 0U))
        return false;
    if (!nextToken(token) || !parseChopInteger(token, decoded.firstPad) ||
        !nextToken(token) || !parseChopInteger(token, decoded.padCount) ||
        decoded.padCount < 2U || decoded.padCount > kPadsPerBank ||
        decoded.firstPad >= kPadCount || decoded.padCount > kPadCount - decoded.firstPad)
        return false;
    if (decoded.revisionChecked) {
        for (std::uint32_t index = 0; index < decoded.padCount; ++index) {
            if (!nextToken(token) || !parseChopInteger(token, decoded.expectedGenerations[index]))
                return false;
        }
    }
    for (std::uint32_t index = 0; index + 1U < decoded.padCount; ++index) {
        if (!nextToken(token) || !parseChopInteger(token, decoded.boundaryOffsets[index]))
            return false;
    }
    if (cursor <= encoded.size())
        return false;
    request = decoded;
    return true;
}

inline constexpr std::size_t kChopSnapshotPadCount = 3U;

struct ChopSnapshotRequest {
    std::uint64_t sequence = 0U;
    std::uint32_t firstPad = 0U;
    std::uint32_t padCount = kChopSnapshotPadCount;
};

struct ChopSnapshotReply {
    ChopSnapshotRequest request{};
    std::array<std::uint64_t, kPadsPerBank> generations{};
    std::array<sms::audio::WaveformSummary, kChopSnapshotPadCount> waveforms{};
};

[[nodiscard]] inline std::string encodeChopSnapshotRequest(const ChopSnapshotRequest& request)
{
    return "CQ1;" + std::to_string(request.sequence) + ";" +
        std::to_string(request.firstPad) + ";" + std::to_string(request.padCount);
}

[[nodiscard]] inline bool decodeChopSnapshotRequest(
    const std::string_view encoded, ChopSnapshotRequest& request) noexcept
{
    if (!encoded.starts_with("CQ1;"))
        return false;
    std::array<std::string_view, 3> fields{};
    std::size_t cursor = 4U;
    for (auto& field : fields) {
        if (cursor > encoded.size())
            return false;
        const auto end = encoded.find(';', cursor);
        field = encoded.substr(cursor, end == std::string_view::npos
            ? encoded.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? encoded.size() + 1U : end + 1U;
    }
    ChopSnapshotRequest decoded;
    if (cursor <= encoded.size() || !parseChopInteger(fields[0], decoded.sequence) ||
        decoded.sequence == 0U || !parseChopInteger(fields[1], decoded.firstPad) ||
        !parseChopInteger(fields[2], decoded.padCount) ||
        decoded.padCount != kChopSnapshotPadCount ||
        decoded.firstPad >= kPadCount || decoded.padCount > kPadCount - decoded.firstPad)
        return false;
    request = decoded;
    return true;
}

[[nodiscard]] inline std::string encodeChopSnapshotReply(const ChopSnapshotReply& reply)
{
    std::string encoded = "CS1;" + std::to_string(reply.request.sequence) + ";" +
        std::to_string(reply.request.firstPad) + ";" + std::to_string(reply.request.padCount);
    for (std::size_t index = 0; index < kChopSnapshotPadCount; ++index)
        encoded += ";" + std::to_string(reply.generations[index]);
    for (const auto& waveform : reply.waveforms)
        encoded += "|" + sms::audio::encodeWaveformSummary(waveform);
    return encoded;
}

[[nodiscard]] inline bool decodeChopSnapshotReply(
    const std::string_view encoded, ChopSnapshotReply& reply) noexcept
{
    if (!encoded.starts_with("CS1;"))
        return false;
    const auto payload = encoded.find('|');
    if (payload == std::string_view::npos)
        return false;
    std::array<std::string_view, 6> fields{};
    std::size_t cursor = 4U;
    for (auto& field : fields) {
        if (cursor > payload)
            return false;
        const auto separator = encoded.find(';', cursor);
        const auto end = separator == std::string_view::npos || separator > payload
            ? payload : separator;
        field = encoded.substr(cursor, end - cursor);
        cursor = end + 1U;
    }
    ChopSnapshotReply decoded;
    if (cursor != payload + 1U || !parseChopInteger(fields[0], decoded.request.sequence) ||
        decoded.request.sequence == 0U || !parseChopInteger(fields[1], decoded.request.firstPad) ||
        !parseChopInteger(fields[2], decoded.request.padCount) ||
        decoded.request.padCount != kChopSnapshotPadCount ||
        decoded.request.firstPad >= kPadCount ||
        decoded.request.padCount > kPadCount - decoded.request.firstPad)
        return false;
    for (std::size_t index = 0; index < kChopSnapshotPadCount; ++index) {
        if (!parseChopInteger(fields[index + 3U], decoded.generations[index]) ||
            cursor > encoded.size())
            return false;
        const auto end = encoded.find('|', cursor);
        const auto waveform = encoded.substr(cursor, end == std::string_view::npos
            ? encoded.size() - cursor : end - cursor);
        if (!sms::audio::decodeWaveformSummary(waveform, decoded.waveforms[index]) ||
            decoded.waveforms[index].pad != decoded.request.firstPad + index)
            return false;
        cursor = end == std::string_view::npos ? encoded.size() + 1U : end + 1U;
    }
    if (cursor <= encoded.size())
        return false;
    reply = decoded;
    return true;
}

struct ChopApplyStatus {
    std::uint64_t sequence = 0U;
    bool success = false;
    std::string_view message{};
};

[[nodiscard]] inline std::string encodeChopStatus(
    const std::uint64_t sequence, const bool success, const std::string_view message = {})
{
    return "CH2;" + std::to_string(sequence) + (success ? ";OK" : ";ERROR;" + std::string(message));
}

[[nodiscard]] inline std::string encodeChopSnapshotError(
    const std::uint64_t sequence, const std::string_view message)
{
    return "CS1;" + std::to_string(sequence) + ";ERROR;" + std::string(message);
}

[[nodiscard]] inline bool decodeChopSnapshotError(
    const std::string_view encoded, ChopApplyStatus& status) noexcept
{
    if (!encoded.starts_with("CS1;"))
        return false;
    const auto separator = encoded.find(';', 4U);
    ChopApplyStatus decoded;
    if (separator == std::string_view::npos ||
        !parseChopInteger(encoded.substr(4U, separator - 4U), decoded.sequence) ||
        decoded.sequence == 0U)
        return false;
    const auto result = encoded.substr(separator + 1U);
    if (!result.starts_with("ERROR;") || result.size() <= 6U)
        return false;
    decoded.message = result.substr(6U);
    status = decoded;
    return true;
}

[[nodiscard]] inline bool decodeChopApplyStatus(
    const std::string_view encoded, ChopApplyStatus& status) noexcept
{
    if (!encoded.starts_with("CH2;"))
        return false;
    const auto separator = encoded.find(';', 4U);
    ChopApplyStatus decoded;
    if (separator == std::string_view::npos ||
        !parseChopInteger(encoded.substr(4U, separator - 4U), decoded.sequence) ||
        decoded.sequence == 0U)
        return false;
    const auto result = encoded.substr(separator + 1U);
    if (result == "OK")
        decoded.success = true;
    else if (result.starts_with("ERROR;") && result.size() > 6U)
        decoded.message = result.substr(6U);
    else
        return false;
    status = decoded;
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
