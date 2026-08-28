#include "../src/core/SamplerEngine.hpp"

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
    float in[20]{}; float out[20]{};
    std::vector<midichopper::MidiEvent> events;
    for (unsigned i = 0; i < 17; ++i) events.emplace_back(i, 60, 100, midichopper::MidiEventType::NoteOn);
    e.process(in, in, out, out, 17, events.data(), static_cast<std::uint32_t>(events.size()));
    e.finalizeRecording(); e.process(nullptr, nullptr, out, out, 0);
    check(e.padMetadata(0).occupied && e.padMetadata(15).occupied, "all 16 pads committed");
    check(!e.padMetadata(0).recording && !e.padMetadata(15).recording, "bank stops safely");
    e.undoLastSlice(); e.process(nullptr, nullptr, out, out, 0);
    check(!e.padMetadata(15).occupied && e.padMetadata(14).occupied, "undo removes only last slice");
    e.clearAllPads();
    for (unsigned i = 0; i < midichopper::kPadCount; ++i) check(!e.padMetadata(i).occupied, "clear all pads");
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
    playback_and_rate_conversion();
    sample_region_and_adsr();
    pad_replacement_hardening();
    fixed_duration();
    disarm_note_off_gain_and_rate_change();
    std::cout << "core tests passed\n";
}
