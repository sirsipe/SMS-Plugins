#include "Audio/WavCodec.hpp"
#include "Audio/RealtimeAccessGate.hpp"
#include "PadFileActionProtocol.hpp"
#include "PadClipboardProtocol.hpp"
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
    const auto longSong = wav(1U, 1U, 16U,
                              std::vector<std::uint8_t>(31U * 48000U * 2U));
    const auto longSongDecoded = sms::audio::decodeWav(longSong);
    check(longSongDecoded && longSongDecoded.audio.frames == 31U * 48000U,
          "default WAV import accepts a song longer than 30 seconds");
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

    source.frames = 6U;
    source.stereo = {1,10, 2,20, 3,30, 4,40, 5,50, 6,60};
    sms::dsp::SamplePlaybackSettings fullRegion;
    sms::dsp::SampleMixerSettings mixer;
    mixer.gainDecibels = -6.0205999f;
    mixer.pan = -1.0f;
    mixer.tuneSemitones = 12.0f;
    const auto mixed = sms::audio::renderProcessedStereo(source, fullRegion, mixer);
    check(mixed.frames == 3U && std::abs(mixed.stereo[0] - 0.5f) < 1.0e-5f &&
          std::abs(mixed.stereo[2] - 1.5f) < 1.0e-5f && mixed.stereo[1] == 0.0f,
          "processed render applies gain, stereo balance, and varispeed tune");

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

    using midichopper::plugin::PadClipboardAction;
    midichopper::plugin::PadClipboardRequest clipboardRequest;
    check(midichopper::plugin::decodePadClipboardRequest(
              midichopper::plugin::encodePadClipboardRequest(
                  PadClipboardAction::paste, 63U), clipboardRequest) &&
          clipboardRequest.action == PadClipboardAction::paste &&
          clipboardRequest.pad == 63U,
          "pad clipboard request round trips");
    check(!midichopper::plugin::decodePadClipboardRequest("2;0", clipboardRequest) &&
          !midichopper::plugin::decodePadClipboardRequest("0;64", clipboardRequest) &&
          !midichopper::plugin::decodePadClipboardRequest("0;1;2", clipboardRequest),
          "pad clipboard request rejects invalid values");

    midichopper::plugin::PadClipboardResultEventTracker clipboardResults;
    check(clipboardResults.consume(midichopper::plugin::padClipboardResultEventValue(
              midichopper::plugin::PadClipboardResultCode::pasted, false)) ==
              midichopper::plugin::PadClipboardResultCode::pasted &&
          clipboardResults.consume(midichopper::plugin::padClipboardResultEventValue(
              midichopper::plugin::PadClipboardResultCode::pasted, false)) ==
              midichopper::plugin::PadClipboardResultCode::none &&
          clipboardResults.consume(midichopper::plugin::padClipboardResultEventValue(
              midichopper::plugin::PadClipboardResultCode::pasted, true)) ==
              midichopper::plugin::PadClipboardResultCode::pasted,
          "pad clipboard results remain observable across repeated actions");

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
    bool suspendedCallbackRan = false;
    check(gate.withPaused([&] { suspendedCallbackRan = true; },
                          std::chrono::milliseconds(20)) && suspendedCallbackRan,
          "control access is immediate when no audio callback is running");

    std::atomic<bool> timedOutCallbackRan{false};
    std::atomic<bool> timedOutRequestSucceeded{true};
    {
        const auto audioAccess = gate.audioAccess();
        std::thread requester([&] {
            timedOutRequestSucceeded.store(gate.withPaused([&] {
                timedOutCallbackRan.store(true, std::memory_order_release);
            }, std::chrono::milliseconds(2)), std::memory_order_release);
        });
        requester.join();
        check(!timedOutRequestSucceeded.load(std::memory_order_acquire) &&
              !audioAccess.shouldYield() &&
              !timedOutCallbackRan.load(std::memory_order_acquire),
              "timed-out request does not race the current audio callback");
    }
    check(!gate.audioAccess().shouldYield(),
          "timed-out control request restores idle state after audio completes");

    std::atomic<bool> activeCallbackRan{false};
    std::atomic<bool> activeRequestSucceeded{false};
    std::thread requester;
    {
        const auto audioAccess = gate.audioAccess();
        requester = std::thread([&] {
            activeRequestSucceeded.store(gate.withPaused([&] {
                activeCallbackRan.store(true, std::memory_order_release);
            }), std::memory_order_release);
        });
        check(!audioAccess.shouldYield() && !activeCallbackRan.load(std::memory_order_acquire),
              "control request waits for the current audio callback to finish");
    }
    requester.join();
    check(activeRequestSucceeded.load(std::memory_order_acquire) &&
          activeCallbackRan.load(std::memory_order_acquire),
          "audio completion hands protected state to the waiting control request");

    std::atomic<bool> controlAccessStarted{false};
    std::atomic<bool> releaseControlAccess{false};
    std::thread controller([&] {
        static_cast<void>(gate.withPaused([&] {
            controlAccessStarted.store(true, std::memory_order_release);
            while (!releaseControlAccess.load(std::memory_order_acquire))
                std::this_thread::yield();
        }));
    });
    const auto controlDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!controlAccessStarted.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < controlDeadline)
        std::this_thread::yield();
    check(controlAccessStarted.load(std::memory_order_acquire) &&
          gate.audioAccess().shouldYield(),
          "audio callback yields while the control thread owns protected state");
    releaseControlAccess.store(true, std::memory_order_release);
    controller.join();
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
