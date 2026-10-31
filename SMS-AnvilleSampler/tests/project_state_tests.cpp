#include "Parameters.hpp"
#include "StateCodec.hpp"
#include "StateKeys.hpp"
#include "src/DistrhoPluginInternal.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace midichopper::plugin;
using DISTRHO::PluginExporter;
constexpr std::uint32_t kFrames = 64U;
using Audio = std::array<std::array<float, kFrames>, 2>;

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool sameBits(const float a, const float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

Audio process(PluginExporter& plugin, const Audio& input, const std::uint32_t frames,
              const bool trigger = false)
{
    Audio output{};
    const float* inputs[]{input[0].data(), input[1].data()};
    float* outputs[]{output[0].data(), output[1].data()};
    DISTRHO::MidiEvent note{};
    note.size = 3U;
    note.data[0] = 0x90U;
    note.data[1] = midichopper::kDefaultBaseMidiNote;
    note.data[2] = 127U;
    plugin.run(inputs, outputs, frames, trigger ? &note : nullptr, trigger ? 1U : 0U);
    return output;
}

struct SavedState {
    std::array<std::string, 3> values;
    Audio playback;
};
const std::array<const char*, 3> kKeys{
    kPadStateKeys[0].c_str(), kPadEditStateKeys[0].c_str(), kPadMixerStateKeys[0].c_str()};

SavedState captureAndSave()
{
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    plugin.setParameterValue(kParameterInputMonitor, 0.0f);
    plugin.setParameterValue(kParameterMode, 1.0f);
    Audio input{};
    constexpr float samples[]{1.5f, -2.25f, 1.0e-8f, -1.0e-8f, 0.1f, -0.3f, 0.0f, -0.0f};
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
        input[0][frame] = samples[frame % 8U];
        input[1][frame] = samples[(frame + 3U) % 8U];
    }
    static_cast<void>(process(plugin, input, kFrames, true));
    plugin.setParameterValue(kParameterMode, 0.0f);
    static_cast<void>(process(plugin, {}, 0U)); // Publish the complete recording.

    sms::dsp::SamplePlaybackSettings edit;
    edit.start = 0.125f;
    edit.end = 0.875f;
    edit.attackSeconds = 0.0001f;
    edit.releaseSeconds = 0.0001f;
    const auto encodedEdit = encodePlaybackSettings(edit);
    plugin.setState(kKeys[1], encodedEdit.c_str());
    sms::dsp::SampleMixerSettings mixer;
    mixer.gainDecibels = -6.0f;
    mixer.pan = 0.25f;
    mixer.tuneSemitones = 0.37f;
    const auto encodedMixer = encodeMixerSettings(mixer);
    plugin.setState(kKeys[2], encodedMixer.c_str());

    SavedState saved;
    for (std::size_t i = 0U; i < kKeys.size(); ++i)
        saved.values[i] = plugin.getStateValue(kKeys[i]).buffer();
    DecodedPadState decoded;
    check(decodePadState(saved.values[0].c_str(), decoded) && decoded.pad.frames == kFrames &&
          decoded.sourceSampleRate == 48000U, "adapter saves captured stereo PCM at its source rate");
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame)
        for (std::uint32_t channel = 0U; channel < 2U; ++channel)
            check(sameBits(decoded.pad.stereo[frame * 2U + channel], input[channel][frame]),
                  "capture and host save preserve quiet audio, headroom, channel order and signed zero");
    check(saved.values[1] == encodedEdit && saved.values[2] == encodedMixer,
          "host save includes desired region, ADSR and mixer settings before another callback");
    saved.playback = process(plugin, {}, kFrames, true);
    bool audible = false;
    for (const float sample : saved.playback[0]) audible = audible || sample != 0.0f;
    check(audible, "reference playback contains the saved recording");
    plugin.deactivateIfNeeded();
    return saved;
}

void restoreAndCompare(const SavedState& saved, const bool active)
{
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    plugin.setParameterValue(kParameterInputMonitor, 0.0f);
    if (active) plugin.activate(); // An active host can suspend callbacks while restoring.
    for (std::size_t i = 0U; i < kKeys.size(); ++i)
        plugin.setState(kKeys[i], saved.values[i].c_str());
    for (std::size_t i = 0U; i < kKeys.size(); ++i)
        check(plugin.getStateValue(kKeys[i]) == saved.values[i].c_str(),
              "host can read restored PCM and settings before processing resumes");
    const auto output = process(plugin, {}, kFrames, true);
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame)
        for (std::uint32_t channel = 0U; channel < 2U; ++channel)
            check(sameBits(output[channel][frame], saved.playback[channel][frame]),
                  "recreated adapter plays the saved edited recording identically");
    check(plugin.getStateValue(kKeys[0]) == saved.values[0].c_str(),
          "processing leaves the restored source audio unchanged");
    plugin.deactivateIfNeeded();
}

void legacyRestore()
{
    // Literal released PCM16 state; its decoded samples must retain the old scaling.
    constexpr char legacy[] = "U1NQMQEAAgCAuwAAAgAAAAgAAAAy9vZ8AEAA4P9/AYA=";
    constexpr float expected[]{16384.0f / 32767.0f, -8192.0f / 32767.0f, 1.0f, -1.0f};
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    plugin.setParameterValue(kParameterInputMonitor, 0.0f);
    plugin.setState(kKeys[0], legacy);
    const auto upgraded = plugin.getStateValue(kKeys[0]);
    DecodedPadState decoded;
    check(decodePadState(upgraded.buffer(), decoded) && decoded.pad.stereo.size() == 4U,
          "adapter reads a released PCM16 project and saves float32 state");
    for (std::size_t i = 0U; i < 4U; ++i)
        check(sameBits(decoded.pad.stereo[i], expected[i]), "upgrading preserves released PCM16 sample values");
    const auto output = process(plugin, {}, kFrames, true);
    for (std::uint32_t frame = 0U; frame < 2U; ++frame)
        for (std::uint32_t channel = 0U; channel < 2U; ++channel)
            check(sameBits(output[channel][frame], expected[frame * 2U + channel]),
                  "legacy project playback retains its released PCM16 amplitude");
    plugin.deactivateIfNeeded();
}
} // namespace

int main()
{
    DISTRHO::d_nextBufferSize = kFrames;
    DISTRHO::d_nextSampleRate = 48000.0;
    // Suppress user preferences during construction; state and run use the real adapter/engine.
    DISTRHO::d_nextPluginIsDummy = true;
    const auto saved = captureAndSave();
    restoreAndCompare(saved, false);
    restoreAndCompare(saved, true);
    legacyRestore();
    DISTRHO::d_nextPluginIsDummy = false;
    std::cout << "Project state tests passed\n";
}
