#include "StateCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace midichopper::plugin {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'S', 'S', 'P', '1'}};
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kChannels = 2;
constexpr std::size_t kHeaderBytes = 24;
// 30 seconds at 384 kHz. This is intentionally a restore limit, not a UI
// setting: malformed project state must never request unbounded memory.
constexpr std::uint32_t kMaximumFrames = 11'520'000;

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    for (std::uint32_t shift = 0; shift < 32; shift += 8)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}

[[nodiscard]] std::uint16_t readU16(const std::uint8_t* const data) noexcept
{
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(const std::uint8_t* const data) noexcept
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* const data,
                                  const std::size_t size) noexcept
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

[[nodiscard]] std::string base64Encode(const std::vector<std::uint8_t>& input)
{
    std::string encoded;
    encoded.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t index = 0; index < input.size(); index += 3U) {
        const std::uint32_t first = input[index];
        const std::uint32_t second = index + 1U < input.size() ? input[index + 1U] : 0U;
        const std::uint32_t third = index + 2U < input.size() ? input[index + 2U] : 0U;
        const std::uint32_t group = (first << 16U) | (second << 8U) | third;
        encoded.push_back(kBase64Alphabet[(group >> 18U) & 0x3fU]);
        encoded.push_back(kBase64Alphabet[(group >> 12U) & 0x3fU]);
        encoded.push_back(index + 1U < input.size() ? kBase64Alphabet[(group >> 6U) & 0x3fU] : '=');
        encoded.push_back(index + 2U < input.size() ? kBase64Alphabet[group & 0x3fU] : '=');
    }
    return encoded;
}

[[nodiscard]] int base64Value(const char character) noexcept
{
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

[[nodiscard]] bool base64Decode(const std::string_view encoded,
                                std::vector<std::uint8_t>& output) noexcept
{
    if (encoded.empty() || encoded.size() % 4U != 0U)
        return false;

    const std::size_t padding = encoded.back() == '=' ?
        (encoded.size() > 1U && encoded[encoded.size() - 2U] == '=' ? 2U : 1U) : 0U;
    if (padding > 2U)
        return false;

    const std::size_t decodedSize = encoded.size() / 4U * 3U - padding;
    if (decodedSize > kHeaderBytes + static_cast<std::size_t>(kMaximumFrames) * 4U)
        return false;

    try {
        output.clear();
        output.reserve(decodedSize);
    } catch (...) {
        return false;
    }

    for (std::size_t index = 0; index < encoded.size(); index += 4U) {
        const bool finalGroup = index + 4U == encoded.size();
        const char c2 = encoded[index + 2U];
        const char c3 = encoded[index + 3U];
        if ((c2 == '=' || c3 == '=') && !finalGroup)
            return false;
        if (c2 == '=' && c3 != '=')
            return false;

        const int a = base64Value(encoded[index]);
        const int b = base64Value(encoded[index + 1U]);
        const int c = c2 == '=' ? 0 : base64Value(c2);
        const int d = c3 == '=' ? 0 : base64Value(c3);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return false;

        const std::uint32_t group = (static_cast<std::uint32_t>(a) << 18U) |
                                    (static_cast<std::uint32_t>(b) << 12U) |
                                    (static_cast<std::uint32_t>(c) << 6U) |
                                    static_cast<std::uint32_t>(d);
        output.push_back(static_cast<std::uint8_t>((group >> 16U) & 0xffU));
        if (c2 != '=') output.push_back(static_cast<std::uint8_t>((group >> 8U) & 0xffU));
        if (c3 != '=') output.push_back(static_cast<std::uint8_t>(group & 0xffU));
    }
    return output.size() == decodedSize;
}

[[nodiscard]] std::int16_t quantize(const float sample) noexcept
{
    const float limited = std::clamp(std::isfinite(sample) ? sample : 0.0f, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lrint(limited * 32767.0f));
}

} // namespace

std::string encodePadState(const midichopper::PadData& pad, const std::uint32_t sourceSampleRate)
{
    const std::size_t expectedSamples = static_cast<std::size_t>(pad.frames) * 2U;
    if (sourceSampleRate == 0U || pad.frames > kMaximumFrames || pad.stereo.size() != expectedSamples)
        return {};

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderBytes + expectedSamples * 2U);
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    appendU16(bytes, kVersion);
    appendU16(bytes, kChannels);
    appendU32(bytes, sourceSampleRate);
    appendU32(bytes, pad.frames);
    appendU32(bytes, 0U); // payload byte length, filled below
    appendU32(bytes, 0U); // checksum, filled below

    for (const float sample : pad.stereo) {
        const std::uint16_t pcm = static_cast<std::uint16_t>(quantize(sample));
        appendU16(bytes, pcm);
    }

    const std::uint32_t payloadBytes = static_cast<std::uint32_t>(bytes.size() - kHeaderBytes);
    const std::uint32_t checksum = crc32(bytes.data() + kHeaderBytes, payloadBytes);
    for (unsigned index = 0; index < 4; ++index) {
        bytes[16U + index] = static_cast<std::uint8_t>((payloadBytes >> (index * 8U)) & 0xffU);
        bytes[20U + index] = static_cast<std::uint8_t>((checksum >> (index * 8U)) & 0xffU);
    }
    return base64Encode(bytes);
}

bool decodePadState(const char* const encoded, DecodedPadState& destination) noexcept
{
    if (encoded == nullptr)
        return false;

    std::vector<std::uint8_t> bytes;
    if (!base64Decode(encoded, bytes) || bytes.size() < kHeaderBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        readU16(bytes.data() + 4U) != kVersion || readU16(bytes.data() + 6U) != kChannels)
        return false;

    const std::uint32_t sourceSampleRate = readU32(bytes.data() + 8U);
    const std::uint32_t frames = readU32(bytes.data() + 12U);
    const std::uint32_t payloadBytes = readU32(bytes.data() + 16U);
    const std::uint32_t expectedChecksum = readU32(bytes.data() + 20U);
    const std::size_t expectedBytes = static_cast<std::size_t>(frames) * 4U;
    if (sourceSampleRate == 0U || frames > kMaximumFrames || payloadBytes != expectedBytes ||
        bytes.size() != kHeaderBytes + expectedBytes ||
        crc32(bytes.data() + kHeaderBytes, expectedBytes) != expectedChecksum)
        return false;

    try {
        midichopper::PadData restored;
        restored.sampleRate = static_cast<double>(sourceSampleRate);
        restored.frames = frames;
        restored.stereo.resize(static_cast<std::size_t>(frames) * 2U);
        float peak = 0.0f;
        double sumSquares = 0.0;
        for (std::size_t index = 0; index < restored.stereo.size(); ++index) {
            const auto pcm = static_cast<std::int16_t>(readU16(bytes.data() + kHeaderBytes + index * 2U));
            const float sample = static_cast<float>(pcm) / 32767.0f;
            restored.stereo[index] = sample;
            peak = std::max(peak, std::abs(sample));
            sumSquares += static_cast<double>(sample) * sample;
        }
        restored.peak = peak;
        restored.rms = restored.stereo.empty() ? 0.0f :
            static_cast<float>(std::sqrt(sumSquares / static_cast<double>(restored.stereo.size())));
        destination.pad = std::move(restored);
        destination.sourceSampleRate = sourceSampleRate;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace midichopper::plugin
