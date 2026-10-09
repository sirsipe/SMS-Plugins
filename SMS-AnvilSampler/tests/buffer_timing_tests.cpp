#include "SamplerEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {
using namespace midichopper;

void check(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

constexpr std::uint32_t kFrames = 8192U;
const std::array<std::vector<std::uint32_t>, 7> kPartitions{{
    {kFrames}, {1U}, {64U}, {256U}, {511U},
    {1U, 7U, 31U, 2U, 255U, 64U, 513U},
    {63U, 1U, 191U, 1U, 255U, 1U, 511U, 1U}
}};

struct Audio {
    std::vector<float> left = std::vector<float>(kFrames);
    std::vector<float> right = std::vector<float>(kFrames);
};

Audio inputAudio()
{
    Audio input;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        // Binary fractions make captured PCM comparisons exact and channels distinct.
        input.left[frame] = static_cast<float>(frame % 101U) / 128.0f;
        input.right[frame] = -static_cast<float>(frame % 73U) / 128.0f;
    }
    return input;
}

Audio processTimeline(SamplerEngine& engine, const Audio* input,
                      const std::span<const MidiEvent> timeline,
                      const std::vector<std::uint32_t>& partition,
                      const bool emptyCallbacks = false)
{
    Audio output;
    std::size_t eventIndex = 0U;
    std::size_t block = 0U;
    for (std::uint32_t start = 0U; start < kFrames;) {
        const auto count = std::min(partition[block++ % partition.size()], kFrames - start);
        std::vector<MidiEvent> events;
        while (eventIndex < timeline.size() && timeline[eventIndex].frameOffset < start + count) {
            check(timeline[eventIndex].frameOffset >= start, "timeline is sorted");
            auto event = timeline[eventIndex++];
            event.frameOffset -= start;
            events.push_back(event);
        }
        if (emptyCallbacks) {
            float left = 123.0f, right = -456.0f;
            engine.process(nullptr, nullptr, &left, &right, 0U);
            check(left == 123.0f && right == -456.0f,
                  "zero-frame callbacks leave output buffers untouched");
        }
        engine.process(input ? input->left.data() + start : nullptr,
                       input ? input->right.data() + start : nullptr,
                       output.left.data() + start, output.right.data() + start,
                       count, std::span<const MidiEvent>(events));
        start += count;
    }
    check(eventIndex == timeline.size(), "all scheduled events reached the engine");
    return output;
}

void compare(const Audio& actual, const Audio& expected)
{
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
        if (!std::isfinite(actual.left[frame]) || !std::isfinite(actual.right[frame]) ||
            std::abs(actual.left[frame] - expected.left[frame]) > 1.0e-5f ||
            std::abs(actual.right[frame] - expected.right[frame]) > 1.0e-5f) {
            std::cerr << "FAIL: partition changed stereo output at frame " << frame << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

PadData playbackSource(const double rate, const std::uint32_t pad)
{
    PadData source;
    source.sampleRate = rate;
    source.frames = 1024U;
    source.stereo.resize(source.frames * 2U);
    for (std::uint32_t frame = 0U; frame < source.frames; ++frame) {
        source.stereo[frame * 2U] = static_cast<float>((frame + pad * 11U) % 47U) / 64.0f;
        source.stereo[frame * 2U + 1U] = -static_cast<float>((frame + pad * 7U) % 31U) / 64.0f;
    }
    return source;
}

Audio renderPlayback(const double rate, const PlaybackMode mode, const bool effects,
                     const std::vector<std::uint32_t>& partition, const bool emptyCallbacks)
{
    SamplerEngine engine(rate, 0.2);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = mode;
    settings.maxVoices = 2U;
    settings.gain = 0.75f;
    if (effects) {
        settings.lowpass = 0.3f;
        settings.highpass = 0.1f;
        settings.filterSlope = 2.0f;
        settings.tuneSemitones = -3.0f;
    }
    engine.setSettings(settings);
    for (std::uint32_t pad = 0U; pad < 3U; ++pad) {
        check(engine.importPad(pad, playbackSource(pad == 1U ? 32000.0 : 96000.0, pad)),
              "import partition playback source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.875f;
        playback.attackSeconds = 0.002f;
        playback.decaySeconds = 0.001f;
        playback.sustainLevel = 0.7f;
        playback.releaseSeconds = 0.002f;
        engine.setPadPlaybackSettings(pad, playback);
        if (effects) {
            sms::dsp::SampleMixerSettings mixer;
            mixer.pan = pad == 1U ? -0.4f : 0.3f;
            mixer.tuneSemitones = static_cast<float>(pad * 4U);
            mixer.lowpass = 0.2f;
            mixer.highpass = 0.15f;
            mixer.filterSlope = static_cast<float>(pad);
            mixer.dirty = pad == 2U ? 1.0f : 0.0f;
            engine.setPadMixerSettings(pad, mixer);
        }
    }
    const std::array<MidiEvent, 19> events{{
        {0U, 36U, 127U, MidiEventType::NoteOn},
        {63U, 37U, 81U, MidiEventType::NoteOn},
        {64U, 38U, 100U, MidiEventType::NoteOn}, // steals the oldest of three voices
        {255U, 37U, 0U, MidiEventType::NoteOff},
        {256U, 36U, 91U, MidiEventType::NoteOn},
        {256U, 36U, 0U, MidiEventType::NoteOff},
        {257U, 38U, 0U, MidiEventType::NoteOff}, // adapter-normalized velocity-zero note
        {511U, 38U, 127U, MidiEventType::NoteOn},
        {512U, 36U, 70U, MidiEventType::NoteOn},
        {1023U, 37U, 127U, MidiEventType::NoteOn},
        {1024U, 37U, 0U, MidiEventType::NoteOff},
        {1024U, 37U, 100U, MidiEventType::NoteOn},
        {1399U, 38U, 127U, MidiEventType::NoteOn},
        {2047U, 36U, 127U, MidiEventType::NoteOn},
        {2048U, 37U, 127U, MidiEventType::NoteOn},
        {2048U, 38U, 127U, MidiEventType::NoteOn},
        {3600U, 36U, 127U, MidiEventType::NoteOn},
        {3900U, 36U, 0U, MidiEventType::NoteOff},
        {4095U, 127U, 127U, MidiEventType::NoteOn} // outside the configured pad range
    }};
    auto output = processTimeline(engine, nullptr, events, partition, emptyCallbacks);
    check(std::any_of(output.left.begin(), output.left.end(), [](float value) {
        return std::abs(value) > 0.001f;
    }), "partition playback actually produces audio");
    check(!engine.anyPlaybackActive(), "timeline finishes every voice");
    check(engine.lastPlaybackTrigger().pad == 0U,
          "out-of-range MIDI does not change the last successful trigger");
    return output;
}

void playbackPartitions()
{
    for (const auto rate : {44100.0, 48000.0})
        for (const auto mode : {PlaybackMode::OneShot, PlaybackMode::Gated})
            for (const bool effects : {false, true}) {
                const auto expected = renderPlayback(rate, mode, effects, kPartitions[0], false);
                for (const auto& partition : kPartitions)
                    compare(renderPlayback(rate, mode, effects, partition, true), expected);
            }
}

struct Capture {
    Audio monitor;
    std::array<PadData, 5> pads;
};

Capture captureTimeline(const double rate, const CaptureMode mode, const float preRoll,
                        const std::vector<std::uint32_t>& partition)
{
    SamplerEngine engine(rate, 0.2);
    auto settings = engine.settings();
    settings.armed = true;
    settings.captureMode = mode;
    settings.preRollMilliseconds = preRoll;
    settings.fixedLengthSeconds = 0.004;
    settings.monitorInput = true;
    settings.monitorGain = 0.25f;
    settings.gain = 0.5f;
    engine.setSettings(settings);
    const std::array<MidiEvent, 8> events{{
        {63U, 90U, 127U, MidiEventType::NoteOn},
        {64U, 90U, 0U, MidiEventType::NoteOff},
        {255U, 91U, 0U, MidiEventType::NoteOff}, // adapter-normalized velocity-zero note
        {256U, 91U, 127U, MidiEventType::NoteOn},
        {511U, 92U, 127U, MidiEventType::NoteOn},
        {1024U, 93U, 127U, MidiEventType::NoteOn},
        {1024U, 93U, 0U, MidiEventType::NoteOff},
        {6144U, 94U, 127U, MidiEventType::NoteOn} // pre-roll ring has wrapped
    }};
    const auto input = inputAudio();
    Capture capture;
    capture.monitor = processTimeline(engine, &input, events, partition, true);
    engine.finalizeRecording();
    float left = 0.0f, right = 0.0f;
    engine.process(nullptr, nullptr, &left, &right, 0U);
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
        check(capture.monitor.left[frame] == input.left[frame] * settings.monitorGain &&
              capture.monitor.right[frame] == input.right[frame] * settings.monitorGain,
              "capture monitoring uses only monitor gain");
    }
    for (std::uint32_t pad = 0U; pad < capture.pads.size(); ++pad) {
        check(engine.exportPad(pad, capture.pads[pad]), "export partitioned capture");
        check(capture.pads[pad].frames > 0U && !engine.padMetadata(pad).recording,
              "every capture trigger creates a finalized slice");
    }
    check(!engine.padMetadata(5U).occupied,
          "note-offs and velocity-zero messages create no capture slices");
    return capture;
}

void capturePartitions()
{
    constexpr std::array<std::uint32_t, 5> boundaries{63U, 256U, 511U, 1024U, 6144U};
    const auto input = inputAudio();
    for (const auto rate : {44100.0, 48000.0})
        for (const auto mode : {CaptureMode::Sequential, CaptureMode::FixedDuration})
            for (const float preRoll : {0.0f, 0.5f, 2.0f}) {
                const auto expected = captureTimeline(rate, mode, preRoll, kPartitions[0]);
                const auto history = static_cast<std::uint32_t>(std::llround(rate * preRoll / 1000.0));
                for (std::uint32_t pad = 0U; pad < boundaries.size(); ++pad) {
                    const auto start = boundaries[pad] - std::min(history, boundaries[pad]);
                    const auto end = mode == CaptureMode::Sequential
                        ? (pad + 1U < boundaries.size() ? boundaries[pad + 1U] - history : kFrames)
                        : start + static_cast<std::uint32_t>(std::llround(rate * 0.004));
                    check(expected.pads[pad].frames == end - start,
                          "capture length matches absolute MIDI boundaries and pre-roll");
                    for (auto frame = start; frame < end; ++frame) {
                        const auto offset = (frame - start) * 2U;
                        check(expected.pads[pad].stereo[offset] == input.left[frame] &&
                              expected.pads[pad].stereo[offset + 1U] == input.right[frame],
                              "captured PCM matches the original stereo input interval");
                    }
                }
                for (const auto& partition : kPartitions) {
                    const auto actual = captureTimeline(rate, mode, preRoll, partition);
                    compare(actual.monitor, expected.monitor);
                    for (std::uint32_t pad = 0U; pad < actual.pads.size(); ++pad)
                        check(actual.pads[pad].frames == expected.pads[pad].frames &&
                              actual.pads[pad].stereo == expected.pads[pad].stereo,
                              "block partition preserves capture boundaries and exact PCM");
                }
            }
}

void eventOrdering()
{
    // Independent sample expectations ensure partition comparisons cannot pass in silence.
    for (const bool offFirst : {false, true}) {
        SamplerEngine engine(1000.0, 0.1);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        PadData source;
        source.sampleRate = 1000.0;
        source.frames = 8U;
        source.stereo.assign(16U, 0.5f);
        check(engine.importPad(0U, source), "import event ordering sample");
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        const MidiEvent off{0U, 36U, 0U, MidiEventType::NoteOff};
        const std::array<MidiEvent, 2> events = offFirst
            ? std::array<MidiEvent, 2>{off, on} : std::array<MidiEvent, 2>{on, off};
        std::array<float, 4> left{}, right{};
        engine.process(nullptr, nullptr, left.data(), right.data(), 4U, events);
        for (std::size_t frame = 0U; frame < left.size(); ++frame)
            check(left[frame] == (offFirst ? 0.5f : 0.0f) && right[frame] == left[frame],
                  "same-offset events preserve supplied order before producing audio");
    }
}

void playbackOffsets()
{
    const std::array<MidiEvent, 7> events{{
        {0U, 36U, 127U, MidiEventType::NoteOn},
        {2U, 36U, 0U, MidiEventType::NoteOff},
        {63U, 36U, 127U, MidiEventType::NoteOn},
        {64U, 36U, 0U, MidiEventType::NoteOff},
        {256U, 36U, 127U, MidiEventType::NoteOn},
        {258U, 36U, 0U, MidiEventType::NoteOff},
        {kFrames - 1U, 36U, 127U, MidiEventType::NoteOn}
    }};
    for (const auto& partition : kPartitions) {
        SamplerEngine engine(48000.0, 0.1);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        PadData source;
        source.sampleRate = 48000.0;
        source.frames = 8U;
        source.stereo.assign(16U, 0.5f);
        check(engine.importPad(0U, source), "import exact MIDI timing sample");
        const auto actual = processTimeline(engine, nullptr, events, partition, true);
        for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
            const float expected = frame < 2U || frame == 63U || frame == 256U ||
                frame == 257U || frame == kFrames - 1U ? 0.5f : 0.0f;
            check(actual.left[frame] == expected && actual.right[frame] == expected,
                  "note-on and note-off take effect at exact absolute sample offsets");
        }
        check(engine.anyPlaybackActive() && engine.lastPlaybackTrigger().generation == 4U,
              "last-frame note-on remains active and zero-frame callbacks do not replay events");
    }
}
}

int main()
{
    eventOrdering();
    playbackOffsets();
    playbackPartitions();
    capturePartitions();
    std::cout << "Buffer timing tests passed\n";
}
