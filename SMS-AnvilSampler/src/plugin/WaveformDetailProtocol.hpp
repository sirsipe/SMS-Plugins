#pragma once

#include "Audio/WaveformSummary.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace midichopper::plugin {

struct WaveformDetailRequest {
    std::uint64_t sequence = 0;
    std::uint32_t firstPad = 0;
    std::uint32_t padCount = 0;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
};

struct WaveformDetailReply {
    WaveformDetailRequest request;
    sms::audio::WaveformSummary waveform;
};

inline std::string encodeWaveformDetailRequest(const WaveformDetailRequest& request)
{
    return "WD1;" + std::to_string(request.sequence) + ";" +
        std::to_string(request.firstPad) + ";" +
        std::to_string(request.padCount) + ";" +
        std::to_string(request.start) + ";" + std::to_string(request.end);
}

inline bool decodeWaveformDetailRequest(std::string_view encoded,
                                        WaveformDetailRequest& request) noexcept
{
    if (!encoded.starts_with("WD1;"))
        return false;
    encoded.remove_prefix(4);
    std::uint64_t fields[5]{};
    for (std::size_t index = 0; index < 5; ++index) {
        const auto separator = encoded.find(';');
        if ((index < 4 && separator == std::string_view::npos) ||
            (index == 4 && separator != std::string_view::npos))
            return false;
        const auto field = encoded.substr(0, separator);
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), fields[index]);
        if (field.empty() || parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size())
            return false;
        if (index < 4)
            encoded.remove_prefix(separator + 1U);
    }
    if (fields[1] > 63U || fields[2] == 0U || fields[2] > 3U ||
        fields[1] + fields[2] > 64U || fields[3] >= fields[4] ||
        fields[4] - fields[3] > UINT32_MAX)
        return false;
    request = {fields[0], static_cast<std::uint32_t>(fields[1]),
               static_cast<std::uint32_t>(fields[2]), fields[3], fields[4]};
    return true;
}

inline std::string encodeWaveformDetailReply(const WaveformDetailReply& reply)
{
    return encodeWaveformDetailRequest(reply.request) + ";" +
        sms::audio::encodeWaveformSummary(reply.waveform);
}

inline bool decodeWaveformDetailReply(const std::string_view encoded,
                                      WaveformDetailReply& reply) noexcept
{
    const auto marker = encoded.find(";WS2;");
    if (marker == std::string_view::npos ||
        !decodeWaveformDetailRequest(encoded.substr(0, marker), reply.request) ||
        !sms::audio::decodeWaveformSummary(encoded.substr(marker + 1U), reply.waveform))
        return false;
    return reply.waveform.pad == reply.request.firstPad &&
        reply.waveform.frames == reply.request.end - reply.request.start;
}

} // namespace midichopper::plugin
