#pragma once

#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"
#include "State/SampleMixerSettingsCodec.hpp"

#include <array>
#include <charconv>
#include <string>
#include <string_view>

namespace midichopper::plugin {

struct EditorSnapshotRequest {
    std::uint64_t sequence = 0U;
    std::uint32_t pad = 0U;
};

struct EditorSnapshotReply {
    EditorSnapshotRequest request{};
    sms::audio::WaveformSummary waveform{};
    sms::dsp::SamplePlaybackSettings playback{};
    sms::dsp::SampleMixerSettings mixer{};
};

[[nodiscard]] inline std::string encodeEditorSnapshotRequest(const EditorSnapshotRequest& request)
{
    return "ES1;" + std::to_string(request.sequence) + ";" + std::to_string(request.pad);
}

[[nodiscard]] inline bool decodeEditorSnapshotRequest(
    const std::string_view encoded, EditorSnapshotRequest& request) noexcept
{
    if (!encoded.starts_with("ES1;"))
        return false;
    const auto separator = encoded.find(';', 4U);
    if (separator == std::string_view::npos)
        return false;
    const auto sequence = encoded.substr(4U, separator - 4U);
    const auto pad = encoded.substr(separator + 1U);
    EditorSnapshotRequest decoded;
    const auto parsedSequence = std::from_chars(sequence.data(), sequence.data() + sequence.size(), decoded.sequence);
    const auto parsedPad = std::from_chars(pad.data(), pad.data() + pad.size(), decoded.pad);
    if (sequence.empty() || pad.empty() || parsedSequence.ec != std::errc{} ||
        parsedSequence.ptr != sequence.data() + sequence.size() || decoded.sequence == 0U ||
        parsedPad.ec != std::errc{} || parsedPad.ptr != pad.data() + pad.size() || decoded.pad >= kPadCount)
        return false;
    request = decoded;
    return true;
}

[[nodiscard]] inline std::string encodeEditorSnapshotReply(const EditorSnapshotReply& reply)
{
    return "ER1;" + std::to_string(reply.request.sequence) + ";" + std::to_string(reply.request.pad) +
        "|" + sms::audio::encodeWaveformSummary(reply.waveform) +
        "|" + sms::state::encodeSamplePlaybackSettings(reply.playback) +
        "|" + sms::state::encodeSampleMixerSettings(reply.mixer);
}

[[nodiscard]] inline bool decodeEditorSnapshotReply(
    const std::string_view encoded, EditorSnapshotReply& reply) noexcept
{
    if (!encoded.starts_with("ER1;"))
        return false;
    std::array<std::string_view, 4> parts{};
    std::size_t cursor = 0U;
    for (auto& part : parts) {
        if (cursor > encoded.size())
            return false;
        const auto end = encoded.find('|', cursor);
        part = encoded.substr(cursor, end == std::string_view::npos
            ? encoded.size() - cursor : end - cursor);
        cursor = end == std::string_view::npos ? encoded.size() + 1U : end + 1U;
    }
    EditorSnapshotReply decoded;
    // The request/reply envelope uses the same numeric fields.
    const auto header = parts[0];
    const auto separator = header.find(';', 4U);
    if (cursor <= encoded.size() || separator == std::string_view::npos)
        return false;
    const auto sequence = header.substr(4U, separator - 4U);
    const auto pad = header.substr(separator + 1U);
    const auto parsedSequence = std::from_chars(sequence.data(), sequence.data() + sequence.size(), decoded.request.sequence);
    const auto parsedPad = std::from_chars(pad.data(), pad.data() + pad.size(), decoded.request.pad);
    if (sequence.empty() || pad.empty() || parsedSequence.ec != std::errc{} ||
        parsedSequence.ptr != sequence.data() + sequence.size() || decoded.request.sequence == 0U ||
        parsedPad.ec != std::errc{} || parsedPad.ptr != pad.data() + pad.size() ||
        decoded.request.pad >= kPadCount ||
        !sms::audio::decodeWaveformSummary(parts[1], decoded.waveform) ||
        decoded.waveform.pad != decoded.request.pad)
        return false;
    std::array<char, 192> settings{};
    if (parts[2].size() >= settings.size() || parts[3].size() >= settings.size() ||
        parts[2].find('\0') != std::string_view::npos || parts[3].find('\0') != std::string_view::npos)
        return false;
    std::copy(parts[2].begin(), parts[2].end(), settings.begin());
    if (!sms::state::decodeSamplePlaybackSettings(settings.data(), decoded.playback))
        return false;
    settings.fill('\0');
    std::copy(parts[3].begin(), parts[3].end(), settings.begin());
    if (!sms::state::decodeSampleMixerSettings(settings.data(), decoded.mixer))
        return false;
    reply = decoded;
    return true;
}

} // namespace midichopper::plugin
