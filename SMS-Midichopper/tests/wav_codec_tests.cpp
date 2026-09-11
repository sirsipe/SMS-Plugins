#include "Audio/WavCodec.hpp"
#include "Audio/RealtimeAccessGate.hpp"
#include "PadFileActionProtocol.hpp"
#include "PadFileActions.hpp"

#include <bit>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void setU32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
            const std::uint32_t value)
{
    for (std::size_t index = 0; index < 4U; ++index)
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

std::vector<std::uint8_t> wav(const std::uint16_t tag, const std::uint16_t channels,
                              const std::uint16_t bits,
                              const std::vector<std::uint8_t>& samples,
                              const bool extensible = false)
{
    constexpr std::uint32_t rate = 48000U;
    std::vector<std::uint8_t> bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                    'W', 'A', 'V', 'E'};
    bytes.insert(bytes.end(), {'J', 'U', 'N', 'K'});
    u32(bytes, 3U);
    bytes.insert(bytes.end(), {1U, 2U, 3U, 0U});
    bytes.insert(bytes.end(), {'f', 'm', 't', ' '});
    u32(bytes, extensible ? 40U : 16U);
    u16(bytes, extensible ? 0xfffeU : tag);
    u16(bytes, channels);
    u32(bytes, rate);
    const std::uint16_t align = static_cast<std::uint16_t>(channels * bits / 8U);
    u32(bytes, rate * align);
    u16(bytes, align);
    u16(bytes, bits);
    if (extensible) {
        u16(bytes, 22U);
        u16(bytes, bits);
        u32(bytes, channels == 1U ? 4U : 3U);
        u32(bytes, tag);
        bytes.insert(bytes.end(), {0U, 0U, 0x10U, 0U, 0x80U, 0U,
                                   0U, 0xaaU, 0U, 0x38U, 0x9bU, 0x71U});
    }
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    u32(bytes, static_cast<std::uint32_t>(samples.size()));
    bytes.insert(bytes.end(), samples.begin(), samples.end());
    if ((samples.size() & 1U) != 0U)
        bytes.push_back(0U);
    setU32(bytes, 4U, static_cast<std::uint32_t>(bytes.size() - 8U));
    return bytes;
}

void acceptedFormats()
{
    const auto pcm16 = sms::audio::decodeWav(wav(1U, 1U, 16U, {0x00U, 0x40U}));
    check(pcm16 && pcm16.audio.frames == 1U &&
          std::abs(pcm16.audio.stereo[0] - 0.5f) < 1.0e-6f &&
          pcm16.audio.stereo[0] == pcm16.audio.stereo[1],
          "PCM16 mono is duplicated to stereo");

    const auto pcm24 = sms::audio::decodeWav(wav(
        1U, 2U, 24U, {0x00U, 0x00U, 0x40U, 0x00U, 0x00U, 0xc0U}, true));
    check(pcm24 && std::abs(pcm24.audio.stereo[0] - 0.5f) < 1.0e-6f &&
          std::abs(pcm24.audio.stereo[1] + 0.5f) < 1.0e-6f,
          "extensible PCM24 stereo is decoded around padded metadata");
    check(sms::audio::decodeWav(wav(1U, 1U, 24U, {0U, 0U, 0x40U})) &&
          sms::audio::decodeWav(wav(1U, 1U, 16U, {0U, 0x40U}, true)),
          "classic PCM24 and extensible PCM16 are decoded");

    const auto pcm32 = sms::audio::decodeWav(wav(
        1U, 1U, 32U, {0x00U, 0x00U, 0x00U, 0x40U}));
    check(pcm32 && std::abs(pcm32.audio.stereo[0] - 0.5f) < 1.0e-6f,
          "PCM32 is decoded");
    check(static_cast<bool>(sms::audio::decodeWav(wav(
              1U, 1U, 32U, {0U, 0U, 0U, 0x40U}, true))),
          "extensible PCM32 is decoded");

    const std::uint32_t half = std::bit_cast<std::uint32_t>(0.5f);
    const auto floating = sms::audio::decodeWav(wav(
        3U, 1U, 32U,
        {static_cast<std::uint8_t>(half), static_cast<std::uint8_t>(half >> 8U),
         static_cast<std::uint8_t>(half >> 16U), static_cast<std::uint8_t>(half >> 24U)},
        true));
    check(floating && floating.audio.stereo[0] == 0.5f,
          "extensible float32 mono is decoded");
    check(static_cast<bool>(sms::audio::decodeWav(wav(
              3U, 1U, 32U,
              {static_cast<std::uint8_t>(half), static_cast<std::uint8_t>(half >> 8U),
               static_cast<std::uint8_t>(half >> 16U),
               static_cast<std::uint8_t>(half >> 24U)}))),
          "classic float32 is decoded");
}

void rejectedFormats()
{
    check(sms::audio::decodeWav({}).error == sms::audio::WavError::malformed,
          "empty bytes are malformed");
    check(sms::audio::decodeWav(wav(1U, 3U, 16U, {0, 0, 0, 0, 0, 0})).error ==
              sms::audio::WavError::unsupportedFormat,
          "multichannel WAV is rejected");
    check(sms::audio::decodeWav(wav(3U, 1U, 64U, std::vector<std::uint8_t>(8U))).error ==
              sms::audio::WavError::unsupportedFormat,
          "float64 WAV is rejected");

    const auto nan = std::bit_cast<std::uint32_t>(
        std::numeric_limits<float>::quiet_NaN());
    check(sms::audio::decodeWav(wav(
        3U, 1U, 32U,
        {static_cast<std::uint8_t>(nan), static_cast<std::uint8_t>(nan >> 8U),
         static_cast<std::uint8_t>(nan >> 16U), static_cast<std::uint8_t>(nan >> 24U)})).error ==
              sms::audio::WavError::nonFiniteSample,
          "non-finite float WAV is rejected");

    auto truncated = wav(1U, 1U, 16U, {0, 0});
    truncated.pop_back();
    check(sms::audio::decodeWav(truncated).error == sms::audio::WavError::malformed,
          "truncated declared RIFF data is rejected");
    check(sms::audio::decodeWav(wav(1U, 1U, 16U, {0, 0}), 0.00001).error ==
              sms::audio::WavError::tooLong,
          "duration limit uses source frames and rate");
}

void encodeAndRender()
{
    sms::audio::WavAudio source;
    source.sampleRate = 1000U;
    source.frames = 100U;
    source.stereo.assign(200U, 1.0f);
    std::vector<std::uint8_t> encoded;
    check(sms::audio::encodeStereoPcm16Wav(source, encoded),
          "stereo PCM16 export encodes");
    const auto restored = sms::audio::decodeWav(encoded);
    check(restored && restored.audio.sampleRate == 1000U && restored.audio.frames == 100U &&
          std::abs(restored.audio.stereo[0] - 32767.0f / 32768.0f) < 1.0e-6f,
          "PCM16 export round trips");

    sms::dsp::SamplePlaybackSettings settings;
    settings.start = 0.25f;
    settings.end = 0.75f;
    settings.attackSeconds = 0.01f;
    settings.releaseSeconds = 0.01f;
    const auto processed = sms::audio::renderProcessedStereo(source, settings);
    check(processed.frames == 50U && processed.sampleRate == source.sampleRate &&
          processed.stereo.front() == 0.0f && processed.stereo.back() < 0.2f,
          "processed render applies region and automatic ADSR release");

    check(sms::audio::hasWavExtension("sample.WAV") &&
          !sms::audio::hasWavExtension("sample.aiff") &&
          !sms::audio::hasAnyExtension("folder.name/sample") &&
          sms::audio::hasAnyExtension("folder.name/sample.aiff"),
          "WAV path extension policy is case insensitive and basename scoped");
}

void fileActionsAndProtocol()
{
    using midichopper::plugin::PadFileAction;
    const std::string pathText = "/tmp/SMS ü; sample.wav";
    const std::string encoded = midichopper::plugin::encodePadFileRequest(
        PadFileAction::exportProcessed, 17U, pathText);
    midichopper::plugin::PadFileRequest request;
    check(midichopper::plugin::decodePadFileRequest(encoded, request) &&
          request.action == PadFileAction::exportProcessed && request.pad == 17U &&
          request.path == pathText, "pad file request preserves Unicode and separators in paths");
    check(!midichopper::plugin::decodePadFileRequest("9;0;/tmp/a.wav", request) &&
          !midichopper::plugin::decodePadFileRequest("0;64;/tmp/a.wav", request),
          "pad file request rejects invalid action and pad values");

    midichopper::plugin::PadFileResultEventTracker results;
    check(results.consume(midichopper::plugin::padFileResultEventValue(
              midichopper::plugin::PadFileResultCode::importSucceeded, false)) ==
              midichopper::plugin::PadFileResultCode::importSucceeded &&
          results.consume(midichopper::plugin::padFileResultEventValue(
              midichopper::plugin::PadFileResultCode::importSucceeded, false)) ==
              midichopper::plugin::PadFileResultCode::none &&
          results.consume(midichopper::plugin::padFileResultEventValue(
              midichopper::plugin::PadFileResultCode::importSucceeded, true)) ==
              midichopper::plugin::PadFileResultCode::importSucceeded,
          "pad file result events remain observable across repeated results");

    sms::audio::WavAudio source;
    source.sampleRate = 48000U;
    source.frames = 2U;
    source.stereo = {-1.0f, -0.5f, 0.5f, 1.0f};
    const auto temporary = std::filesystem::temp_directory_path() /
                           "sms-midichopper-wav-action-test.wav";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    check(midichopper::plugin::writePadWav(temporary, source).empty(),
          "pad WAV writer creates a file");
    const auto loaded = midichopper::plugin::readPadWav(temporary);
    check(loaded && loaded.audio.frames == source.frames &&
          loaded.audio.sampleRate == source.sampleRate && loaded.audio.stereo.front() == -1.0f,
          "pad WAV reader decodes an exported file");
    std::filesystem::remove(temporary, ignored);
    check(!midichopper::plugin::readPadWav(temporary),
          "pad WAV reader reports a missing file");
}

void realtimeAccessGate()
{
    sms::audio::RealtimeAccessGate gate;
    bool inactiveCallbackRan = false;
    check(gate.withPaused([&] { inactiveCallbackRan = true; }) && inactiveCallbackRan,
          "inactive audio grants control access immediately");

    gate.activate();
    bool timedOutCallbackRan = false;
    check(!gate.withPaused([&] { timedOutCallbackRan = true; },
                           std::chrono::milliseconds(2)) &&
          !timedOutCallbackRan && !gate.audioShouldYield(),
          "timed-out access request restores the idle audio state");

    std::atomic<bool> activeCallbackRan{false};
    std::atomic<bool> activeRequestSucceeded{false};
    std::thread requester([&] {
        activeRequestSucceeded.store(gate.withPaused([&] {
            activeCallbackRan.store(true, std::memory_order_release);
        }), std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    bool yielded = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (gate.audioShouldYield()) {
            yielded = true;
            break;
        }
        std::this_thread::yield();
    }
    requester.join();
    check(yielded && activeRequestSucceeded.load(std::memory_order_acquire) &&
          activeCallbackRan.load(std::memory_order_acquire) && !gate.audioShouldYield(),
          "active audio yields at a callback boundary and resumes after control access");
    gate.deactivate();
}

} // namespace

int main()
{
    acceptedFormats();
    rejectedFormats();
    encodeAndRender();
    fileActionsAndProtocol();
    realtimeAccessGate();
    std::cout << "WAV codec tests passed\n";
}
