#include "../src/core/SamplerEngine.hpp"
#include "../src/plugin/Parameters.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
void close(float a, float b, const char* message) { check(std::abs(a - b) < 1.0e-5f, message); }

void sequential_boundaries_and_preroll() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    midichopper::EngineSettings s;
    s.armed = true; s.preRollMilliseconds = 2.0f; s.monitorInput = false;
    e.setSettings(s);

    float inL[] = {1, 2, 3}; float inR[] = {1, 2, 3}; float outL[3]{}; float outR[3]{};
    midichopper::MidiEvent first{2, 90, 127, midichopper::MidiEventType::NoteOn};
    e.process(inL, inR, outL, outR, 3, &first, 1);
    float in2[] = {4, 5}; float out2[2]{}; float out2r[2]{};
    midichopper::MidiEvent second{1, 91, 127, midichopper::MidiEventType::NoteOn};
    e.process(in2, in2, out2, out2r, 2, &second, 1);
    e.finalizeRecording();
    e.process(nullptr, nullptr, out2, out2r, 0);

    midichopper::PadData p0, p1;
    check(e.exportPad(0, p0) && e.exportPad(1, p1), "export sequential pads");
    check(p0.frames == 2 && p1.frames == 3, "boundary trim and finalization lengths");
    close(p0.stereo[0], 1, "pad zero starts with preroll history");
    close(p0.stereo[2], 2, "pad zero boundary trim");
    close(p1.stereo[0], 3, "pad one starts at adjusted boundary");
    close(p1.stereo[4], 5, "pad one contains post-boundary audio");
}

void full_bank_and_undo() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    auto s = e.settings(); s.armed = true; s.monitorInput = false; e.setSettings(s);
    float in[65]{}; float out[65]{};
    std::vector<midichopper::MidiEvent> events;
    for (unsigned i = 0; i < 65; ++i) events.emplace_back(i, 60, 100, midichopper::MidiEventType::NoteOn);
    e.process(in, in, out, out, 65, events.data(), static_cast<std::uint32_t>(events.size()));
    e.finalizeRecording(); e.process(nullptr, nullptr, out, out, 0);
    check(e.padMetadata(0).occupied && e.padMetadata(63).occupied, "all four banks committed");
    check(!e.padMetadata(0).recording && !e.padMetadata(63).recording, "final bank stops safely");
    e.undoLastSlice(); e.process(nullptr, nullptr, out, out, 0);
    check(!e.padMetadata(63).occupied && e.padMetadata(62).occupied, "undo removes only last slice");
    e.clearAllPads();
    for (unsigned i = 0; i < midichopper::kPadCount; ++i) check(!e.padMetadata(i).occupied, "clear all pads");
}

void bank_and_layout_mapping() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    auto settings = e.settings();
    settings.midiBankMode = midichopper::MidiBankMode::SelectedBank;
    settings.armed = true;
    settings.monitorInput = false;
    settings.padsPerBank = 12;
    e.setSettings(settings);

    float input[13]{};
    float output[13]{};
    std::vector<midichopper::MidiEvent> boundaries;
    for (unsigned frame = 0; frame < 13; ++frame)
        boundaries.emplace_back(frame, 60, 127, midichopper::MidiEventType::NoteOn);
    e.process(input, input, output, output, 13, boundaries.data(),
              static_cast<std::uint32_t>(boundaries.size()));
    e.finalizeRecording();
    e.process(nullptr, nullptr, output, output, 0);
    check(e.padMetadata(11).occupied && !e.padMetadata(12).occupied &&
          e.padMetadata(16).occupied,
          "12-pad capture skips hidden slots when advancing banks");

    settings.armed = false;
    settings.activeBank = 1;
    e.setSettings(settings);
    midichopper::PadData pad;
    pad.sampleRate = 1000.0;
    pad.frames = 4;
    pad.stereo.assign(8U, 1.0f);
    pad.peak = pad.rms = 1.0f;
    check(e.importPad(16, pad), "import pad in second bank");
    const midichopper::MidiEvent play{0, 36, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, output, output, 1, &play, 1);
    check(e.padMetadata(16).active, "MIDI playback targets the selected bank");

    settings.padsPerBank = 8;
    settings.activeBank = 2;
    e.setSettings(settings);
    check(e.importPad(32, pad), "import pad in eight-pad bank");
    e.process(nullptr, nullptr, output, output, 1, &play, 1);
    check(e.padMetadata(32).active, "eight-pad layout maps from the bank base");

    settings.padsPerBank = 16;
    settings.activeBank = 0;
    e.setSettings(settings);
    check(e.importPad(12, pad), "import pad hidden by smaller layouts");
    settings.padsPerBank = 12;
    e.setSettings(settings);
    const midichopper::MidiEvent hiddenPlay{0, 48, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, output, output, 1, &hiddenPlay, 1);
    check(!e.padMetadata(12).active, "hidden pad is not MIDI-addressable");
    settings.padsPerBank = 16;
    e.setSettings(settings);
    e.process(nullptr, nullptr, output, output, 1, &hiddenPlay, 1);
    check(e.padMetadata(12).active, "restoring the layout reveals preserved pad audio");
}

void all_bank_midi_mapping() {
    midichopper::SamplerEngine defaultEngine(1000.0, 1.0);
    check(midichopper::kDefaultMidiBankMode == midichopper::MidiBankMode::AllBanks &&
          defaultEngine.settings().midiBankMode == midichopper::MidiBankMode::AllBanks,
          "All Banks is the engine default MIDI mapping");

    check(midichopper::midiNoteForPad(0, midichopper::kDefaultBaseMidiNote,
              midichopper::MidiBankMode::AllBanks) == 36 &&
          midichopper::midiNoteForPad(16, midichopper::kDefaultBaseMidiNote,
              midichopper::MidiBankMode::AllBanks) == 52 &&
          midichopper::midiNoteForPad(63, midichopper::kDefaultBaseMidiNote,
              midichopper::MidiBankMode::AllBanks) == 99,
          "UI-facing all-bank note mapping matches the four fixed ranges");
    check(midichopper::midiNoteForPad(63, midichopper::kDefaultBaseMidiNote,
              midichopper::MidiBankMode::SelectedBank) == 51,
          "UI-facing selected-bank mapping reuses the local note range");

    midichopper::SamplerEngine e(1000.0, 1.0);
    auto settings = e.settings();
    settings.monitorInput = false;
    settings.playbackMode = midichopper::PlaybackMode::Gated;
    settings.midiBankMode = midichopper::MidiBankMode::AllBanks;
    e.setSettings(settings);

    midichopper::PadData pad;
    pad.sampleRate = 1000.0;
    pad.frames = 16;
    pad.stereo.assign(32U, 1.0f);
    pad.peak = pad.rms = 1.0f;
    check(e.importPad(0, pad) && e.importPad(16, pad) && e.importPad(63, pad),
          "import pads across all MIDI banks");

    float outputLeft[2]{};
    float outputRight[2]{};
    const midichopper::MidiEvent bankBOn{0, 52, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &bankBOn, 1);
    check(e.padMetadata(16).active && !e.padMetadata(0).active,
          "all-bank mode uses a fixed 16-note bank stride");

    settings.activeBank = 3;
    e.setSettings(settings);
    const midichopper::MidiEvent bankBOff{0, 52, 0, midichopper::MidiEventType::NoteOff};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &bankBOff, 1);
    check(!e.padMetadata(16).active,
          "all-bank note-off ignores visible bank changes");

    const midichopper::MidiEvent bankAOn{0, 36, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &bankAOn, 1);
    check(e.padMetadata(0).active,
          "all-bank note-on ignores the selected view bank");
    const midichopper::MidiEvent bankAOff{0, 36, 0, midichopper::MidiEventType::NoteOff};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &bankAOff, 1);

    const midichopper::MidiEvent lastOn{0, 99, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &lastOn, 1);
    check(e.padMetadata(63).active, "all-bank mapping includes its upper boundary");
    const midichopper::MidiEvent lastOff{0, 99, 0, midichopper::MidiEventType::NoteOff};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &lastOff, 1);
    const midichopper::MidiEvent aboveRange{0, 100, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &aboveRange, 1);
    check(!e.padMetadata(63).active, "all-bank mapping rejects notes above its range");

    settings.padsPerBank = 8;
    e.setSettings(settings);
    check(e.importPad(8, pad), "import the first Bank B pad in the eight-pad layout");
    const midichopper::MidiEvent regroupedOn{0, 44, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &regroupedOn, 1);
    check(e.padMetadata(8).active &&
          midichopper::bankForPad(8, 8, midichopper::MidiBankMode::AllBanks) == 1,
          "eight-pad layout groups sample slot eight into Bank B");
    const midichopper::MidiEvent regroupedOff{0, 44, 0, midichopper::MidiEventType::NoteOff};
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &regroupedOff, 1);

    settings.padsPerBank = 12;
    e.setSettings(settings);
    e.process(nullptr, nullptr, outputLeft, outputRight, 1, &regroupedOn, 1);
    check(e.padMetadata(8).active &&
          midichopper::bankForPad(8, 12, midichopper::MidiBankMode::AllBanks) == 0,
          "twelve-pad layout regroups the same sample and preserves its MIDI note");
    check(midichopper::midiNoteForPad(8, midichopper::kDefaultBaseMidiNote,
              midichopper::MidiBankMode::AllBanks) == 44,
          "layout changes do not renumber a sample in all-bank mode");

    settings.baseNote = 112;
    settings.padsPerBank = 16;
    e.setSettings(settings);
    check(e.settings().baseNote == midichopper::maximumBaseMidiNote(
              midichopper::MidiBankMode::AllBanks),
          "all-bank base note clamps so all 64 pads remain addressable");

    midichopper::SamplerEngine capture(1000.0, 1.0);
    auto captureSettings = capture.settings();
    captureSettings.armed = true;
    captureSettings.monitorInput = false;
    captureSettings.midiBankMode = midichopper::MidiBankMode::AllBanks;
    captureSettings.padsPerBank = 8;
    capture.setSettings(captureSettings);
    // Arming chooses the first empty pad; a later start-pad change is an
    // explicit manual destination selection.
    captureSettings.startPad = 7;
    capture.setSettings(captureSettings);
    float input[2]{};
    const midichopper::MidiEvent boundaries[]{
        {0, 60, 127, midichopper::MidiEventType::NoteOn},
        {1, 60, 127, midichopper::MidiEventType::NoteOn},
    };
    capture.process(input, input, outputLeft, outputRight, 2, boundaries, 2);
    capture.finalizeRecording();
    capture.process(nullptr, nullptr, outputLeft, outputRight, 0);
    check(capture.padMetadata(7).occupied && capture.padMetadata(8).occupied &&
          !capture.padMetadata(16).occupied,
          "all-bank capture advances through sequential layout pages");
}

midichopper::PadData occupiedPad() {
    midichopper::PadData pad;
    pad.sampleRate = 1000.0;
    pad.frames = 1;
    pad.stereo = {1.0f, 1.0f};
    pad.peak = pad.rms = 1.0f;
    return pad;
}

void capture_target_selection() {
    const auto pad = occupiedPad();

    midichopper::SamplerEngine selected(1000.0, 1.0);
    check(selected.captureTargetPad() == midichopper::kPadCount,
          "Play mode has no capture target");
    check(selected.importPad(16, pad) && selected.importPad(17, pad),
          "populate leading Selected Bank pads");
    auto settings = selected.settings();
    settings.armed = true;
    settings.monitorInput = false;
    settings.activeBank = 1;
    settings.padsPerBank = 12;
    settings.midiBankMode = midichopper::MidiBankMode::SelectedBank;
    selected.setSettings(settings);
    check(selected.captureTargetPad() == 18U,
          "arming chooses the first empty visible Selected Bank pad");

    settings.activeBank = 2;
    selected.setSettings(settings);
    check(selected.captureTargetPad() == 32U,
          "idle Selected Bank changes choose the new bank's first empty pad");
    settings.startPad = 4;
    selected.setSettings(settings);
    check(selected.captureTargetPad() == 36U,
          "an idle start-pad-only change explicitly retargets capture");

    float input[1] = {1.0f};
    float outputLeft[1]{};
    float outputRight[1]{};
    const midichopper::MidiEvent trigger{0, 99, 127, midichopper::MidiEventType::NoteOn};
    selected.process(input, input, outputLeft, outputRight, 1, &trigger, 1);
    check(selected.captureTargetPad() == 36U && selected.padMetadata(36).recording,
          "MIDI note identity starts capture at the explicit target");

    settings.activeBank = 3;
    settings.startPad = 2;
    settings.padsPerBank = 8;
    settings.midiBankMode = midichopper::MidiBankMode::AllBanks;
    selected.setSettings(settings);
    check(selected.captureTargetPad() == 36U,
          "target-setting changes are ignored while a pad is recording");
    selected.process(input, input, outputLeft, outputRight, 1, &trigger, 1);
    check(selected.captureTargetPad() == 37U && selected.padMetadata(37).recording,
          "mid-capture routing changes preserve the established sequence");

    midichopper::SamplerEngine unique(1000.0, 1.0);
    check(unique.importPad(16, pad), "populate the leading All Banks pad");
    settings = unique.settings();
    settings.armed = true;
    settings.activeBank = 2;
    settings.padsPerBank = 8;
    settings.midiBankMode = midichopper::MidiBankMode::AllBanks;
    unique.setSettings(settings);
    check(unique.captureTargetPad() == 17U,
          "arming uses contiguous layout pages in All Banks mode");

    midichopper::SamplerEngine full(1000.0, 1.0);
    for (std::uint32_t localPad = 0; localPad < 8U; ++localPad)
        check(full.importPad(16U + localPad, pad), "populate a full visible bank");
    settings = full.settings();
    settings.armed = true;
    settings.activeBank = 2;
    settings.padsPerBank = 8;
    settings.midiBankMode = midichopper::MidiBankMode::AllBanks;
    full.setSettings(settings);
    check(full.captureTargetPad() == 16U,
          "a full visible bank falls back to its first pad");
    full.clearAllPads();
    check(full.captureTargetPad() == 16U,
          "clearing an armed bank resets its target to the first visible pad");
}

void completed_capture_retargeting() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    auto settings = e.settings();
    settings.armed = true;
    settings.monitorInput = false;
    settings.activeBank = 3;
    settings.midiBankMode = midichopper::MidiBankMode::SelectedBank;
    e.setSettings(settings);
    settings.startPad = 15;
    e.setSettings(settings);

    float input[1] = {1.0f};
    float outputLeft[1]{};
    float outputRight[1]{};
    const midichopper::MidiEvent trigger{0, 60, 127, midichopper::MidiEventType::NoteOn};
    e.process(input, input, outputLeft, outputRight, 1, &trigger, 1);
    e.process(input, input, outputLeft, outputRight, 1, &trigger, 1);
    check(e.captureTargetPad() == midichopper::kPadCount && e.padMetadata(63).occupied,
          "exhausting the final bank leaves no capture target");

    e.selectCaptureTarget(47U);
    check(e.captureTargetPad() == midichopper::kPadCount,
          "an explicit target outside the active bank is rejected");
    e.selectCaptureTarget(63U);
    check(e.captureTargetPad() == 63U,
          "reselecting the same local pad reopens a completed capture session");
    e.process(input, input, outputLeft, outputRight, 1, &trigger, 1);
    check(e.padMetadata(63).recording,
          "capture restarts when the exhausted target is selected again");
    e.finalizeRecording();
    e.process(nullptr, nullptr, outputLeft, outputRight, 0);

    settings.startPad = 14;
    e.setSettings(settings);
    check(e.captureTargetPad() == 62U,
          "an explicit idle target change reopens a completed capture session");
    e.process(input, input, outputLeft, outputRight, 1, &trigger, 1);
    check(e.padMetadata(62).recording,
          "capture restarts at the explicit target after session completion");

    using namespace midichopper::plugin;
    check(padFromCaptureTargetRequest(captureTargetRequestValue(63U, false)) == 63U &&
          padFromCaptureTargetRequest(captureTargetRequestValue(63U, true)) == 63U,
          "alternating capture-target commands preserve repeated global-pad requests");
}

void playback_and_rate_conversion() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    midichopper::PadData source; source.sampleRate = 500.0; source.frames = 2;
    source.stereo = {1, 1, -1, -1}; source.peak = 1.0f; source.rms = 1.0f;
    check(e.importPad(0, source), "import source-rate pad");
    float outL[5]{}; float outR[5]{};
    midichopper::MidiEvent on{0, 36, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, outL, outR, 5, &on, 1);
    close(outL[0], 1, "playback starts on note offset");
    close(outL[1], 0, "linear source-rate interpolation");
    close(outL[2], -1, "resampled second source frame");
    check(e.padMetadata(0).sampleRate == 500.0, "source rate retained");
}

void playback_trigger_notifications() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 100;
    source.stereo.assign(200U, 1.0f);
    source.peak = source.rms = 1.0f;
    check(e.importPad(0, source) && e.importPad(1, source),
          "import pads for playback notifications");

    float left[1]{};
    float right[1]{};
    const midichopper::MidiEvent pad0{0, 36, 127, midichopper::MidiEventType::NoteOn};
    const midichopper::MidiEvent pad1{0, 37, 127, midichopper::MidiEventType::NoteOn};

    e.process(nullptr, nullptr, left, right, 1, &pad0, 1);
    const auto first = e.lastPlaybackTrigger();
    e.process(nullptr, nullptr, left, right, 1, &pad1, 1);
    const auto second = e.lastPlaybackTrigger();
    e.process(nullptr, nullptr, left, right, 1, &pad0, 1);
    const auto retrigger = e.lastPlaybackTrigger();

    check(first.pad == 0U && second.pad == 1U && retrigger.pad == 0U &&
          first.generation + 1U == second.generation &&
          second.generation + 1U == retrigger.generation,
          "every successful MIDI note-on reports its pad, including an active-pad retrigger");

    using namespace midichopper::plugin;
    const float low = playbackPadEventValue(0U, false);
    const float high = playbackPadEventValue(0U, true);
    check(low != high && padFromPlaybackEvent(low) == 0U &&
          padFromPlaybackEvent(high) == 0U &&
          padFromPlaybackEvent(std::numeric_limits<float>::quiet_NaN()) ==
              midichopper::kPadCount,
          "alternating playback-pad values preserve identity and force a host-visible edge");
    PlaybackPadEventTracker tracker;
    check(tracker.consume(low) == 0U &&
          tracker.consume(low) == midichopper::kPadCount &&
          tracker.consume(high) == 0U,
          "playback-pad tracking ignores duplicates but accepts alternate-half retriggers");
    const auto targetRange = parameterRange(kParameterCaptureTargetPad);
    check(targetRange.minimum == 0.0f &&
          targetRange.maximum == static_cast<float>(midichopper::kPadCount),
          "capture-target output covers no-target and every global pad");
}

void shared_storage_blocks() {
    midichopper::SamplerEngine e(2000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 2000.0;
    source.frames = 1500;
    source.stereo.resize(3000U);
    for (std::uint32_t frame = 0; frame < source.frames; ++frame) {
        const float sample = static_cast<float>(frame) / 2000.0f;
        source.stereo[static_cast<std::size_t>(frame) * 2U] = sample;
        source.stereo[static_cast<std::size_t>(frame) * 2U + 1U] = -sample;
    }
    source.peak = 0.75f;
    source.rms = 0.4f;
    check(e.importPad(0, source), "import sample spanning storage blocks");
    midichopper::PadData restored;
    check(e.exportPad(0, restored), "export sample spanning storage blocks");
    close(restored.stereo[2046], source.stereo[2046], "sample before block boundary survives");
    close(restored.stereo[2048], source.stereo[2048], "sample after block boundary survives");
    close(restored.stereo.back(), source.stereo.back(), "sample tail survives pooled storage");

    e.clearPad(0);
    check(e.importPad(63, source), "cleared blocks are reusable by another bank");
    check(e.exportPad(63, restored), "export reused storage blocks");
    close(restored.stereo[2048], source.stereo[2048], "reused block retains imported audio");
}

void maximum_voice_limit() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 32;
    source.stereo.assign(64U, 1.0f);
    source.peak = source.rms = 1.0f;
    check(e.importPad(0, source) && e.importPad(1, source) && e.importPad(2, source),
          "import polyphony test pads");

    auto settings = e.settings();
    settings.monitorInput = false;
    settings.maxVoices = 2;
    e.setSettings(settings);

    const midichopper::MidiEvent events[] = {
        {0, 36, 127, midichopper::MidiEventType::NoteOn},
        {1, 37, 127, midichopper::MidiEventType::NoteOn},
        {2, 38, 127, midichopper::MidiEventType::NoteOn},
    };
    float left[4]{};
    float right[4]{};
    e.process(nullptr, nullptr, left, right, 4, events, 3);
    check(!e.padMetadata(0).active && e.padMetadata(1).active && e.padMetadata(2).active,
          "new voice steals the oldest active pad");
    close(left[0], 1.0f, "first voice starts alone");
    close(left[1], 2.0f, "two voices may overlap at the configured limit");
    close(left[2], 2.0f, "stolen voice is silent when the third starts");

    settings.maxVoices = 1;
    e.setSettings(settings);
    check(!e.padMetadata(1).active && e.padMetadata(2).active,
          "lowering the limit immediately keeps only the newest voice");

    settings.maxVoices = 0;
    e.setSettings(settings);
    check(e.settings().maxVoices == 1, "voice limit clamps to one");
    settings.maxVoices = 127;
    e.setSettings(settings);
    check(e.settings().maxVoices == midichopper::kPadsPerBank,
          "voice limit clamps to the per-bank pad count");
}

void sample_region_and_adsr() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 8;
    source.stereo = {1,1, 2,2, 3,3, 4,4, 5,5, 6,6, 7,7, 8,8};
    source.peak = 8.0f;
    check(e.importPad(0, source), "import sample-editor source");

    sms::dsp::SamplePlaybackSettings region;
    region.start = 0.25f;
    region.end = 0.75f;
    e.setPadPlaybackSettings(0, region);
    const auto stored = e.padPlaybackSettings(0);
    close(stored.start, 0.25f, "sample start setting round-trips");
    close(stored.end, 0.75f, "sample end setting round-trips");

    float output[8]{};
    midichopper::MidiEvent on{0, 36, 127, midichopper::MidiEventType::NoteOn};
    e.process(nullptr, nullptr, output, output, 8, &on, 1);
    close(output[0], 3.0f, "cut region starts at selected frame");
    close(output[3], 6.0f, "cut region includes selected final source frame");
    close(output[4], 0.0f, "cut region stops at exclusive end");

    midichopper::SamplerEngine envelopeEngine(1000.0, 1.0);
    source.frames = 6;
    source.stereo.assign(12U, 1.0f);
    source.peak = source.rms = 1.0f;
    check(envelopeEngine.importPad(0, source), "import ADSR source");
    sms::dsp::SamplePlaybackSettings envelope;
    envelope.attackSeconds = 0.002f;
    envelope.decaySeconds = 0.0f;
    envelope.sustainLevel = 1.0f;
    envelope.releaseSeconds = 0.002f;
    envelopeEngine.setPadPlaybackSettings(0, envelope);
    float shaped[7]{};
    envelopeEngine.process(nullptr, nullptr, shaped, shaped, 7, &on, 1);
    close(shaped[0], 0.0f, "ADSR attack begins at zero");
    close(shaped[1], 0.5f, "ADSR attack advances sample accurately");
    close(shaped[2], 1.0f, "ADSR reaches sustain");
    close(shaped[5], 0.5f, "one-shot release fades before the cut end");
    close(shaped[6], 0.0f, "ADSR voice becomes idle after release");
}

void pad_replacement_hardening() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    auto settings = e.settings();
    settings.armed = true;
    settings.monitorInput = false;
    e.setSettings(settings);
    float input[2] = {1.0f, 1.0f};
    float output[2]{};
    midichopper::MidiEvent on{0, 60, 127, midichopper::MidiEventType::NoteOn};
    e.process(input, input, output, output, 2, &on, 1);
    check(e.padMetadata(0).recording, "pad begins recording before replacement");
    e.clearPad(0);
    check(!e.padMetadata(0).recording && !e.padMetadata(0).occupied,
          "clearing active pad cancels capture cleanly");

    midichopper::PadData invalid;
    invalid.sampleRate = std::numeric_limits<double>::quiet_NaN();
    invalid.frames = 1;
    invalid.stereo = {0.0f, 0.0f};
    check(!e.importPad(0, invalid), "non-finite source rate is rejected");
    invalid.sampleRate = 1000.0;
    invalid.stereo[0] = std::numeric_limits<float>::infinity();
    check(!e.importPad(0, invalid), "non-finite source sample is rejected");
}

void fixed_duration() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    auto s = e.settings(); s.armed = true; s.captureMode = midichopper::CaptureMode::FixedDuration;
    s.fixedLengthSeconds = 0.003; s.monitorInput = false; e.setSettings(s);
    float in[8] = {1,2,3,4,5,6,7,8}; float out[8]{};
    midichopper::MidiEvent on{0, 40, 100, midichopper::MidiEventType::NoteOn};
    e.process(in, in, out, out, 8, &on, 1);
    e.finalizeRecording(); e.process(nullptr, nullptr, out, out, 0);
    midichopper::PadData p; check(e.exportPad(0, p) && p.frames == 3, "fixed duration capture");
    check(!e.padMetadata(1).occupied, "fixed capture waits for another note before next pad");
}

void disarm_note_off_gain_and_rate_change() {
    midichopper::SamplerEngine e(1000.0, 1.0);
    auto s = e.settings();
    s.armed = true; s.monitorInput = true; s.gain = 0.5f; e.setSettings(s);
    float input[] = {1, 2, 3, 4}; float left[4]{}; float right[4]{};
    const midichopper::MidiEvent events[] = {
        {0, 60, 127, midichopper::MidiEventType::NoteOn},
        {2, 60, 0, midichopper::MidiEventType::NoteOff},
    };
    e.process(input, input, left, right, 4, events, 2);
    close(left[1], 1.0f, "output gain applies to monitored input");
    check(e.padMetadata(0).recording, "note-off does not end sequential capture");

    s.armed = false; e.setSettings(s);
    e.process(nullptr, nullptr, left, right, 0);
    check(e.padMetadata(0).occupied && e.padMetadata(0).frames == 4,
          "disarming finalizes the open slice");

    e.setSampleRate(2000.0);
    check(e.padMetadata(0).occupied && e.padMetadata(0).frames == 4,
          "host sample-rate change preserves captured pads");
}
}

int main() {
    sequential_boundaries_and_preroll();
    full_bank_and_undo();
    bank_and_layout_mapping();
    all_bank_midi_mapping();
    capture_target_selection();
    completed_capture_retargeting();
    playback_and_rate_conversion();
    playback_trigger_notifications();
    shared_storage_blocks();
    maximum_voice_limit();
    sample_region_and_adsr();
    pad_replacement_hardening();
    fixed_duration();
    disarm_note_off_gain_and_rate_change();
    std::cout << "core tests passed\n";
}
