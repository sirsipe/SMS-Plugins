#pragma once

#include "DSP/SamplePlaybackSettings.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sms::audio {

inline constexpr double kMaximumWavDurationSeconds = 30.0;

enum class WavError : std::uint8_t {
    none,
    malformed,
    unsupportedFormat,
    empty,
    tooLong,
    nonFiniteSample,
    tooLarge,
};

struct WavAudio {
    std::uint32_t sampleRate = 0;
    std::uint32_t frames = 0;
    std::vector<float> stereo;
};

struct WavDecodeResult {
    WavAudio audio;
    WavError error = WavError::none;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == WavError::none;
    }
};

[[nodiscard]] WavDecodeResult decodeWav(
    std::span<const std::uint8_t> bytes,
    double maximumDurationSeconds = kMaximumWavDurationSeconds) noexcept;

[[nodiscard]] bool encodeStereoPcm16Wav(
    const WavAudio& audio, std::vector<std::uint8_t>& destination) noexcept;

[[nodiscard]] WavAudio renderProcessedStereo(
    const WavAudio& source, const dsp::SamplePlaybackSettings& settings) noexcept;

[[nodiscard]] const char* wavErrorMessage(WavError error) noexcept;
[[nodiscard]] bool hasWavExtension(std::string_view path) noexcept;
[[nodiscard]] bool hasAnyExtension(std::string_view path) noexcept;

} // namespace sms::audio
