#include "../src/core/SamplerEngine.hpp"
#include "../src/core/PadClipboard.hpp"
#include "Audio/RealtimeCommandDispatcher.hpp"
#include "Audio/WavCodec.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

// Count destruction as well as allocation: retiring a retained sample on the
// callback thread is just as unsuitable as allocating its replacement there.
namespace allocation_probe {
thread_local bool audioThread = false;
std::atomic<unsigned> allocations{0};
std::atomic<unsigned> deletions{0};
void allocated() noexcept { if (audioThread) allocations.fetch_add(1); }
void deleted(void* pointer) noexcept { if (audioThread && pointer) deletions.fetch_add(1); }
}

void* operator new(std::size_t bytes)
{
    allocation_probe::allocated();
    if (void* pointer = std::malloc(std::max(bytes, std::size_t{1}))) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept
{
    allocation_probe::deleted(pointer);
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    allocation_probe::allocated();
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment),
                      std::max(bytes, std::size_t{1})) == 0) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{ return ::operator new(bytes, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{ ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{ ::operator delete(pointer); }

namespace {
using midichopper::SamplerEngine;
using midichopper::PadData;
using sms::audio::RealtimeCommandDispatcher;
constexpr double kRate = 48000.0;
constexpr std::uint32_t kFrames = 64;

void check(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<class Predicate>
void await(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::yield();
    }
}

PadData sample(std::uint32_t frames, float scale = 1.0f)
{
    PadData pad;
    pad.sampleRate = kRate;
    pad.frames = frames;
    pad.stereo.resize(static_cast<std::size_t>(frames) * 2);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const float value = scale * static_cast<float>((frame % 251) + 1) / 1024.0f;
        pad.stereo[static_cast<std::size_t>(frame) * 2] = value;
        pad.stereo[static_cast<std::size_t>(frame) * 2 + 1] = -value * 0.5f;
        pad.peak = std::max(pad.peak, value);
    }
    pad.rms = pad.peak * 0.5f;
    return pad;
}

bool same(sms::dsp::SamplePlaybackSettings a, sms::dsp::SamplePlaybackSettings b)
{
    return a.start == b.start && a.end == b.end &&
           a.attackSeconds == b.attackSeconds && a.decaySeconds == b.decaySeconds &&
           a.sustainLevel == b.sustainLevel && a.releaseSeconds == b.releaseSeconds;
}
bool same(sms::dsp::SampleMixerSettings a, sms::dsp::SampleMixerSettings b)
{
    return a.gainDecibels == b.gainDecibels && a.pan == b.pan &&
           a.tuneSemitones == b.tuneSemitones && a.lowpass == b.lowpass &&
           a.highpass == b.highpass && a.filterSlope == b.filterSlope && a.dirty == b.dirty;
}

// The callback uses only fixed arrays and atomics. Every block has the same
// exact oracle, including a note-on at frame 7 and note-off at frame 51.
class AudioHarness {
public:
    explicit AudioHarness(SamplerEngine& sampler, bool recording = false)
        : sampler_(sampler), recording_(recording)
    {
        auto settings = sampler.settings();
        settings.monitorInput = true;
        settings.playbackMode = midichopper::PlaybackMode::Gated;
        settings.maxVoices = 1;
        if (recording) {
            settings.armed = true;
            settings.activeBank = 2;
            settings.midiBankMode = midichopper::MidiBankMode::SelectedBank;
            // Bound total recording independently of machine speed.
            blockLimit.store(16);
        }
        sampler.setSettings(settings);
        sampler.setControlDispatcher(this, dispatch);
        dispatcher.activate();
        worker_ = std::thread([this] { run(); });
        await([this] { return blocks.load() >= 1; }, "audio thread starts");
    }

    ~AudioHarness() { stop(); }

    void stop()
    {
        if (!worker_.joinable()) return;
        stopping_.store(true);
        worker_.join();
        dispatcher.deactivate();
        sampler_.setControlDispatcher(nullptr, nullptr);
        check(errors.load() == 0, "every processed frame preserves monitoring and MIDI timing");
        check(allocation_probe::allocations.load() == 0, "audio processing/commits allocate nothing");
        check(allocation_probe::deletions.load() == 0, "audio processing/commits destroy no allocations");
    }

    // Runs once after the next capture returns, before the caller can copy PCM.
    // It deliberately replaces the captured pad and exercises reader lifetime.
    void (*afterCapture)(void*) = nullptr;
    void* afterContext = nullptr;
    std::atomic<std::uint64_t> blocks{0};
    std::atomic<std::uint64_t> blockLimit{~std::uint64_t{0}};
    std::atomic<unsigned> errors{0};
    unsigned captures = 0;
    RealtimeCommandDispatcher dispatcher;

private:
    static void dispatch(void* opaque, RealtimeCommandDispatcher::Operation operation,
                         void* context)
    {
        auto& harness = *static_cast<AudioHarness*>(opaque);
        harness.dispatcher.dispatch(operation, context);
        ++harness.captures;
        const auto before = harness.blocks.load();
        if (!harness.recording_)
            await([&] { return harness.blocks.load() >= before + 4; },
                  "audio progresses while the control caller holds a captured snapshot");
        if (harness.afterCapture) {
            const auto callback = harness.afterCapture;
            harness.afterCapture = nullptr;
            callback(harness.afterContext);
        }
    }

    void run() noexcept
    {
        std::array<float, kFrames> inputLeft{}, inputRight{}, outputLeft{}, outputRight{};
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            inputLeft[frame] = static_cast<float>(frame + 1) / 1024.0f;
            inputRight[frame] = -static_cast<float>(frame + 1) / 2048.0f;
        }
        const std::array<midichopper::MidiEvent, 2> notes{{
            {7, 51, 127, midichopper::MidiEventType::NoteOn},
            {51, 51, 0, midichopper::MidiEventType::NoteOff}}};
        const midichopper::MidiEvent capture{11, 36, 127, midichopper::MidiEventType::NoteOn};
        allocation_probe::audioThread = true;
        while (!stopping_.load()) {
            dispatcher.service();
            const auto block = blocks.load();
            if (block >= blockLimit.load()) {
                std::this_thread::yield();
                continue;
            }
            sampler_.process(inputLeft.data(), inputRight.data(),
                             outputLeft.data(), outputRight.data(), kFrames,
                             recording_ ? (block == 0 ? &capture : nullptr) : notes.data(),
                             recording_ ? (block == 0 ? 1 : 0) : 2);
            for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
                const bool voice = !recording_ && frame >= 7 && frame < 51;
                const float expectedLeft = inputLeft[frame] + (voice ? 0.125f : 0.0f);
                const float expectedRight = inputRight[frame] + (voice ? -0.25f : 0.0f);
                if (std::abs(outputLeft[frame] - expectedLeft) > 1.0e-6f ||
                    std::abs(outputRight[frame] - expectedRight) > 1.0e-6f)
                    errors.fetch_add(1);
            }
            blocks.store(block + 1);
        }
        allocation_probe::audioThread = false;
    }

    SamplerEngine& sampler_;
    bool recording_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

void setPerformancePad(SamplerEngine& engine)
{
    PadData pad;
    pad.sampleRate = kRate;
    pad.frames = kFrames;
    pad.stereo.resize(kFrames * 2);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        pad.stereo[frame * 2] = 0.125f;
        pad.stereo[frame * 2 + 1] = -0.25f;
    }
    pad.peak = pad.rms = 0.25f;
    check(engine.importPad(15, pad), "install unrelated performance pad");
}

void transferContinuity(const PadData& source)
{
    SamplerEngine engine(kRate, 30.0);
    setPerformancePad(engine);
    AudioHarness audio(engine);
    midichopper::PadClipboard clipboard;
    PadData exported, left, middle, right;
    for (unsigned repetition = 0; repetition < 3; ++repetition) {
        const auto before = audio.blocks.load();
        check(engine.importPad(0, source), "long import during performance");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f; playback.end = 0.875f;
        playback.attackSeconds = 0.01f; playback.decaySeconds = 0.02f;
        playback.sustainLevel = 0.75f; playback.releaseSeconds = 0.03f;
        sms::dsp::SampleMixerSettings mixer;
        mixer.gainDecibels = -6.0f; mixer.pan = 0.25f;
        mixer.tuneSemitones = -3.0f; mixer.lowpass = 0.125f;
        mixer.highpass = 0.25f; mixer.filterSlope = 2; mixer.dirty = 1;
        engine.setPadPlaybackSettings(0, playback);
        engine.setPadMixerSettings(0, mixer);
        sms::dsp::SamplePlaybackSettings copiedPlayback;
        sms::dsp::SampleMixerSettings copiedMixer;
        check(engine.exportPad(0, exported, &copiedPlayback, &copiedMixer) &&
              exported.stereo == source.stereo && exported.frames == source.frames &&
              exported.sampleRate == source.sampleRate && exported.generation != 0 &&
              same(copiedPlayback, playback) && same(copiedMixer, mixer),
              "export returns coherent PCM, generation, playback, and mixer settings");
        sms::audio::WaveformSummary waveform;
        check(engine.summarizePadRange(0, 1, 0, source.frames, waveform),
              "summarize long pad during performance");
        const auto expected = sms::audio::summarizeStereo(
            0, source.stereo.data(), source.frames, source.sampleRate);
        check(waveform.frames == expected.frames && waveform.minimum == expected.minimum &&
              waveform.maximum == expected.maximum, "waveform snapshot matches every bin");
        check(clipboard.copyFrom(engine, 0) && clipboard.pasteTo(engine, 4),
              "clipboard copy/paste during performance");
        check(engine.exportPad(4, exported, &copiedPlayback, &copiedMixer) &&
              exported.stereo == source.stereo && same(copiedPlayback, playback) &&
              same(copiedMixer, mixer), "clipboard preserves complete sample snapshot");

        std::array<std::uint64_t, 2> generations{};
        audio.dispatcher.invoke([&]() noexcept {
            generations = {engine.padMetadata(0).generation, engine.padMetadata(1).generation};
        });
        const auto split = source.frames / 2;
        check(engine.splitPadAndShiftRight(0, 1, split, generations),
              "split long pad during performance");
        const std::array<std::int64_t, 2> offsets{0, -17};
        check(engine.rechopPads(0, 3, offsets), "rechop split pads during performance");
        check(engine.exportPad(0, left) && engine.exportPad(1, middle) &&
              engine.exportPad(2, right), "export repartitioned long sample");
        check(left.frames == split && middle.frames == source.frames - split - 17 &&
              right.frames == 17, "split/rechop conserves source length");
        std::vector<float> combined = left.stereo;
        combined.insert(combined.end(), middle.stereo.begin(), middle.stereo.end());
        combined.insert(combined.end(), right.stereo.begin(), right.stereo.end());
        check(combined == source.stereo, "split/rechop preserves every PCM frame");
        audio.dispatcher.invoke([&]() noexcept {
            engine.clearPad(0); engine.clearPad(1); engine.clearPad(2); engine.clearPad(4);
        });
        check(audio.blocks.load() > before + 16, "audio overlaps every transfer sequence");
    }
    check(audio.captures >= 30, "heavy operations dispatch capture/commit commands");
    audio.stop();
}

void retainedSnapshotsAndStalePlans()
{
    SamplerEngine engine(kRate, 2.0);
    const auto original = sample(250000);
    const auto replacement = sample(250000, 0.5f);
    setPerformancePad(engine);
    check(engine.importPad(0, original), "install retained-reader source");
    sms::dsp::SamplePlaybackSettings playback;
    playback.start = 0.25f; playback.end = 0.75f; playback.sustainLevel = 0.5f;
    sms::dsp::SampleMixerSettings mixer;
    mixer.pan = -0.5f; mixer.gainDecibels = -3.0f;
    engine.setPadPlaybackSettings(0, playback);
    engine.setPadMixerSettings(0, mixer);
    const auto generation = engine.padMetadata(0).generation;
    AudioHarness audio(engine);
    struct Replacement { SamplerEngine* engine; const PadData* sample; } context{&engine, &replacement};
    audio.afterContext = &context;
    audio.afterCapture = [](void* opaque) {
        auto& replacementContext = *static_cast<Replacement*>(opaque);
        check(replacementContext.engine->importPad(0, *replacementContext.sample),
              "replace pad while old snapshot is retained");
    };
    PadData captured;
    sms::dsp::SamplePlaybackSettings capturedPlayback;
    sms::dsp::SampleMixerSettings capturedMixer;
    check(engine.exportPad(0, captured, &capturedPlayback, &capturedMixer) &&
          captured.stereo == original.stereo && captured.generation == generation &&
          same(capturedPlayback, playback) && same(capturedMixer, mixer),
          "retained export survives replacement with coherent old PCM/settings");
    check(engine.exportPad(0, captured) && captured.stereo == replacement.stereo &&
          captured.generation != generation, "replacement publishes a fresh snapshot");
    const std::array<std::uint64_t, 2> stale{generation, 0};
    check(!engine.splitPadAndShiftRight(0, 1, original.frames / 2, stale),
          "stale split generation is rejected");
    check(engine.exportPad(0, captured) && captured.stereo == replacement.stereo,
          "stale split leaves replacement audio intact");
    std::array<std::uint64_t, 2> current{};
    audio.dispatcher.invoke([&]() noexcept {
        current = {engine.padMetadata(0).generation, engine.padMetadata(1).generation};
    });
    audio.afterContext = &engine;
    audio.afterCapture = [](void* opaque) {
        sms::dsp::SampleMixerSettings changed;
        changed.pan = 0.5f;
        static_cast<SamplerEngine*>(opaque)->setPadMixerSettings(0, changed);
    };
    check(!engine.splitPadAndShiftRight(0, 1, original.frames / 2, current),
          "split revalidates generations after staging its PCM");
    check(engine.exportPad(0, captured) && captured.stereo == replacement.stereo,
          "rejected staged split preserves replacement audio");
    auto tooLarge = sample(2000000);
    check(!engine.importPad(0, tooLarge), "over-capacity import is rejected");
    check(engine.exportPad(0, captured) && captured.stereo == replacement.stereo,
          "capacity rejection preserves published pad");
    const auto large = sample(900000);
    check(engine.importPad(0, large), "one pad accepts most of the shared capacity");
    check(!engine.importPad(1, large), "sum of imported pad lengths respects shared capacity");
    check(engine.exportPad(0, captured) && captured.stereo == large.stereo &&
          engine.exportPad(1, captured) && captured.frames == 0,
          "shared-capacity rejection changes neither source nor destination");
    audio.stop();
}

void recordingContinuity()
{
    SamplerEngine engine(kRate, 2.0);
    AudioHarness audio(engine, true);
    const auto source = sample(250000);
    PadData exported;
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        const auto before = audio.blocks.load();
        audio.blockLimit.store(before + 32);
        check(engine.importPad(0, source) && engine.exportPad(0, exported) &&
              exported.stereo == source.stereo, "unrelated transfer during active recording");
        await([&] { return audio.blocks.load() >= before + 32; },
              "active recording keeps processing during transfer");
        bool recording = false;
        audio.dispatcher.invoke([&]() noexcept { recording = engine.padMetadata(32).recording; });
        check(recording, "unrelated transfer does not finalize recording");
    }
    const auto blocks = audio.blockLimit.load();
    await([&] { return audio.blocks.load() == blocks; }, "recording reaches deterministic end");
    engine.finalizeRecording();
    audio.dispatcher.invoke([&]() noexcept {
        float left = 0, right = 0;
        engine.process(nullptr, nullptr, &left, &right, 0);
    });
    struct RetainedRecording { SamplerEngine* engine; AudioHarness* audio; } retained{&engine, &audio};
    audio.afterContext = &retained;
    audio.afterCapture = [](void* opaque) {
        auto& retainedContext = *static_cast<RetainedRecording*>(opaque);
        retainedContext.audio->dispatcher.invoke([&]() noexcept {
            auto& sampler = *retainedContext.engine;
            sampler.clearPad(32);
            sampler.selectCaptureTarget(32);
            std::array<float, kFrames> left{}, right{}, outputLeft{}, outputRight{};
            left.fill(0.75f); right.fill(-0.5f);
            const midichopper::MidiEvent note{0, 36, 127, midichopper::MidiEventType::NoteOn};
            for (unsigned block = 0; block < 16; ++block)
                sampler.process(left.data(), right.data(), outputLeft.data(), outputRight.data(),
                                kFrames, block == 0 ? &note : nullptr, block == 0 ? 1 : 0);
            sampler.finalizeRecording();
            sampler.process(nullptr, nullptr, outputLeft.data(), outputRight.data(), 0);
        });
    };
    check(engine.exportPad(32, exported) && exported.frames == blocks * kFrames - 11,
          "recording preserves all blocks and exact MIDI start offset");
    for (std::uint32_t frame = 0; frame < exported.frames; ++frame) {
        const auto inputFrame = (frame + 11) % kFrames;
        check(exported.stereo[static_cast<std::size_t>(frame) * 2] ==
                  static_cast<float>(inputFrame + 1) / 1024.0f &&
              exported.stereo[static_cast<std::size_t>(frame) * 2 + 1] ==
                  -static_cast<float>(inputFrame + 1) / 2048.0f,
              "active recording contains continuous exact stereo input");
    }
    check(engine.exportPad(32, exported) && exported.frames == 16 * kFrames &&
          exported.stereo.front() == 0.75f && exported.stereo.back() == -0.5f,
          "new capture uses separate blocks while the old recording snapshot is retained");
    audio.stop();
}

void dispatcherLifecycle()
{
    RealtimeCommandDispatcher dispatcher;
    const auto caller = std::this_thread::get_id();
    unsigned executions = 0;
    dispatcher.invoke([&]() noexcept {
        check(std::this_thread::get_id() == caller, "inactive commands execute on control caller");
        ++executions;
    });
    dispatcher.activate();
    bool timedOut = false;
    try {
        // The dispatch context is on this stack. A canceled pending command
        // must never be touched by a later callback.
        dispatcher.invoke([&]() noexcept { ++executions; });
    } catch (const std::runtime_error&) { timedOut = true; }
    check(timedOut && executions == 1, "unserviced active command times out");
    allocation_probe::audioThread = true;
    dispatcher.service();
    allocation_probe::audioThread = false;
    check(executions == 1, "timeout leaves no abandoned stack command for later service");
    dispatcher.deactivate();
    dispatcher.invoke([&]() noexcept { ++executions; });
    check(executions == 2, "inactive dispatch recovers after canceled pending command");

    dispatcher.activate();
    std::atomic<bool> entered{false}, release{false}, returned{false};
    std::thread control([&] {
        dispatcher.invoke([&]() noexcept {
            entered.store(true);
            while (!release.load()) std::this_thread::yield();
        });
        returned.store(true);
    });
    std::thread audio([&] {
        allocation_probe::audioThread = true;
        while (!entered.load()) { dispatcher.service(); std::this_thread::yield(); }
        allocation_probe::audioThread = false;
    });
    await([&] { return entered.load(); }, "audio claims in-flight command");
    // Cross the pending timeout while a command has already been claimed.
    // The entered/release handshake, not elapsed time, establishes overlap.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2100);
    while (std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    check(!returned.load(), "claimed command keeps its stack context alive past timeout");
    release.store(true);
    audio.join(); control.join();
    check(returned.load(), "claimed command returns only after execution completes");
    dispatcher.shutdown();
    bool stopped = false;
    try { dispatcher.invoke([]() noexcept {}); }
    catch (const std::runtime_error&) { stopped = true; }
    check(stopped, "shutdown rejects later commands");
    dispatcher.service();
}

void publishedStateWithoutCallbacks()
{
    SamplerEngine engine(kRate, 1.0);
    const auto source = sample(8192);
    sms::dsp::SamplePlaybackSettings playback;
    playback.start = 0.125f; playback.end = 0.75f; playback.sustainLevel = 0.625f;
    sms::dsp::SampleMixerSettings mixer;
    mixer.pan = -0.5f; mixer.gainDecibels = -6;
    check(engine.importPad(0, source, false, &playback, &mixer),
          "install complete published state transaction");
    RealtimeCommandDispatcher dispatcher;
    dispatcher.activate();
    engine.setControlDispatcher(&dispatcher, RealtimeCommandDispatcher::dispatchFromEngine);
    PadData exported;
    sms::dsp::SamplePlaybackSettings copiedPlayback;
    sms::dsp::SampleMixerSettings copiedMixer;
    check(engine.exportPublishedPad(0, exported, &copiedPlayback, &copiedMixer) &&
          exported.stereo == source.stereo && same(copiedPlayback, playback) &&
          same(copiedMixer, mixer), "published state reads do not require active host callbacks");
    engine.snapshotPadSettings(0, copiedPlayback, copiedMixer);
    check(same(copiedPlayback, playback) && same(copiedMixer, mixer),
          "settings state reads do not dispatch to a stopped host");
    check(engine.exportPublishedPad(1, exported) && exported.frames == 0 && exported.stereo.empty(),
          "stopped-host state reads preserve empty pads");
    dispatcher.shutdown();
    engine.setControlDispatcher(nullptr, nullptr);
}

void concurrentImportedPublication()
{
    SamplerEngine engine(kRate, 1.0);
    const auto first = sample(8192), second = sample(16384, 0.5f);
    sms::dsp::SamplePlaybackSettings firstPlayback, secondPlayback;
    firstPlayback.start = 0.125f; firstPlayback.end = 0.75f;
    secondPlayback.start = 0.25f; secondPlayback.end = 0.875f;
    sms::dsp::SampleMixerSettings firstMixer, secondMixer;
    firstMixer.pan = -0.5f; firstMixer.gainDecibels = -3;
    secondMixer.pan = 0.5f; secondMixer.gainDecibels = -9;
    check(engine.importPad(0, first, false, &firstPlayback, &firstMixer),
          "install first published import revision");
    setPerformancePad(engine);
    AudioHarness audio(engine);
    std::atomic<bool> finished{false};
    std::atomic<unsigned> reads{0};
    std::thread reader([&] {
        PadData exported;
        sms::dsp::SamplePlaybackSettings playback;
        sms::dsp::SampleMixerSettings mixer;
        std::uint64_t priorGeneration = 0;
        while (!finished.load() || reads.load() < 2000) {
            check(engine.exportPublishedPad(0, exported, &playback, &mixer),
                  "read imported revision concurrently with replacements");
            const bool isFirst = exported.frames == first.frames;
            check((isFirst && exported.stereo == first.stereo && same(playback, firstPlayback) &&
                   same(mixer, firstMixer)) ||
                  (!isFirst && exported.frames == second.frames && exported.stereo == second.stereo &&
                   same(playback, secondPlayback) && same(mixer, secondMixer)),
                  "published import snapshot never mixes PCM and settings revisions");
            check(exported.generation >= priorGeneration, "published generations never move backwards");
            priorGeneration = exported.generation;
            engine.snapshotPadSettings(0, playback, mixer);
            check((same(playback, firstPlayback) && same(mixer, firstMixer)) ||
                  (same(playback, secondPlayback) && same(mixer, secondMixer)),
                  "published settings snapshot never combines transactions");
            reads.fetch_add(1);
        }
    });
    await([&] { return reads.load() >= 1; }, "published import reader starts");
    for (unsigned revision = 0; revision < 128; ++revision) {
        const bool isFirst = revision % 2 == 0;
        check(engine.importPad(0, isFirst ? first : second, false,
                               isFirst ? &firstPlayback : &secondPlayback,
                               isFirst ? &firstMixer : &secondMixer),
              "publish alternating complete import transactions");
    }
    finished.store(true);
    reader.join();
    audio.stop();
}

void concurrentCapturedPublication()
{
    SamplerEngine engine(kRate, 1.0);
    auto settings = engine.settings();
    settings.armed = true;
    engine.setSettings(settings);
    std::atomic<bool> finished{false};
    std::atomic<unsigned> publications{0};
    std::thread writer([&] {
        std::array<float, kFrames> inputLeft{}, inputRight{}, outputLeft{}, outputRight{};
        const midichopper::MidiEvent note{0, 36, 127, midichopper::MidiEventType::NoteOn};
        allocation_probe::audioThread = true;
        while (!finished.load() || publications.load() < 1000) {
            const bool first = publications.load() % 2 == 0;
            inputLeft.fill(first ? 0.125f : 0.375f);
            inputRight.fill(first ? -0.25f : -0.5f);
            engine.clearPad(0);
            engine.selectCaptureTarget(0);
            engine.process(inputLeft.data(), inputRight.data(), outputLeft.data(), outputRight.data(),
                           kFrames, &note, 1);
            engine.finalizeRecording();
            engine.process(nullptr, nullptr, outputLeft.data(), outputRight.data(), 0);
            publications.fetch_add(1);
            std::this_thread::yield();
        }
        allocation_probe::audioThread = false;
    });
    await([&] { return publications.load() > 0; }, "captured publication writer starts");
    PadData exported;
    sms::dsp::SamplePlaybackSettings playback;
    sms::dsp::SampleMixerSettings mixer;
    std::uint64_t priorGeneration = 0;
    unsigned occupiedReads = 0;
    for (unsigned read = 0; read < 5000 || occupiedReads < 128; ++read) {
        check(engine.exportPublishedPad(0, exported, &playback, &mixer),
              "read published captured storage while it is cleared and reused");
        check(exported.generation >= priorGeneration, "capture snapshot generation is monotonic");
        priorGeneration = exported.generation;
        check(same(playback, {}) && same(mixer, {}), "captured snapshot keeps default settings");
        if (exported.frames == 0) {
            check(exported.stereo.empty(), "clear/recording publication exports no incomplete audio");
            continue;
        }
        ++occupiedReads;
        check(exported.frames == kFrames && exported.sampleRate == kRate,
              "captured publication exposes only complete recordings");
        const bool first = exported.stereo[0] == 0.125f;
        const float left = first ? 0.125f : 0.375f, right = first ? -0.25f : -0.5f;
        for (std::uint32_t frame = 0; frame < kFrames; ++frame)
            check(exported.stereo[frame * 2] == left && exported.stereo[frame * 2 + 1] == right,
                  "pinned captured snapshot contains one immutable PCM revision");
        check(exported.peak == -right &&
              std::abs(exported.rms - std::sqrt((left * left + right * right) * 0.5f)) < 1.0e-6f,
              "captured snapshot PCM and metadata share the same revision");
    }
    finished.store(true);
    writer.join();
    check(allocation_probe::allocations.load() == 0 && allocation_probe::deletions.load() == 0,
          "capture publication and block reuse allocate and destroy nothing on audio");
}

void ordinaryChopSnapshots()
{
    SamplerEngine engine(kRate, 1.0);
    const auto first = sample(8192), second = sample(4096, 0.5f);
    check(engine.importPad(0, first) && engine.importPad(1, second),
          "install ordinary chop source");
    setPerformancePad(engine);
    AudioHarness audio(engine);
    std::array<sms::audio::WaveformSummary, 3> summaries{};
    std::array<std::uint64_t, 3> generations{};
    // Replacing a pad after capture verifies all summaries describe the same
    // retained baseline, including the generation used by Apply.
    struct Replace { SamplerEngine* sampler; const PadData* sample; } replacement{&engine, &second};
    audio.afterContext = &replacement;
    audio.afterCapture = [](void* opaque) {
        auto& context = *static_cast<Replace*>(opaque);
        check(context.sampler->importPad(0, *context.sample), "replace source after chop baseline capture");
    };
    check(engine.getChopSnapshot(0, 3, summaries, generations), "capture ordinary chop baseline");
    const auto firstExpected = sms::audio::summarizeStereo(0, first.stereo.data(), first.frames);
    const auto secondExpected = sms::audio::summarizeStereo(1, second.stereo.data(), second.frames);
    check(summaries[0].frames == first.frames && summaries[0].minimum == firstExpected.minimum &&
          summaries[0].maximum == firstExpected.maximum && summaries[1].frames == second.frames &&
          summaries[1].minimum == secondExpected.minimum && summaries[1].maximum == secondExpected.maximum &&
          summaries[2].frames == 0 && generations[0] != 0,
          "ordinary chop retains coherent summaries and generations despite replacement");
    const std::array<std::int64_t, 2> offsets{17, 0};
    check(!engine.rechopPads(0, 3, offsets, generations), "ordinary rechop rejects stale expected generations");
    PadData exported;
    check(engine.exportPublishedPad(0, exported) && exported.stereo == second.stereo,
          "stale ordinary rechop preserves replacement");
    check(engine.getChopSnapshot(0, 3, summaries, generations), "refresh ordinary baseline");
    audio.afterContext = &engine;
    audio.afterCapture = [](void* opaque) {
        sms::dsp::SampleMixerSettings changed;
        changed.pan = 0.375f;
        static_cast<SamplerEngine*>(opaque)->setPadMixerSettings(0, changed);
    };
    check(!engine.rechopPads(0, 3, offsets, generations),
          "ordinary rechop revalidates settings generations after staging");
    check(engine.exportPublishedPad(0, exported) && exported.stereo == second.stereo,
          "ordinary staged rejection preserves source PCM");
    check(engine.getChopSnapshot(0, 3, summaries, generations) &&
          engine.rechopPads(0, 3, offsets, generations), "fresh ordinary baseline can apply");
    audio.stop();
    auto armed = engine.settings(); armed.armed = true; engine.setSettings(armed);
    check(!engine.getChopSnapshot(0, 3, summaries, generations), "ordinary chop baseline rejects armed state");
}

void resumePendingState(SamplerEngine& engine)
{
    std::array<float, kFrames> left{}, right{};
    allocation_probe::audioThread = true;
    engine.process(nullptr, nullptr, left.data(), right.data(), kFrames);
    allocation_probe::audioThread = false;
    check(allocation_probe::allocations.load() == 0 && allocation_probe::deletions.load() == 0,
          "durable restore commits allocate and destroy nothing on audio");
}

void suspendedHostRestore()
{
    SamplerEngine engine(kRate, 0.001);
    RealtimeCommandDispatcher dispatcher;
    dispatcher.activate();
    engine.setControlDispatcher(&dispatcher, RealtimeCommandDispatcher::dispatchFromEngine);
    std::array<PadData, midichopper::kPadCount> sources;
    std::array<sms::dsp::SamplePlaybackSettings, midichopper::kPadCount> playback;
    std::array<sms::dsp::SampleMixerSettings, midichopper::kPadCount> mixers;
    for (unsigned pad = 0; pad < midichopper::kPadCount; ++pad) {
        sources[pad] = sample(64 + pad, static_cast<float>(pad + 1) / 64);
        playback[pad].start = 0.125f; playback[pad].end = 0.875f;
        playback[pad].sustainLevel = static_cast<float>(pad + 1) / 64;
        mixers[pad].pan = static_cast<float>(pad) / 128;
        mixers[pad].gainDecibels = -static_cast<float>(pad % 12);
        check(engine.restorePad(pad, &sources[pad]), "queue suspended-host pad PCM without callback");
        engine.restorePadPlaybackSettings(pad, playback[pad]);
        engine.restorePadMixerSettings(pad, mixers[pad]);
    }
    const auto validate = [&] {
        PadData exported;
        sms::dsp::SamplePlaybackSettings actualPlayback;
        sms::dsp::SampleMixerSettings actualMixer;
        for (unsigned pad = 0; pad < midichopper::kPadCount; ++pad) {
            check(engine.exportPublishedPad(pad, exported, &actualPlayback, &actualMixer) &&
                  exported.stereo == sources[pad].stereo && same(actualPlayback, playback[pad]) &&
                  same(actualMixer, mixers[pad]), "suspended restore reads desired complete pad state immediately");
            engine.snapshotPadSettings(pad, actualPlayback, actualMixer);
            check(same(actualPlayback, playback[pad]) && same(actualMixer, mixers[pad]),
                  "suspended settings snapshots overlay queued complete state");
        }
    };
    validate();
    resumePendingState(engine);
    check(!engine.stateRestoreFailed(), "64-pad restore fits the shared capacity");
    validate();
    check(engine.restorePad(5, nullptr), "queue suspended pad clear");
    PadData cleared;
    sms::dsp::SamplePlaybackSettings clearedPlayback;
    sms::dsp::SampleMixerSettings clearedMixer;
    check(engine.exportPublishedPad(5, cleared, &clearedPlayback, &clearedMixer) &&
          cleared.frames == 0 && same(clearedPlayback, {}) && same(clearedMixer, {}),
          "queued clear immediately exports empty PCM and default settings");
    resumePendingState(engine);
    check(engine.exportPublishedPad(5, cleared) && cleared.frames == 0,
          "resuming callbacks commits the queued clear");
    dispatcher.shutdown();
    engine.setControlDispatcher(nullptr, nullptr);
}

void durableRestoreCapacityAndConsumption()
{
    SamplerEngine engine(kRate, 0.001); // Minimum shared pool: 64 blocks of 1,024 frames.
    const auto full = sample(64 * 1024), small = sample(64, 0.5f);
    check(engine.importPad(63, full), "fill shared capacity before durable restore");
    RealtimeCommandDispatcher dispatcher;
    dispatcher.activate();
    engine.setControlDispatcher(&dispatcher, RealtimeCommandDispatcher::dispatchFromEngine);
    check(engine.restorePad(0, &full) && engine.restorePad(63, nullptr),
          "queue full-capacity move from later pad to earlier pad");
    resumePendingState(engine);
    PadData exported;
    check(!engine.stateRestoreFailed() && engine.exportPublishedPad(0, exported) &&
          exported.stereo == full.stereo && engine.exportPublishedPad(63, exported) && exported.frames == 0,
          "restore clears old full pad before admitting earlier-index full replacement");
    sms::dsp::SampleMixerSettings changed;
    changed.pan = 0.5f; changed.gainDecibels = -6;
    engine.restorePadMixerSettings(0, changed);
    check(engine.restorePad(1, &small), "queue syntactically valid over-capacity batch");
    resumePendingState(engine);
    sms::dsp::SampleMixerSettings actualMixer;
    sms::dsp::SamplePlaybackSettings actualPlayback;
    check(engine.stateRestoreFailed() && engine.exportPublishedPad(0, exported, &actualPlayback, &actualMixer) &&
          exported.stereo == full.stereo && same(actualMixer, {}) &&
          engine.exportPublishedPad(1, exported) && exported.frames == 0,
          "invalid aggregate restore rejects PCM and settings atomically");

    allocation_probe::audioThread = true;
    engine.clearPad(0);
    allocation_probe::audioThread = false;
    engine.restorePadMixerSettings(0, changed);
    check(engine.exportPublishedPad(0, exported) && exported.frames == 0,
          "settings-only mailbox never replays PCM from a consumed restore");
    resumePendingState(engine);
    check(!engine.stateRestoreFailed() && engine.exportPublishedPad(0, exported, &actualPlayback, &actualMixer) &&
          exported.frames == 0 && same(actualMixer, changed),
          "settings-only commit preserves subsequent audio-owner clear");

    check(engine.restorePad(0, &small), "queue PCM before reset");
    engine.restorePadMixerSettings(0, changed);
    allocation_probe::audioThread = true;
    engine.reset();
    allocation_probe::audioThread = false;
    resumePendingState(engine);
    check(engine.exportPublishedPad(0, exported, &actualPlayback, &actualMixer) &&
          exported.frames == 0 && same(actualPlayback, {}) && same(actualMixer, changed),
          "reset clears queued PCM while retaining applied settings through later processing");
    dispatcher.shutdown();
    engine.setControlDispatcher(nullptr, nullptr);
}

void concurrentDurablePublication()
{
    SamplerEngine engine(kRate, 1.0);
    const auto first = sample(8192), second = sample(16384, 0.5f);
    sms::dsp::SamplePlaybackSettings firstPlayback, secondPlayback;
    firstPlayback.start = 0.125f; firstPlayback.end = 0.75f; firstPlayback.sustainLevel = 0.625f;
    secondPlayback.start = 0.25f; secondPlayback.end = 0.875f; secondPlayback.sustainLevel = 0.875f;
    sms::dsp::SampleMixerSettings firstMixer, secondMixer;
    firstMixer.pan = -0.5f; firstMixer.gainDecibels = -3;
    secondMixer.pan = 0.5f; secondMixer.gainDecibels = -9;
    check(engine.importPad(0, first, false, &firstPlayback, &firstMixer), "install durable reader baseline");
    setPerformancePad(engine);
    AudioHarness audio(engine);
    std::atomic<bool> finished{false};
    std::atomic<unsigned> reads{0};
    std::thread reader([&] {
        PadData exported;
        sms::dsp::SamplePlaybackSettings playback;
        sms::dsp::SampleMixerSettings mixer;
        while (!finished.load() || reads.load() < 2000) {
            check(engine.exportPublishedPad(0, exported, &playback, &mixer), "read queued durable state concurrently");
            check(exported.stereo == first.stereo || exported.stereo == second.stereo,
                  "queued/committed durable PCM is one complete immutable revision");
            // These are independent host state keys, so a callback may commit
            // between the PCM, playback and mixer calls. Each setting key must
            // still be complete rather than a mixture of its individual fields.
            check((same(playback, firstPlayback) || same(playback, secondPlayback)) &&
                  (same(mixer, firstMixer) || same(mixer, secondMixer)),
                  "queued/committed durable settings expose only complete keys");
            engine.snapshotPadSettings(0, playback, mixer);
            check((same(playback, firstPlayback) || same(playback, secondPlayback)) &&
                  (same(mixer, firstMixer) || same(mixer, secondMixer)),
                  "durable settings state snapshot is coherent during mailbox consumption");
            reads.fetch_add(1);
        }
    });
    await([&] { return reads.load() != 0; }, "durable state reader starts");
    for (unsigned revision = 0; revision < 128; ++revision) {
        const bool firstRevision = revision % 2 == 0;
        check(engine.restorePad(0, firstRevision ? &first : &second), "queue active durable PCM revision");
        engine.restorePadPlaybackSettings(0, firstRevision ? firstPlayback : secondPlayback);
        engine.restorePadMixerSettings(0, firstRevision ? firstMixer : secondMixer);
    }
    finished.store(true);
    reader.join();
    PadData exported;
    sms::dsp::SamplePlaybackSettings playback;
    sms::dsp::SampleMixerSettings mixer;
    check(engine.exportPad(0, exported, &playback, &mixer) && exported.stereo == second.stereo &&
          same(playback, secondPlayback) && same(mixer, secondMixer),
          "bounded command commits final durable state before returning its snapshot");
    audio.stop();
}

void pendingRestoreLifecycle()
{
    const auto source = sample(8192);
    for (unsigned lifetime = 0; lifetime < 16; ++lifetime) {
        SamplerEngine engine(kRate, 0.001);
        RealtimeCommandDispatcher dispatcher;
        dispatcher.activate();
        engine.setControlDispatcher(&dispatcher, RealtimeCommandDispatcher::dispatchFromEngine);
        for (unsigned revision = 0; revision < 16; ++revision) {
            check(engine.restorePad(0, &source), "queue unconsumed PCM for lifecycle test");
            sms::dsp::SamplePlaybackSettings playback;
            playback.sustainLevel = static_cast<float>(revision + 1) / 16;
            engine.restorePadPlaybackSettings(0, playback);
        }
        // No callback consumes this mailbox. Sanitizers cover destruction of
        // its owned request records and retained imported storage at teardown.
        dispatcher.shutdown();
        engine.setControlDispatcher(nullptr, nullptr);
    }
}

PadData readOptionalWav(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    check(static_cast<bool>(stream), "open optional WAV input");
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),
                                          std::istreambuf_iterator<char>()};
    auto decoded = sms::audio::decodeWav(bytes);
    check(static_cast<bool>(decoded), "decode optional WAV input");
    PadData result;
    result.sampleRate = decoded.audio.sampleRate;
    result.frames = decoded.audio.frames;
    result.stereo = std::move(decoded.audio.stereo);
    for (float value : result.stereo) result.peak = std::max(result.peak, std::abs(value));
    result.rms = result.peak * 0.5f;
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    check(argc <= 2, "usage: anvilsampler_transfer_tests [optional.wav]");
    dispatcherLifecycle();
    publishedStateWithoutCallbacks();
    concurrentImportedPublication();
    concurrentCapturedPublication();
    ordinaryChopSnapshots();
    suspendedHostRestore();
    durableRestoreCapacityAndConsumption();
    concurrentDurablePublication();
    pendingRestoreLifecycle();
    transferContinuity(sample(1500000));
    retainedSnapshotsAndStalePlans();
    recordingContinuity();
    if (argc == 2) transferContinuity(readOptionalWav(argv[1]));
    std::cout << "Transfer continuity tests passed (48 kHz, 64-frame blocks).\n";
}
