#pragma once

#include "DSP/SamplePlaybackSettings.hpp"
#include "SamplerEngine.hpp"

#include <cstdint>
#include <string>

namespace midichopper::plugin {

/**
 * Project-state representation for a pad. Audio uses interleaved signed 16-bit
 * PCM before Base64 encoding: half the size of float32 and portable across
 * LV2, VST3, and future wrappers. The source sample rate is deliberately part
 * of the payload, rather than being inferred from the current project.
 */
struct DecodedPadState {
    midichopper::PadData pad;
    std::uint32_t sourceSampleRate = 0;
};

[[nodiscard]] std::string encodePadState(const midichopper::PadData& pad,
                                         std::uint32_t sourceSampleRate);
[[nodiscard]] bool decodePadState(const char* encoded,
                                  DecodedPadState& destination) noexcept;

[[nodiscard]] std::string encodePlaybackSettings(
    const sms::dsp::SamplePlaybackSettings& settings);
[[nodiscard]] bool decodePlaybackSettings(
    const char* encoded, sms::dsp::SamplePlaybackSettings& destination) noexcept;

} // namespace midichopper::plugin
