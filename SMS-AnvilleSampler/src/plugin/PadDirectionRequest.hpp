#pragma once

#include "Configuration.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"
#include "State/SampleMixerSettingsCodec.hpp"

#include <charconv>
#include <string>
#include <string_view>

namespace midichopper::plugin {

inline constexpr const char* kPadDirectionRequestKey = "pad_direction_request";

/** One editor action publishes mirrored cuts and the desired orientation together. */
struct PadDirectionRequest {
    std::uint32_t pad = kPadCount;
    sms::dsp::SamplePlaybackSettings playback{};
    sms::dsp::SampleMixerSettings mixer{};
};

[[nodiscard]] inline std::string encodePadDirectionRequest(const PadDirectionRequest& request)
{
    return "DR1;" + std::to_string(request.pad) + "|" +
        sms::state::encodeSamplePlaybackSettings(request.playback) + "|" +
        sms::state::encodeSampleMixerSettings(request.mixer);
}

[[nodiscard]] inline bool decodePadDirectionRequest(
    const std::string_view text, PadDirectionRequest& destination)
{
    if (text.size() > 512U || !text.starts_with("DR1;") ||
        text.find('\0') != std::string_view::npos)
        return false;
    const auto first = text.find('|', 4U);
    const auto second = first == std::string_view::npos ? first : text.find('|', first + 1U);
    if (second == std::string_view::npos || text.find('|', second + 1U) != std::string_view::npos)
        return false;
    PadDirectionRequest result;
    const auto parsed = std::from_chars(text.data() + 4U, text.data() + first, result.pad);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + first || result.pad >= kPadCount)
        return false;
    const std::string playback(text.substr(first + 1U, second - first - 1U));
    const std::string mixer(text.substr(second + 1U));
    if (!sms::state::decodeSamplePlaybackSettings(playback.c_str(), result.playback) ||
        !sms::state::decodeSampleMixerSettings(mixer.c_str(), result.mixer))
        return false;
    destination = result;
    return true;
}

} // namespace midichopper::plugin
