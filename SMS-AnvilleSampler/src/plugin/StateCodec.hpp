#pragma once

#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "SamplerEngine.hpp"

#include <cstdint>
#include <string>

namespace midichopper::plugin {

/**
 * Project-state representation for a pad. The SSP1 container writes version 2:
 * interleaved little-endian IEEE 754 float32, Base64 encoded with a payload CRC.
 * Finite sample bits, including headroom and signed zero, are preserved exactly.
 * Version 1 signed PCM16 remains readable with its original conversion. Older
 * binaries cannot read version 2. Both versions store the source sample rate
 * instead of inferring it from the current project. Control-thread use only.
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

[[nodiscard]] std::string encodeMixerSettings(
    const sms::dsp::SampleMixerSettings& settings);
[[nodiscard]] bool decodeMixerSettings(
    const char* encoded, sms::dsp::SampleMixerSettings& destination) noexcept;

} // namespace midichopper::plugin
