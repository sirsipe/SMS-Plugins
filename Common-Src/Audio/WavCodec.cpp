#include "WavCodec.hpp"

#include "DSP/AdsrEnvelope.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace sms::audio {
namespace {

[[nodiscard]] std::uint16_t u16(const std::uint8_t* const data) noexcept
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

[[nodiscard]] std::uint32_t u32(const std::uint8_t* const data) noexcept
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

[[nodiscard]] bool idEquals(const std::uint8_t* const data, const char* const id) noexcept
{
    return std::memcmp(data, id, 4U) == 0;
}

[[nodiscard]] bool extensibleTailMatches(const std::uint8_t* const guid) noexcept
{
    static constexpr std::uint8_t tail[12]{
        0x00U, 0x00U, 0x10U, 0x00U, 0x80U, 0x00U,
        0x00U, 0xaaU, 0x00U, 0x38U, 0x9bU, 0x71U,
    };
    return std::memcmp(guid + 4U, tail, sizeof(tail)) == 0;
}

[[nodiscard]] float decodePcm(const std::uint8_t* const data,
                              const std::uint16_t bits) noexcept
{
    if (bits == 16U) {
        const auto value = std::bit_cast<std::int16_t>(u16(data));
        return static_cast<float>(value) / 32768.0f;
    }
    if (bits == 24U) {
        std::int32_t value = static_cast<std::int32_t>(data[0]) |
                             (static_cast<std::int32_t>(data[1]) << 8U) |
                             (static_cast<std::int32_t>(data[2]) << 16U);
        if ((value & 0x00800000) != 0)
            value |= ~0x00ffffff;
        return static_cast<float>(value) / 8388608.0f;
    }
    const auto value = std::bit_cast<std::int32_t>(u32(data));
    return static_cast<float>(static_cast<double>(value) / 2147483648.0);
}

[[nodiscard]] std::int16_t quantize(const float sample) noexcept
{
    const float limited = std::clamp(sample, -1.0f, 1.0f);
    if (limited <= -1.0f)
        return std::numeric_limits<std::int16_t>::min();
    return static_cast<std::int16_t>(std::lrint(limited * 32767.0f));
}

} // namespace

WavDecodeResult decodeWav(const std::span<const std::uint8_t> bytes,
                          const double maximumDurationSeconds) noexcept
{
    WavDecodeResult result;
    if (bytes.size() < 12U || !idEquals(bytes.data(), "RIFF") ||
        !idEquals(bytes.data() + 8U, "WAVE")) {
        result.error = WavError::malformed;
        return result;
    }
    const std::uint64_t riffEnd = static_cast<std::uint64_t>(u32(bytes.data() + 4U)) + 8U;
    if (riffEnd < 12U || riffEnd > bytes.size()) {
        result.error = WavError::malformed;
        return result;
    }

    const std::uint8_t* format = nullptr;
    std::uint32_t formatSize = 0U;
    const std::uint8_t* samples = nullptr;
    std::uint32_t sampleBytes = 0U;
    std::uint64_t offset = 12U;
    while (offset + 8U <= riffEnd) {
        const auto chunk = bytes.data() + offset;
        const std::uint32_t size = u32(chunk + 4U);
        const std::uint64_t dataOffset = offset + 8U;
        const std::uint64_t dataEnd = dataOffset + size;
        if (dataEnd < dataOffset || dataEnd > riffEnd) {
            result.error = WavError::malformed;
            return result;
        }
        if (idEquals(chunk, "fmt ")) {
            if (format != nullptr) {
                result.error = WavError::malformed;
                return result;
            }
            format = bytes.data() + dataOffset;
            formatSize = size;
        } else if (idEquals(chunk, "data")) {
            if (samples != nullptr) {
                result.error = WavError::malformed;
                return result;
            }
            samples = bytes.data() + dataOffset;
            sampleBytes = size;
        }
        offset = dataEnd + (size & 1U);
        if (offset > riffEnd) {
            result.error = WavError::malformed;
            return result;
        }
    }
    if (offset != riffEnd) {
        result.error = WavError::malformed;
        return result;
    }
    if (format == nullptr || formatSize < 16U || samples == nullptr) {
        result.error = WavError::malformed;
        return result;
    }

    std::uint16_t tag = u16(format);
    const std::uint16_t channels = u16(format + 2U);
    const std::uint32_t sampleRate = u32(format + 4U);
    const std::uint32_t byteRate = u32(format + 8U);
    const std::uint16_t blockAlign = u16(format + 12U);
    const std::uint16_t bits = u16(format + 14U);
    if (tag == 0xfffeU) {
        const std::uint16_t extensionSize = formatSize >= 18U ? u16(format + 16U) : 0U;
        if (formatSize < 40U || extensionSize < 22U ||
            static_cast<std::uint32_t>(extensionSize) + 18U > formatSize ||
            !extensibleTailMatches(format + 24U)) {
            result.error = WavError::unsupportedFormat;
            return result;
        }
        const std::uint16_t validBits = u16(format + 18U);
        if (validBits != bits) {
            result.error = WavError::unsupportedFormat;
            return result;
        }
        const std::uint32_t subtype = u32(format + 24U);
        if (subtype > std::numeric_limits<std::uint16_t>::max()) {
            result.error = WavError::unsupportedFormat;
            return result;
        }
        tag = static_cast<std::uint16_t>(subtype);
    }

    const bool pcm = tag == 1U && (bits == 16U || bits == 24U || bits == 32U);
    const bool floating = tag == 3U && bits == 32U;
    const std::uint32_t bytesPerSample = bits / 8U;
    const std::uint64_t expectedAlign = static_cast<std::uint64_t>(channels) * bytesPerSample;
    const std::uint64_t expectedByteRate = static_cast<std::uint64_t>(sampleRate) * expectedAlign;
    if ((!pcm && !floating) || (channels != 1U && channels != 2U) ||
        sampleRate == 0U || sampleRate > 384000U || bits % 8U != 0U ||
        expectedAlign != blockAlign || expectedByteRate != byteRate ||
        expectedByteRate > std::numeric_limits<std::uint32_t>::max()) {
        result.error = WavError::unsupportedFormat;
        return result;
    }
    if (sampleBytes == 0U) {
        result.error = WavError::empty;
        return result;
    }
    if (sampleBytes % blockAlign != 0U) {
        result.error = WavError::malformed;
        return result;
    }
    const std::uint64_t frames = sampleBytes / blockAlign;
    if (frames == 0U) {
        result.error = WavError::empty;
        return result;
    }
    if (!std::isfinite(maximumDurationSeconds) || maximumDurationSeconds <= 0.0 ||
        static_cast<double>(frames) / sampleRate > maximumDurationSeconds) {
        result.error = WavError::tooLong;
        return result;
    }
    if (frames > std::numeric_limits<std::uint32_t>::max() ||
        frames > std::numeric_limits<std::size_t>::max() / 2U) {
        result.error = WavError::tooLarge;
        return result;
    }

    try {
        result.audio.stereo.resize(static_cast<std::size_t>(frames) * 2U);
    } catch (...) {
        result.error = WavError::tooLarge;
        return result;
    }
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        const auto input = samples + frame * blockAlign;
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
            const auto valueBytes = input + static_cast<std::size_t>(channel) * bytesPerSample;
            const float value = floating
                ? std::bit_cast<float>(u32(valueBytes)) : decodePcm(valueBytes, bits);
            if (!std::isfinite(value)) {
                result.audio = {};
                result.error = WavError::nonFiniteSample;
                return result;
            }
            result.audio.stereo[static_cast<std::size_t>(frame) * 2U + channel] = value;
        }
        if (channels == 1U)
            result.audio.stereo[static_cast<std::size_t>(frame) * 2U + 1U] =
                result.audio.stereo[static_cast<std::size_t>(frame) * 2U];
    }
    result.audio.sampleRate = sampleRate;
    result.audio.frames = static_cast<std::uint32_t>(frames);
    return result;
}

bool encodeStereoPcm16Wav(const WavAudio& audio,
                          std::vector<std::uint8_t>& destination) noexcept
{
    if (audio.sampleRate == 0U || audio.sampleRate > 384000U || audio.frames == 0U ||
        audio.stereo.size() < static_cast<std::size_t>(audio.frames) * 2U)
        return false;
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(audio.frames) * 4U;
    if (dataBytes + 36U > std::numeric_limits<std::uint32_t>::max())
        return false;
    try {
        destination.clear();
        destination.reserve(static_cast<std::size_t>(dataBytes) + 44U);
        destination.insert(destination.end(), {'R', 'I', 'F', 'F'});
        appendU32(destination, static_cast<std::uint32_t>(dataBytes + 36U));
        destination.insert(destination.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        appendU32(destination, 16U);
        appendU16(destination, 1U);
        appendU16(destination, 2U);
        appendU32(destination, audio.sampleRate);
        appendU32(destination, audio.sampleRate * 4U);
        appendU16(destination, 4U);
        appendU16(destination, 16U);
        destination.insert(destination.end(), {'d', 'a', 't', 'a'});
        appendU32(destination, static_cast<std::uint32_t>(dataBytes));
        for (std::size_t index = 0; index < static_cast<std::size_t>(audio.frames) * 2U;
             ++index) {
            if (!std::isfinite(audio.stereo[index])) {
                destination.clear();
                return false;
            }
            appendU16(destination, static_cast<std::uint16_t>(quantize(audio.stereo[index])));
        }
    } catch (...) {
        destination.clear();
        return false;
    }
    return true;
}

WavAudio renderProcessedStereo(const WavAudio& source,
                               const dsp::SamplePlaybackSettings& requested,
                               const dsp::SampleMixerSettings& requestedMixer) noexcept
{
    WavAudio output;
    if (source.sampleRate == 0U || source.frames == 0U ||
        source.stereo.size() < static_cast<std::size_t>(source.frames) * 2U)
        return output;
    auto settings = dsp::sanitize(requested);
    const std::uint32_t start = std::min(source.frames - 1U, static_cast<std::uint32_t>(
        std::floor(static_cast<double>(settings.start) * source.frames)));
    const std::uint32_t end = std::clamp(static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(settings.end) * source.frames)), start + 1U, source.frames);
    const std::uint32_t sourceFrames = end - start;
    const auto mixer = dsp::sanitize(requestedMixer);
    const double ratio = dsp::tuneRatio(mixer);
    const double requestedFrames = std::ceil(static_cast<double>(sourceFrames) / ratio);
    if (!std::isfinite(requestedFrames) || requestedFrames < 1.0 ||
        requestedFrames > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return output;
    const auto frames = static_cast<std::uint32_t>(requestedFrames);
    const float seconds = static_cast<float>(frames) / static_cast<float>(source.sampleRate);
    settings.releaseSeconds = std::min(settings.releaseSeconds, seconds * 0.5f);

    output.sampleRate = source.sampleRate;
    output.frames = frames;
    try {
        output.stereo.resize(static_cast<std::size_t>(frames) * 2U);
    } catch (...) {
        return {};
    }
    dsp::AdsrEnvelope envelope;
    envelope.configure(source.sampleRate, settings);
    envelope.noteOn();
    const float mixerGain = dsp::sampleGain(mixer);
    const float leftGain = mixerGain * (mixer.pan > 0.0f ? 1.0f - mixer.pan : 1.0f);
    const float rightGain = mixerGain * (mixer.pan < 0.0f ? 1.0f + mixer.pan : 1.0f);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const std::uint32_t remaining = frames - frame;
        if (!envelope.releasing() && envelope.releaseFrames() > 0U &&
            remaining <= envelope.releaseFrames())
            envelope.noteOff();
        const float gain = envelope.next();
        const double position = std::min(static_cast<double>(sourceFrames - 1U),
                                         static_cast<double>(frame) * ratio);
        const auto first = static_cast<std::uint32_t>(position);
        const auto second = std::min(first + 1U, sourceFrames - 1U);
        const float fraction = static_cast<float>(position - first);
        const std::size_t firstInput = static_cast<std::size_t>(start + first) * 2U;
        const std::size_t secondInput = static_cast<std::size_t>(start + second) * 2U;
        const std::size_t target = static_cast<std::size_t>(frame) * 2U;
        output.stereo[target] = (source.stereo[firstInput] * (1.0f - fraction) +
                                 source.stereo[secondInput] * fraction) * gain * leftGain;
        output.stereo[target + 1U] = (source.stereo[firstInput + 1U] * (1.0f - fraction) +
                                      source.stereo[secondInput + 1U] * fraction) *
                                     gain * rightGain;
    }
    return output;
}

const char* wavErrorMessage(const WavError error) noexcept
{
    switch (error) {
    case WavError::none: return "WAV loaded";
    case WavError::malformed: return "Malformed or truncated WAV file";
    case WavError::unsupportedFormat: return "Unsupported WAV format";
    case WavError::empty: return "WAV file contains no audio";
    case WavError::tooLong: return "WAV exceeds the eight-minute sample limit";
    case WavError::nonFiniteSample: return "WAV contains a non-finite float sample";
    case WavError::tooLarge: return "WAV file is too large";
    }
    return "WAV error";
}

bool hasWavExtension(const std::string_view path) noexcept
{
    if (path.size() < 4U)
        return false;
    const auto tail = path.substr(path.size() - 4U);
    return tail[0] == '.' && (tail[1] == 'w' || tail[1] == 'W') &&
           (tail[2] == 'a' || tail[2] == 'A') &&
           (tail[3] == 'v' || tail[3] == 'V');
}

bool hasAnyExtension(const std::string_view path) noexcept
{
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    return dot != std::string_view::npos && dot + 1U < path.size() &&
           (slash == std::string_view::npos || dot > slash + 1U);
}

} // namespace sms::audio
