#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sms::audio {

inline constexpr std::size_t kWaveformBins = 128U;

struct WaveformSummary {
    std::uint32_t pad = 0U;
    std::uint32_t frames = 0U;
    double sampleRate = 48000.0;
    std::array<float, kWaveformBins> minimum{};
    std::array<float, kWaveformBins> maximum{};
};

[[nodiscard]] inline WaveformSummary summarizeStereo(const std::uint32_t pad,
                                                       const float* const interleaved,
                                                       const std::uint32_t frames,
                                                       const double sampleRate = 48000.0) noexcept
{
    WaveformSummary result;
    result.pad = pad;
    result.frames = frames;
    result.sampleRate = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
    if (interleaved == nullptr || frames == 0U)
        return result;

    for (std::size_t bin = 0; bin < kWaveformBins; ++bin) {
        const auto first = static_cast<std::uint32_t>(bin * frames / kWaveformBins);
        const auto last = std::max(first + 1U,
            static_cast<std::uint32_t>((bin + 1U) * frames / kWaveformBins));
        float low = 1.0f;
        float high = -1.0f;
        for (std::uint32_t frame = first; frame < std::min(last, frames); ++frame) {
            const auto offset = static_cast<std::size_t>(frame) * 2U;
            low = std::min({low, interleaved[offset], interleaved[offset + 1U]});
            high = std::max({high, interleaved[offset], interleaved[offset + 1U]});
        }
        result.minimum[bin] = std::clamp(low, -1.0f, 1.0f);
        result.maximum[bin] = std::clamp(high, -1.0f, 1.0f);
    }
    return result;
}

namespace detail {
inline void appendHex16(std::string& destination, const std::uint16_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    destination.push_back(digits[(value >> 12U) & 0x0fU]);
    destination.push_back(digits[(value >> 8U) & 0x0fU]);
    destination.push_back(digits[(value >> 4U) & 0x0fU]);
    destination.push_back(digits[value & 0x0fU]);
}

[[nodiscard]] inline std::uint16_t quantize(const float value) noexcept
{
    const auto sample = static_cast<std::int16_t>(std::lrint(
        std::clamp(std::isfinite(value) ? value : 0.0f, -1.0f, 1.0f) * 32767.0f));
    return static_cast<std::uint16_t>(sample);
}

[[nodiscard]] inline bool readHex16(const std::string_view text, std::uint16_t& value) noexcept
{
    if (text.size() != 4U)
        return false;
    unsigned parsed = 0U;
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size())
        return false;
    value = static_cast<std::uint16_t>(parsed);
    return true;
}
} // namespace detail

/** Compact text transport intended for plug-in DSP-to-UI state messages. */
[[nodiscard]] inline std::string encodeWaveformSummary(const WaveformSummary& summary)
{
    const auto sampleRate = static_cast<std::uint32_t>(std::clamp(
        std::llround(summary.sampleRate), 2LL, 768000LL));
    std::string encoded = "WS2;" + std::to_string(summary.pad) + ";" +
                          std::to_string(summary.frames) + ";" +
                          std::to_string(sampleRate) + ";";
    encoded.reserve(encoded.size() + kWaveformBins * 8U);
    for (std::size_t bin = 0; bin < kWaveformBins; ++bin) {
        detail::appendHex16(encoded, detail::quantize(summary.minimum[bin]));
        detail::appendHex16(encoded, detail::quantize(summary.maximum[bin]));
    }
    return encoded;
}

[[nodiscard]] inline bool decodeWaveformSummary(const std::string_view encoded,
                                                 WaveformSummary& destination) noexcept
{
    if (!encoded.starts_with("WS2;"))
        return false;
    const auto padEnd = encoded.find(';', 4U);
    const auto framesEnd = padEnd == std::string_view::npos
        ? std::string_view::npos : encoded.find(';', padEnd + 1U);
    const auto sampleRateEnd = framesEnd == std::string_view::npos
        ? std::string_view::npos : encoded.find(';', framesEnd + 1U);
    if (padEnd == std::string_view::npos || framesEnd == std::string_view::npos ||
        sampleRateEnd == std::string_view::npos)
        return false;

    unsigned pad = 0U;
    unsigned frames = 0U;
    unsigned sampleRate = 0U;
    const auto padText = encoded.substr(4U, padEnd - 4U);
    const auto framesText = encoded.substr(padEnd + 1U, framesEnd - padEnd - 1U);
    const auto sampleRateText = encoded.substr(framesEnd + 1U, sampleRateEnd - framesEnd - 1U);
    const auto parsedPad = std::from_chars(padText.data(), padText.data() + padText.size(), pad);
    const auto parsedFrames = std::from_chars(framesText.data(), framesText.data() + framesText.size(), frames);
    const auto parsedSampleRate = std::from_chars(sampleRateText.data(),
                                                  sampleRateText.data() + sampleRateText.size(),
                                                  sampleRate);
    const auto payload = encoded.substr(sampleRateEnd + 1U);
    if (parsedPad.ec != std::errc{} || parsedPad.ptr != padText.data() + padText.size() ||
        parsedFrames.ec != std::errc{} || parsedFrames.ptr != framesText.data() + framesText.size() ||
        parsedSampleRate.ec != std::errc{} ||
        parsedSampleRate.ptr != sampleRateText.data() + sampleRateText.size() ||
        sampleRate <= 1U || sampleRate > 768000U ||
        payload.size() != kWaveformBins * 8U)
        return false;

    WaveformSummary decoded;
    decoded.pad = static_cast<std::uint32_t>(pad);
    decoded.frames = static_cast<std::uint32_t>(frames);
    decoded.sampleRate = static_cast<double>(sampleRate);
    for (std::size_t bin = 0; bin < kWaveformBins; ++bin) {
        std::uint16_t low = 0U;
        std::uint16_t high = 0U;
        if (!detail::readHex16(payload.substr(bin * 8U, 4U), low) ||
            !detail::readHex16(payload.substr(bin * 8U + 4U, 4U), high))
            return false;
        decoded.minimum[bin] = static_cast<float>(static_cast<std::int16_t>(low)) / 32767.0f;
        decoded.maximum[bin] = static_cast<float>(static_cast<std::int16_t>(high)) / 32767.0f;
    }
    destination = decoded;
    return true;
}

} // namespace sms::audio
