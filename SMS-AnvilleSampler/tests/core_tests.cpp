#include "../src/core/SamplerEngine.hpp"
#include "../src/core/PadClipboard.hpp"
#include "../src/plugin/Parameters.hpp"
#include "DSP/PeakMeter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
void close(float actual, float expected, const char* message)
{
    if (std::abs(actual - expected) >= 1.0e-5f)
        std::cerr << "FAIL: " << message << " (actual " << actual
                  << ", expected " << expected << ")\n";
    check(std::abs(actual - expected) < 1.0e-5f, message);
}

void checkPadSettingsCleared(const midichopper::SamplerEngine& engine,
                             const std::uint32_t pad,
                             const char* const message)
{
    const auto playback = engine.padPlaybackSettings(pad);
    const auto mixer = engine.padMixerSettings(pad);
    check(playback.start == 0.0f && playback.end == 1.0f &&
          playback.attackSeconds == 0.0f && playback.decaySeconds == 0.0f &&
          playback.sustainLevel == 1.0f && playback.releaseSeconds == 0.0f &&
          mixer.gainDecibels == 0.0f && mixer.pan == 0.0f &&
          mixer.tuneSemitones == 0.0f && mixer.lowpass == 0.0f &&
          mixer.highpass == 0.0f && mixer.filterSlope == 0.0f &&
          mixer.dirty == 0.0f && mixer.reverse == 0.0f, message);
}

void retained_storage_reserve_and_bounded_reclamation() {
    midichopper::SampleStoragePool pool(2U); // two logical blocks, four physical blocks
    auto* oldCapture = pool.acquireCapture();
    check(oldCapture && pool.ensureCaptureBlock(*oldCapture, 0U) &&
          pool.ensureCaptureBlock(*oldCapture, 1U), "allocate complete original capture");
    pool.writeCapture(*oldCapture, 0U, 0.25f, -0.5f);
    oldCapture->references.fetch_add(1U); // retained reader
    oldCapture->references.fetch_sub(1U); // release original pad

    auto* replacement = pool.acquireCapture();
    check(replacement && replacement != oldCapture &&
          pool.ensureCaptureBlock(*replacement, 0U) &&
          pool.ensureCaptureBlock(*replacement, 1U),
          "physical reserve allows replacement beside retained original");
    pool.writeCapture(*replacement, 0U, 0.75f, -0.125f);
    pool.reclaimCapture(4U);
    close(pool.sample(oldCapture, 0U, 0U), 0.25f, "retained original survives reserve writes");
    close(pool.sample(oldCapture, 0U, 1U), -0.5f, "retained original preserves stereo");

    auto* nextCapture = pool.acquireCapture();
    check(nextCapture && !pool.ensureCaptureBlock(*nextCapture, 0U),
          "physical exhaustion fails without reclaiming retained storage");
    oldCapture->references.fetch_sub(1U); // reader finishes
    pool.reclaimCapture(1U);
    check(oldCapture->allocatedBlocks == 1U &&
          pool.ensureCaptureBlock(*nextCapture, 0U) &&
          !pool.ensureCaptureBlock(*nextCapture, 1U),
          "one-block budget returns exactly one retired block");
    close(pool.sample(replacement, 0U, 0U), 0.75f,
          "bounded reclaim preserves active replacement");
    pool.reclaimCapture(1U);
    check(oldCapture->allocatedBlocks == 0U && pool.ensureCaptureBlock(*nextCapture, 1U),
          "later callback returns remaining retired capacity");
    replacement->references.fetch_sub(1U);
    nextCapture->references.fetch_sub(1U);
}

void newSessionDefaults()
{
    namespace ranges = midichopper::plugin::parameterRanges;
    check(ranges::maxVoices.defaultValue == 1.0f &&
          ranges::inputMonitor.defaultValue == 2.0f &&
          ranges::monitorGainDb.defaultValue == 0.0f &&
          ranges::filterSlope.defaultValue == 0.0f,
          "host defaults match the new-session controls");
    check(midichopper::EngineSettings{}.maxVoices == 1U &&
          midichopper::EngineSettings{}.monitorGain == 1.0f &&
          !midichopper::EngineSettings{}.reverse &&
          midichopper::EngineSettings{}.filterSlope == 0.0f &&
          sms::dsp::SampleMixerSettings{}.filterSlope == 0.0f,
          "engine and pad mixer defaults match the host controls");
    const auto globalReverseRange = midichopper::plugin::parameterRange(
        midichopper::plugin::kParameterGlobalReverse);
    check(globalReverseRange.defaultValue == 0.0f && globalReverseRange.minimum == 0.0f &&
          globalReverseRange.maximum == 1.0f,
          "global reverse host parameter defaults to Forward and uses a toggle range");
}

void stableParameterIds()
{
    using namespace midichopper::plugin;
    // Literal identities protect saved host automation from accidental insertions
    // anywhere in the enum. Append new parameters; do not regenerate this fixture.
    constexpr std::pair<Parameter, std::uint32_t> identities[]{
        {kParameterMode, 0U}, {kParameterStartPad, 1U}, {kParameterPreRollMs, 2U},
        {kParameterCaptureMode, 3U}, {kParameterFixedLengthSeconds, 4U},
        {kParameterPlaybackMode, 5U}, {kParameterInputMonitor, 6U},
        {kParameterBaseMidiNote, 7U}, {kParameterOutputGainDb, 8U},
        {kParameterFinalize, 9U}, {kParameterUndo, 10U}, {kParameterClearAll, 11U},
        {kParameterMaxVoices, 44U}, {kParameterActiveBank, 45U},
        {kParameterPadLayout, 46U}, {kParameterCurrentCapturePad, 47U},
        {kParameterMidiBankMode, 48U}, {kParameterPlaybackPadEvent, 49U},
        {kParameterCaptureTargetPad, 50U}, {kParameterCaptureTargetRequest, 51U},
        {kParameterInputLevelLeft, 52U}, {kParameterInputLevelRight, 53U},
        {kParameterOutputLevelLeft, 54U}, {kParameterOutputLevelRight, 55U},
        {kParameterPadFileResultEvent, 56U}, {kParameterPadClipboardAvailable, 57U},
        {kParameterPadClipboardResultEvent, 58U}, {kParameterChopPreviewPosition, 59U},
        {kParameterPlaybackPosition, 60U}, {kParameterGlobalPan, 61U},
        {kParameterGlobalTuneSemitones, 62U}, {kParameterGlobalLowpass, 63U},
        {kParameterGlobalHighpass, 64U}, {kParameterGlobalFilterSlope, 65U},
        {kParameterGlobalDirty, 66U}, {kParameterStopAllPlayback, 67U},
        {kParameterAnyPlaybackActive, 68U}, {kParameterMonitorGainDb, 69U},
        {kParameterGlobalReverse, 70U},
    };
    for (const auto& [parameter, expected] : identities)
        check(static_cast<std::uint32_t>(parameter) == expected,
              "existing parameter retains its literal host automation ID");
    check(kParameterPadCount == 16U, "saved pad-output parameter blocks retain 16 entries");
    for (std::uint32_t pad = 0U; pad < 16U; ++pad)
        check(kFirstPadStatusParameter + pad == 12U + pad &&
              kFirstPadActivityParameter + pad == 28U + pad,
              "each pad status and activity parameter retains its literal ID");
}

void live_peak_meter() {
    sms::dsp::StereoPeakMeter meter;
    const float left[] = {-0.25f, 0.5f, -0.1f};
    const float right[] = {0.1f, -0.75f, 0.2f};
    meter.process(left, right, 3, 1000.0);
    close(meter.left(), 0.5f, "peak meter measures left magnitude");
    close(meter.right(), 0.75f, "peak meter measures right magnitude");

    std::array<float, 100> quiet{};
    quiet.fill(0.1f);
    meter.process(quiet.data(), quiet.data(), 50, 1000.0);
    close(meter.left(), 0.5f, "peak meter holds transients");
    close(meter.right(), 0.75f, "stereo channels hold independently");
    meter.process(quiet.data(), quiet.data(), 100, 1000.0);
    check(meter.left() < 0.5f && meter.left() > 0.1f,
          "peak meter releases after its hold");
    check(meter.right() < 0.75f && meter.right() > meter.left(),
          "peak meter preserves channel separation during release");

    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(), 2.0f};
    meter.process(invalid, nullptr, 3, 1000.0);
    close(meter.left(), 1.0f, "peak meter clamps clipping and infinity");
    check(meter.right() < 1.0f, "null channel remains independent");
    meter.reset();
    close(meter.left(), 0.0f, "peak meter reset clears left");
    close(meter.right(), 0.0f, "peak meter reset clears right");
    meter.process(left, right, 3, 1000.0);
    meter.process(nullptr, nullptr, 5000, 1000.0);
    close(meter.left(), 0.0f, "peak meter stops below the visible floor");
    close(meter.right(), 0.0f, "silent peak meter avoids subnormal decay");

    using namespace midichopper::plugin;
    check(kParameterInputLevelLeft == kParameterCaptureTargetRequest + 1U &&
          kParameterPadClipboardAvailable == kParameterPadFileResultEvent + 1U &&
          kParameterChopPreviewPosition == kParameterPadClipboardResultEvent + 1U &&
          kParameterPlaybackPosition == kParameterChopPreviewPosition + 1U &&
          kParameterGlobalPan == kParameterPlaybackPosition + 1U &&
          kParameterGlobalTuneSemitones == kParameterGlobalPan + 1U &&
          kParameterGlobalLowpass == kParameterGlobalTuneSemitones + 1U &&
          kParameterStopAllPlayback == kParameterGlobalDirty + 1U &&
          kParameterAnyPlaybackActive == kParameterStopAllPlayback + 1U &&
          kParameterMonitorGainDb == kParameterAnyPlaybackActive + 1U &&
          kParameterGlobalReverse == kParameterMonitorGainDb + 1U &&
          kParameterGlobalReverse + 1U == kParameterCount,
          "new controls remain appended after released and hidden parameters");
}

void color_effects()
{
    using sms::dsp::ColorFilters;
    sms::dsp::SampleMixerSettings color;
    ColorFilters filter;
    color.lowpass = 0.5f;
    color.filterSlope = 0.0f;
    auto energy = [&](const float hz) {
        filter.reset();
        filter.configure(color, 48000.0);
        double sum = 0.0;
        for (int i = 0; i < 4096; ++i) {
            float left = std::sin(2.0 * 3.141592653589793 * hz * i / 48000.0);
            float right = left;
            filter.process(left, right);
            if (i >= 1024) sum += static_cast<double>(left) * left;
        }
        return sum;
    };
    const auto lowEnergy = energy(100.0f);
    const auto highEnergy6 = energy(5000.0f);
    check(lowEnergy > highEnergy6 * 5.0, "lowpass attenuates high frequencies");
    color.filterSlope = 2.0f;
    const auto highEnergy24 = energy(5000.0f);
    check(highEnergy24 < highEnergy6 * 0.1,
          "24 dB lowpass is steeper than 6 dB lowpass");
    color.lowpass = 0.0f;
    color.highpass = 0.5f;
    filter.reset();
    filter.configure(color, 48000.0);
    const auto highPassLowEnergy = energy(100.0f);
    const auto highPassHighEnergy = energy(5000.0f);
    check(highPassHighEnergy > highPassLowEnergy * 5.0,
          "highpass attenuates low frequencies");

    check(sms::dsp::dirtySourceFrame(1U, 48000.0) == 0U &&
          sms::dsp::dirtySourceFrame(2U, 48000.0) == 1U,
          "dirty clock holds source frames at 26.04 kHz");
    const float crushed = sms::dsp::dirtyMono(0.3f, -0.1f);
    close(crushed, std::round(0.1f * 2047.0f) / 2047.0f,
          "dirty mode folds to mono and quantizes to 12 bits");

    midichopper::SamplerEngine engine(48000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 48000.0;
    source.frames = 64U;
    source.stereo.assign(128U, 0.0f);
    for (std::uint32_t frame = 0; frame < source.frames; ++frame) {
        source.stereo[frame * 2U] = 0.3f + 0.01f * static_cast<float>(frame);
        source.stereo[frame * 2U + 1U] = -0.1f;
    }
    check(engine.importPad(0, source), "import dirty source");
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.dirty = 1.0f;
    engine.setSettings(settings);
    const midichopper::MidiEvent on{0U, 36U, 127U, midichopper::MidiEventType::NoteOn};
    float left[3]{}, right[3]{};
    engine.process(nullptr, nullptr, left, right, 3U, &on, 1U);
    close(left[0], right[0], "global dirty mode renders mono");
    close(left[0], left[1], "global dirty mode holds successive source samples");
    settings.dirty = 0.0f;
    engine.setSettings(settings);
    auto padColor = engine.padMixerSettings(0);
    padColor.dirty = 1.0f;
    engine.setPadMixerSettings(0, padColor);
    engine.process(nullptr, nullptr, left, right, 3U, &on, 1U);
    close(left[0], right[0], "pad dirty mode renders mono");

    midichopper::SamplerEngine filtered(48000.0, 1.0);
    midichopper::PadData wave;
    wave.sampleRate = 48000.0;
    wave.frames = 2048U;
    wave.stereo.resize(4096U);
    for (std::uint32_t frame = 0; frame < wave.frames; ++frame) {
        const float sample = std::sin(2.0 * 3.141592653589793 *
            5000.0 * static_cast<double>(frame) / 48000.0);
        wave.stereo[frame * 2U] = wave.stereo[frame * 2U + 1U] = sample;
    }
    check(filtered.importPad(0, wave), "import filter test tone");
    auto filterSettings = filtered.settings();
    filterSettings.monitorInput = false;
    filtered.setSettings(filterSettings);
    const auto renderEnergy = [&] {
        std::array<float, 1024> outputL{}, outputR{};
        filtered.process(nullptr, nullptr, outputL.data(), outputR.data(),
                         static_cast<std::uint32_t>(outputL.size()), &on, 1U);
        double energy = 0.0;
        for (std::size_t frame = 256; frame < outputL.size(); ++frame)
            energy += static_cast<double>(outputL[frame]) * outputL[frame];
        return energy;
    };
    const double dryEnergy = renderEnergy();
    auto padFilter = filtered.padMixerSettings(0);
    padFilter.lowpass = 0.5f;
    filtered.setPadMixerSettings(0, padFilter);
    check(renderEnergy() < dryEnergy * 0.1,
          "pad lowpass reaches playback voices");
    padFilter.lowpass = 0.0f;
    filtered.setPadMixerSettings(0, padFilter);
    filterSettings.lowpass = 0.5f;
    filtered.setSettings(filterSettings);
    check(renderEnergy() < dryEnergy * 0.1,
          "global lowpass reaches playback voices");
}

void visible_waveform_range() {
    midichopper::SamplerEngine engine(48000.0, 1.0);
    midichopper::PadData pad;
    pad.sampleRate = 48000.0;
    pad.frames = 256;
    pad.stereo.assign(512, 0.0f);
    pad.stereo[200U * 2U] = 0.8f;
    check(engine.importPad(0, pad), "import first waveform pad");
    pad.stereo.assign(512, 0.0f);
    pad.stereo[20U * 2U + 1U] = -0.6f;
    check(engine.importPad(1, pad), "import second waveform pad");
    sms::audio::WaveformSummary summary;
    check(engine.summarizePadRange(0, 2, 180, 290, summary) &&
          summary.frames == 110U && summary.pad == 0U &&
          *std::max_element(summary.maximum.begin(), summary.maximum.end()) > 0.79f &&
          *std::min_element(summary.minimum.begin(), summary.minimum.end()) < -0.59f,
          "visible waveform reads exact frames across a pad boundary");
    check(engine.summarizePadRange(0, 1, 190, 210, summary) &&
          *std::max_element(summary.maximum.begin(), summary.maximum.end()) > 0.79f &&
          *std::min_element(summary.minimum.begin(), summary.minimum.end()) >= 0.0f,
          "deep zoom excludes audio outside the visible range");
    check(!engine.summarizePadRange(0, 2, 180, 513, summary),
          "visible waveform rejects ranges past source audio");
}

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

void rechop_and_raw_preview() {
    midichopper::SamplerEngine engine(1000.0, 1.0);
    midichopper::PadData left;
    left.sampleRate = 1000.0;
    left.frames = 4U;
    left.stereo = {1, 1, 2, 2, 3, 3, 4, 4};
    left.peak = 4.0f;
    left.rms = 1.0f;
    midichopper::PadData right;
    right.sampleRate = 1000.0;
    right.frames = 4U;
    right.stereo = {5, 5, 6, 6, 7, 7, 8, 8};
    right.peak = 8.0f;
    right.rms = 1.0f;
    check(engine.importPad(0, left) && engine.importPad(1, right) &&
          engine.importPad(2, right), "import a contiguous pad run");
    sms::dsp::SamplePlaybackSettings shaped;
    shaped.start = 0.25f;
    shaped.end = 0.75f;
    shaped.attackSeconds = 0.1f;
    shaped.decaySeconds = 0.2f;
    shaped.sustainLevel = 0.4f;
    shaped.releaseSeconds = 0.3f;
    engine.setPadPlaybackSettings(0, shaped);
    engine.setPadPlaybackSettings(2, shaped);
    sms::dsp::SampleMixerSettings mix;
    mix.gainDecibels = -6.0f;
    mix.pan = -0.25f;
    mix.tuneSemitones = 7.0f;
    mix.reverse = 1.0f;
    engine.setPadMixerSettings(0, mix);

    const std::array<std::int64_t, 2> later{{2, 0}};
    check(engine.rechopPads(0, 3, later), "move a boundary later");
    midichopper::PadData movedLeft, movedRight;
    check(engine.exportPad(0, movedLeft) && engine.exportPad(1, movedRight),
          "export rechopped pads");
    check(movedLeft.frames == 6U && movedRight.frames == 2U,
          "rechop transfers frames between neighbors");
    close(movedLeft.stereo[10], 6.0f, "left pad receives the right pad prefix");
    close(movedRight.stereo[0], 7.0f, "right pad begins at the moved boundary");
    const auto reset = engine.padPlaybackSettings(0);
    check(reset.start == 0.0f && reset.end == 1.0f && reset.attackSeconds == 0.0f,
          "rechop resets pad cuts and ADSR");
    check(engine.padPlaybackSettings(2).start == shaped.start &&
          engine.padPlaybackSettings(2).attackSeconds == shaped.attackSeconds,
          "rechop preserves shaping on pads untouched by moved boundaries");
    const auto preservedMix = engine.padMixerSettings(0);
    close(preservedMix.gainDecibels, mix.gainDecibels,
          "rechop preserves mixer gain on an affected pad");
    close(preservedMix.pan, mix.pan, "rechop preserves mixer pan on an affected pad");
    close(preservedMix.tuneSemitones, mix.tuneSemitones,
          "rechop preserves mixer tune on an affected pad");

    auto settings = engine.settings();
    settings.reverse = true;
    engine.setSettings(settings);
    engine.startChopPreview(0, 2, 5U, 8U);
    check(engine.chopPreviewPosition() > 1.0f && engine.chopPreviewPosition() < 2.0f,
          "raw preview reports its first pad position");
    settings = engine.settings();
    settings.monitorInput = false;
    engine.setSettings(settings);
    float outputLeft[3]{};
    float outputRight[3]{};
    engine.process(nullptr, nullptr, outputLeft, outputRight, 3U);
    close(outputLeft[0], 6.0f, "raw preview starts at a requested source frame");
    close(outputLeft[1], 7.0f, "raw preview crosses the edited pad boundary");
    close(outputLeft[2], 8.0f, "raw preview keeps source order");
    check(engine.chopPreviewPosition() == 0.0f, "raw preview stops at session end");

    engine.startChopPreview(0, 2, 0U, 1U);
    settings.armed = true;
    engine.setSettings(settings);
    check(engine.chopPreviewPosition() == 0.0f,
          "arming stops and prevents raw preview");
    engine.startChopPreview(0, 2, 0U, 1U);
    check(engine.chopPreviewPosition() == 0.0f,
          "raw preview cannot start while armed");
    settings.armed = false;
    engine.setSettings(settings);

    check(engine.importPad(3, right), "import an out-of-editor pad");
    midichopper::ChopMidiPreview midiPreview;
    midiPreview.active = true;
    midiPreview.firstPad = 0U;
    midiPreview.sourcePadCount = 3U;
    midiPreview.previewPadCount = 3U;
    midiPreview.sourceFrames = {0U, 2U, 5U};
    midiPreview.sourceEndFrames = {2U, 5U, 12U};
    engine.setChopMidiPreview(midiPreview);
    check(engine.chopMidiPreviewActive(), "cut editor MIDI preview activates");
    const midichopper::MidiEvent middlePreview{
        1U, midichopper::midiNoteForPad(1U, settings.baseNote,
                                       settings.midiBankMode),
        127U, midichopper::MidiEventType::NoteOn};
    float midiPreviewLeft[5]{};
    float midiPreviewRight[5]{};
    engine.process(nullptr, nullptr, midiPreviewLeft, midiPreviewRight, 5U,
                   &middlePreview, 1U);
    close(midiPreviewLeft[0], 0.0f,
          "MIDI raw preview retains the event's sample offset");
    close(midiPreviewLeft[1], 3.0f,
          "MIDI plays the proposed slice start instead of stored pad start");
    close(midiPreviewLeft[3], 5.0f,
          "MIDI raw preview reaches the proposed slice end");
    close(midiPreviewLeft[4], 0.0f,
          "MIDI raw preview stops after the proposed slice end");

    const midichopper::MidiEvent outsidePreview{
        0U, midichopper::midiNoteForPad(3U, settings.baseNote,
                                       settings.midiBankMode),
        127U, midichopper::MidiEventType::NoteOn};
    std::fill_n(midiPreviewLeft, 5U, 0.0f);
    std::fill_n(midiPreviewRight, 5U, 0.0f);
    engine.process(nullptr, nullptr, midiPreviewLeft, midiPreviewRight, 4U,
                   &outsidePreview, 1U);
    close(midiPreviewLeft[0], 0.0f,
          "notes outside the cut editor do not play stored pads");

    midiPreview.sourcePadCount = 1U;
    midiPreview.previewPadCount = 0U;
    engine.setChopMidiPreview(midiPreview);
    std::fill_n(midiPreviewLeft, 5U, 0.0f);
    std::fill_n(midiPreviewRight, 5U, 0.0f);
    engine.process(nullptr, nullptr, midiPreviewLeft, midiPreviewRight, 1U,
                   &outsidePreview, 1U);
    close(midiPreviewLeft[0], 0.0f,
          "loading editor suppresses every MIDI note");

    midiPreview.previewPadCount = 2U;
    midiPreview.sourceFrames = {0U, 3U, 0U};
    midiPreview.sourceEndFrames = {3U, 6U, 0U};
    engine.setChopMidiPreview(midiPreview);
    const midichopper::MidiEvent splitSuffix{
        0U, midichopper::midiNoteForPad(1U, settings.baseNote,
                                       settings.midiBankMode),
        127U, midichopper::MidiEventType::NoteOn};
    std::fill_n(midiPreviewLeft, 5U, 0.0f);
    std::fill_n(midiPreviewRight, 5U, 0.0f);
    engine.process(nullptr, nullptr, midiPreviewLeft, midiPreviewRight, 3U,
                   &splitSuffix, 1U);
    close(midiPreviewLeft[0], 4.0f,
          "split editor second note previews the virtual suffix");
    close(midiPreviewLeft[2], 6.0f,
          "split editor suffix uses only the unsplit source pad");
    engine.setChopMidiPreview({});
    check(!engine.chopMidiPreviewActive(), "leaving the editor disables MIDI preview");

    const std::array<std::int64_t, 2> earlier{{-2, 0}};
    check(engine.rechopPads(0, 3, earlier), "move a boundary earlier");
    check(engine.exportPad(0, movedLeft) && engine.exportPad(1, movedRight) &&
          movedLeft.frames == 4U && movedRight.frames == 4U,
          "negative boundary move transfers frames back to the right pad");
    close(movedLeft.stereo[6], 4.0f, "negative move preserves left pad order");
    close(movedRight.stereo[0], 5.0f, "negative move restores the right pad prefix");

    engine.setPadPlaybackSettings(0, shaped);
    engine.setPadMixerSettings(0, mix);
    const std::array<std::int64_t, 2> emptyOccupied{{-4, 0}};
    check(engine.rechopPads(0, 3, emptyOccupied),
          "rechop can empty an occupied edge pad");
    check(!engine.padMetadata(0).occupied && engine.padMetadata(0).frames == 0U &&
          engine.padMetadata(0).peak == 0.0f && engine.padMetadata(0).rms == 0.0f,
          "zero-length edge result is published as an empty pad");
    checkPadSettingsCleared(engine, 0,
        "emptying an edge pad clears all playback and mixer settings");
    check(engine.exportPad(1, movedRight) && movedRight.frames == 8U,
          "audio from an emptied edge pad transfers to its neighbor");
    close(movedRight.stereo[0], 1.0f, "transferred audio keeps the combined source start");

    engine.setPadPlaybackSettings(1, shaped);
    engine.setPadMixerSettings(1, mix);
    const std::array<std::int64_t, 2> emptyMiddle{{0, -8}};
    check(engine.rechopPads(0, 3, emptyMiddle),
          "rechop can empty the middle pad");
    check(!engine.padMetadata(1).occupied && engine.padMetadata(1).frames == 0U,
          "zero-length middle result is published as an empty pad");
    checkPadSettingsCleared(engine, 1,
        "emptying the middle pad clears all playback and mixer settings");
    check(engine.exportPad(2, movedRight) && movedRight.frames == 12U &&
          engine.padMetadata(0).frames + engine.padMetadata(1).frames +
              engine.padMetadata(2).frames == 12U,
          "emptying the middle pad conserves all source frames");
    close(movedRight.stereo.front(), 1.0f,
          "middle-pad removal preserves the combined source start");
    close(movedRight.stereo.back(), 8.0f,
          "middle-pad removal preserves the combined source end");

    engine.setPadPlaybackSettings(2, shaped);
    engine.setPadMixerSettings(2, mix);
    const std::array<std::int64_t, 2> emptyRight{{0, 12}};
    check(engine.rechopPads(0, 3, emptyRight),
          "rechop can empty an occupied right edge pad");
    check(!engine.padMetadata(2).occupied && engine.padMetadata(2).frames == 0U,
          "zero-length right edge result is published as an empty pad");
    checkPadSettingsCleared(engine, 2,
        "emptying the right pad clears all playback and mixer settings");
    check(engine.exportPad(1, movedRight) && movedRight.frames == 12U &&
          engine.padMetadata(0).frames + engine.padMetadata(1).frames +
              engine.padMetadata(2).frames == 12U,
          "emptying the right pad conserves all source frames");
    close(movedRight.stereo.front(), 1.0f,
          "right-pad removal preserves the combined source start");
    close(movedRight.stereo.back(), 8.0f,
          "right-pad removal preserves the combined source end");

    const std::array<std::int64_t, 2> outsideSource{{-7, 0}};
    check(!engine.rechopPads(0, 3, outsideSource),
          "rechop rejects a cut outside the source");
}

void rechop_empty_neighbors()
{
    midichopper::PadData first;
    first.sampleRate = 1000.0;
    first.frames = 4U;
    first.stereo = {1, 1, 2, 2, 3, 3, 4, 4};
    first.peak = 4.0f;
    first.rms = 1.0f;
    midichopper::PadData second = first;
    second.stereo = {5, 5, 6, 6, 7, 7, 8, 8};
    second.peak = 8.0f;

    midichopper::SamplerEngine emptyLeft(1000.0, 1.0);
    check(emptyLeft.importPad(1, first) && emptyLeft.importPad(2, second),
          "import two pads after an empty slot");
    auto settings = emptyLeft.settings();
    settings.monitorInput = false;
    emptyLeft.setSettings(settings);
    emptyLeft.startChopPreview(0, 3, 0U, 2U);
    float previewLeft[2]{};
    float previewRight[2]{};
    emptyLeft.process(nullptr, nullptr, previewLeft, previewRight, 2U);
    close(previewLeft[0], 1.0f, "raw preview skips an empty leading pad");
    close(previewLeft[1], 2.0f, "raw preview preserves audio after an empty pad");

    const std::array<std::int64_t, 2> claimLeft{{2, 0}};
    check(emptyLeft.rechopPads(0, 3, claimLeft),
          "left empty pad claims audio by moving its cut right");
    midichopper::PadData left, middle, right;
    check(emptyLeft.exportPad(0, left) && emptyLeft.exportPad(1, middle) &&
          emptyLeft.exportPad(2, right), "export pads after filling the empty left slot");
    check(left.frames == 2U && middle.frames == 2U && right.frames == 4U,
          "left edge cut repartitions a two-sample run");
    close(left.stereo[0], 1.0f, "new left pad starts at the combined source start");
    close(middle.stereo[0], 3.0f, "selected pad starts at the moved left cut");

    midichopper::SamplerEngine emptyRight(1000.0, 1.0);
    check(emptyRight.importPad(0, first) && emptyRight.importPad(1, second),
          "import two pads before an empty slot");
    const std::array<std::int64_t, 2> claimRight{{0, -2}};
    check(emptyRight.rechopPads(0, 3, claimRight),
          "right empty pad claims audio by moving its cut left");
    check(emptyRight.exportPad(0, left) && emptyRight.exportPad(1, middle) &&
          emptyRight.exportPad(2, right), "export pads after filling the empty right slot");
    check(left.frames == 4U && middle.frames == 2U && right.frames == 2U,
          "right edge cut repartitions a two-sample run");
    close(middle.stereo[0], 5.0f, "selected pad retains its source prefix");
    close(right.stereo[0], 7.0f, "new right pad receives the source suffix");

    midichopper::SamplerEngine preserveEmpty(1000.0, 1.0);
    check(preserveEmpty.importPad(0, first) && preserveEmpty.importPad(1, second),
          "import a second two-pad run");
    const std::array<std::int64_t, 2> moveOccupiedCut{{2, 0}};
    check(preserveEmpty.rechopPads(0, 3, moveOccupiedCut),
          "moving another cut preserves an untouched empty edge slot");
    check(!preserveEmpty.padMetadata(2).occupied &&
          preserveEmpty.padMetadata(2).frames == 0U,
          "unchanged empty neighbor remains empty after Apply");
}

void long_sample_split_and_roll()
{
    midichopper::SamplerEngine engine(1000.0, 30.0);
    midichopper::PadData song;
    song.sampleRate = 1000.0;
    song.frames = 31000U;
    song.stereo.resize(static_cast<std::size_t>(song.frames) * 2U);
    for (std::uint32_t frame = 0; frame < song.frames; ++frame) {
        const float value = static_cast<float>(frame) / song.frames;
        song.stereo[static_cast<std::size_t>(frame) * 2U] = value;
        song.stereo[static_cast<std::size_t>(frame) * 2U + 1U] = -value;
    }
    song.peak = song.rms = 1.0f;
    check(engine.importPad(0, song), "one pad accepts audio longer than 30 seconds");
    sms::dsp::SampleMixerSettings reverseMixer;
    reverseMixer.reverse = 1.0f;
    engine.setPadMixerSettings(0U, reverseMixer);
    const std::array<std::uint64_t, 2> generations{{
        engine.padMetadata(0).generation, engine.padMetadata(1).generation}};
    check(engine.splitPadAndShiftRight(0, 1, 15500U, generations),
          "long source splits into adjacent pads");
    const std::array<std::int64_t, 2> roll{{0, -1000}};
    check(engine.rechopPads(0, 3, roll),
          "cut-point edit rolls the split source into the next pad");

    midichopper::PadData first, second, third;
    check(engine.exportPad(0, first) && engine.exportPad(1, second) &&
          engine.exportPad(2, third), "export all rolled pads");
    check(first.frames == 15500U && second.frames == 14500U && third.frames == 1000U,
          "rolling preserves the full long source");
    close(first.stereo.front(), 0.0f, "first pad keeps the source start");
    close(second.stereo.front(), 0.5f, "second pad begins at the split");
    close(third.stereo.front(), 30000.0f / 31000.0f,
          "third pad begins at the moved cut");
    close(third.stereo.back(), -30999.0f / 31000.0f,
          "last pad keeps the source end");

    song.frames = 1000000U;
    song.stereo.assign(static_cast<std::size_t>(song.frames) * 2U, 0.0f);
    check(!engine.importPad(0, song) && engine.padMetadata(0).frames == 15500U,
          "over-capacity import leaves the existing pad intact");
}

void pad_structure_edits()
{
    auto sample = [](const double rate, const std::initializer_list<float> values) {
        midichopper::PadData result;
        result.sampleRate = rate;
        result.frames = static_cast<std::uint32_t>(values.size());
        result.stereo.reserve(values.size() * 2U);
        for (const float value : values) {
            result.stereo.push_back(value);
            result.stereo.push_back(value);
            result.peak = std::max(result.peak, std::abs(value));
        }
        result.rms = 1.0f;
        return result;
    };
    sms::dsp::SamplePlaybackSettings shaped;
    shaped.start = 0.2f;
    shaped.end = 0.8f;
    shaped.attackSeconds = 0.1f;
    shaped.decaySeconds = 0.2f;
    shaped.sustainLevel = 0.6f;
    shaped.releaseSeconds = 0.3f;
    sms::dsp::SampleMixerSettings mixed;
    mixed.gainDecibels = -4.0f;
    mixed.pan = 0.35f;
    mixed.tuneSemitones = -3.0f;

    midichopper::SamplerEngine collapse(1000.0, 10.0);
    const auto first = sample(1000.0, {1.0f, 2.0f});
    const auto second = sample(2000.0, {3.0f, 4.0f, 5.0f});
    const auto third = sample(32000.0, {6.0f});
    const auto later = sample(44100.0, {9.0f, 10.0f});
    check(collapse.importPad(0, first) && collapse.importPad(3, second) &&
          collapse.importPad(4, third) && collapse.importPad(6, later),
          "populate separated runs for gap collapse");
    collapse.setPadPlaybackSettings(3, shaped);
    collapse.setPadMixerSettings(3, mixed);
    check(collapse.collapsePadGap(0U, 16U, 2U),
          "collapse the complete empty run containing the selected pad");
    midichopper::PadData moved;
    check(collapse.exportPad(1, moved) && moved.stereo == second.stereo &&
          moved.sampleRate == second.sampleRate,
          "collapse moves PCM and source rate into the gap");
    check(collapse.exportPad(2, moved) && moved.stereo == third.stereo,
          "collapse preserves the following occupied-run order");
    check(!collapse.padMetadata(3).occupied && !collapse.padMetadata(4).occupied,
          "collapse leaves vacated slots empty");
    check(collapse.exportPad(6, moved) && moved.stereo == later.stereo,
          "collapse stops at the next empty slot");
    const auto movedPlayback = collapse.padPlaybackSettings(1);
    const auto movedMixer = collapse.padMixerSettings(1);
    close(movedPlayback.start, shaped.start, "collapse moves playback settings");
    close(movedPlayback.attackSeconds, shaped.attackSeconds,
          "collapse moves envelope settings");
    close(movedMixer.gainDecibels, mixed.gainDecibels,
          "collapse moves mixer gain");
    close(movedMixer.pan, mixed.pan, "collapse moves mixer pan");
    close(movedMixer.tuneSemitones, mixed.tuneSemitones,
          "collapse moves mixer tune");
    check(!collapse.collapsePadGap(0U, 16U, 15U),
          "collapse rejects a gap with no following occupied run");

    midichopper::SamplerEngine split(1000.0, 10.0);
    const auto source = sample(1000.0, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f});
    check(split.importPad(0, source) && split.importPad(1, second) &&
          split.importPad(2, third), "populate a run for inserted split");
    split.setPadPlaybackSettings(0, shaped);
    split.setPadMixerSettings(0, mixed);
    sms::dsp::SamplePlaybackSettings shiftedPlayback = shaped;
    shiftedPlayback.start = 0.1f;
    split.setPadPlaybackSettings(1, shiftedPlayback);
    sms::dsp::SampleMixerSettings shiftedMixer = mixed;
    shiftedMixer.pan = -0.5f;
    split.setPadMixerSettings(1, shiftedMixer);
    std::array<std::uint64_t, 4> generations{};
    for (std::uint32_t pad = 0; pad < generations.size(); ++pad)
        generations[pad] = split.padMetadata(pad).generation;
    check(split.splitPadAndShiftRight(0U, 3U, 2U, generations),
          "split a sample and shift the occupied suffix right");
    midichopper::PadData prefix, suffix, shifted;
    check(split.exportPad(0, prefix) && split.exportPad(1, suffix) &&
          prefix.stereo == sample(1000.0, {1.0f, 2.0f}).stereo &&
          suffix.stereo == sample(1000.0, {3.0f, 4.0f, 5.0f}).stereo,
          "split preserves both source halves exactly");
    check(split.exportPad(2, shifted) && shifted.stereo == second.stereo &&
          split.exportPad(3, moved) && moved.stereo == third.stereo,
          "split shifts later pads without rechopping them");
    check(split.padPlaybackSettings(0).start == 0.0f &&
          split.padPlaybackSettings(0).end == 1.0f &&
          split.padPlaybackSettings(0).attackSeconds == 0.0f &&
          split.padPlaybackSettings(1).start == 0.0f &&
          split.padPlaybackSettings(1).end == 1.0f &&
          split.padPlaybackSettings(1).attackSeconds == 0.0f,
          "split halves reset playback and envelope settings");
    const auto prefixMixer = split.padMixerSettings(0);
    const auto suffixMixer = split.padMixerSettings(1);
    close(prefixMixer.pan, mixed.pan, "split prefix keeps source mixer");
    close(suffixMixer.pan, mixed.pan, "split suffix duplicates source mixer");
    close(split.padPlaybackSettings(2).start, shiftedPlayback.start,
          "shifted pad keeps playback settings");
    close(split.padMixerSettings(2).pan, shiftedMixer.pan,
          "shifted pad keeps mixer settings");

    midichopper::SamplerEngine stale(1000.0, 10.0);
    check(stale.importPad(0, source) && stale.importPad(1, second),
          "populate a stale split plan");
    std::array<std::uint64_t, 3> staleGenerations{};
    for (std::uint32_t pad = 0; pad < staleGenerations.size(); ++pad)
        staleGenerations[pad] = stale.padMetadata(pad).generation;
    stale.setPadMixerSettings(1, shiftedMixer);
    check(!stale.splitPadAndShiftRight(0U, 2U, 2U, staleGenerations),
          "split rejects a plan invalidated by a settings edit");
    check(stale.padMetadata(0).frames == source.frames &&
          stale.padMetadata(1).frames == second.frames &&
          !stale.padMetadata(2).occupied,
          "failed stale split leaves the pad layout unchanged");
    auto armed = stale.settings();
    armed.armed = true;
    stale.setSettings(armed);
    for (std::uint32_t pad = 0; pad < staleGenerations.size(); ++pad)
        staleGenerations[pad] = stale.padMetadata(pad).generation;
    check(!stale.splitPadAndShiftRight(0U, 2U, 2U, staleGenerations) &&
          !stale.collapsePadGap(0U, 16U, 2U),
          "pad structure edits are unavailable while armed");

    midichopper::SamplerEngine banked(1000.0, 10.0);
    auto bankSettings = banked.settings();
    bankSettings.activeBank = 1U;
    bankSettings.midiBankMode = midichopper::MidiBankMode::SelectedBank;
    bankSettings.padsPerBank = 12U;
    banked.setSettings(bankSettings);
    check(banked.importPad(26U, source), "populate the end of a 12-pad visible bank");
    std::array<std::uint64_t, 2> bankGenerations{
        banked.padMetadata(26U).generation, banked.padMetadata(27U).generation};
    check(banked.splitPadAndShiftRight(26U, 27U, 2U, bankGenerations),
          "split stays inside a selected-bank 12-pad page");
    check(!banked.splitPadAndShiftRight(27U, 28U, 1U, bankGenerations),
          "split rejects a hidden slot beyond the visible bank");

    midichopper::SamplerEngine paged(1000.0, 10.0);
    auto pageSettings = paged.settings();
    pageSettings.activeBank = 2U;
    pageSettings.midiBankMode = midichopper::MidiBankMode::AllBanks;
    pageSettings.padsPerBank = 8U;
    paged.setSettings(pageSettings);
    check(paged.importPad(22U, source), "populate the end of an eight-pad page");
    std::array<std::uint64_t, 2> pageGenerations{
        paged.padMetadata(22U).generation, paged.padMetadata(23U).generation};
    check(paged.splitPadAndShiftRight(22U, 23U, 2U, pageGenerations) &&
          !paged.splitPadAndShiftRight(23U, 24U, 1U, pageGenerations),
          "all-bank split stays inside an eight-pad page");
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

void stop_all_playback()
{
    midichopper::SamplerEngine engine(1000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 100U;
    source.stereo.assign(200U, 1.0f);
    check(engine.importPad(0, source) && engine.importPad(1, source),
          "import stop-button test pads");
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.maxVoices = 2U;
    engine.setSettings(settings);
    const midichopper::MidiEvent events[] = {
        {0, 36, 127, midichopper::MidiEventType::NoteOn},
        {0, 37, 127, midichopper::MidiEventType::NoteOn},
    };
    float left[4]{};
    float right[4]{};
    engine.process(nullptr, nullptr, left, right, 4, events, 2);
    check(engine.anyPlaybackActive() && engine.padMetadata(0).active &&
          engine.padMetadata(1).active, "two pads are active before Stop");
    engine.stopAllPlayback();
    engine.process(nullptr, nullptr, left, right, 4);
    check(!engine.anyPlaybackActive() && !engine.padMetadata(0).active &&
          !engine.padMetadata(1).active && left[0] == 0.0f && right[0] == 0.0f,
          "Stop cuts every pad without a release tail");
    engine.process(nullptr, nullptr, left, right, 4, events, 1);
    check(engine.anyPlaybackActive() && engine.padMetadata(0).active,
          "playback can restart after Stop");
    engine.togglePlayback(1U);
    check(!engine.anyPlaybackActive() && !engine.padMetadata(0).active,
          "Play/Stop cuts an active voice even when another pad is selected");
    engine.togglePlayback(midichopper::kPadCount);
    check(!engine.anyPlaybackActive(),
          "Play/Stop with no selection remains idle");
    engine.togglePlayback(2U);
    check(!engine.anyPlaybackActive(),
          "Play/Stop with an empty selected pad remains idle");
    engine.togglePlayback(1U);
    check(engine.anyPlaybackActive() && engine.padMetadata(1).active,
          "Play/Stop auditions the selected occupied pad");
    engine.togglePlayback(0U);
    check(!engine.anyPlaybackActive() && !engine.padMetadata(1).active,
          "a second Play/Stop click cuts the audition");
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

midichopper::PadData loop_source(const double rate = 1000.0)
{
    midichopper::PadData source;
    source.sampleRate = rate;
    source.frames = 8U;
    source.stereo = {1,-10, 2,-20, 3,-30, 4,-40, 5,-50, 6,-60, 7,-70, 8,-80};
    return source;
}

void gated_region_looping()
{
    using namespace midichopper;
    for (const auto mode : {PlaybackMode::Gated, PlaybackMode::OneShot})
      for (const bool reverse : {false, true}) {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = mode;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import trimmed stereo loop");
        sms::dsp::SampleMixerSettings mixer;
        mixer.reverse = reverse ? 1.0f : 0.0f;
        engine.setPadMixerSettings(0U, mixer);
        sms::dsp::SamplePlaybackSettings region;
        region.start = 0.25f;
        region.end = 0.75f;
        engine.setPadPlaybackSettings(0U, region);
        const MidiEvent events[] = {
            {0U, 36U, 127U, MidiEventType::NoteOn},
            {14U, 36U, 0U, MidiEventType::NoteOff},
        };
        std::array<float, 18> left{}, right{};
        engine.process(nullptr, nullptr, left.data(), right.data(), left.size(), events, 2U);
        for (std::size_t frame = 0U; frame < left.size(); ++frame) {
            const bool audible = frame < (mode == PlaybackMode::Gated ? 14U : 4U);
            const float phase = static_cast<float>(frame % 4U);
            const float expected = audible ? (reverse ? 6.0f - phase : 3.0f + phase) : 0.0f;
            close(left[frame], expected, "Gate repeats exclusive region until exact note-off; OneShot stays finite");
            close(right[frame], -10.0f * expected, "trimmed loop preserves independent stereo channels");
        }
        check(!engine.anyPlaybackActive(), "zero-release note-off leaves no held loop");
    }

    // Literal sequences exercise fractional seam interpolation and overshoot
    // larger than the region, including simultaneous source-rate and tune changes.
    for (const bool fast : {false, true})
      for (const bool reverse : {false, true}) {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source(fast ? 1625.0 : 1500.0)),
              "import resampled loop");
        sms::dsp::SamplePlaybackSettings region;
        region.start = 0.25f;
        region.end = 0.75f;
        engine.setPadPlaybackSettings(0U, region);
        sms::dsp::SampleMixerSettings mixer;
        mixer.tuneSemitones = fast ? 24.0f : -12.0f;
        mixer.reverse = reverse ? 1.0f : 0.0f;
        engine.setPadMixerSettings(0U, mixer);
        const std::array<float, 8> expected = fast
            ? std::array<float, 8>{3, 5.5f, 4, 4.5f, 5, 3.5f, 6, 4.5f}
            : std::array<float, 8>{3, 3.75f, 4.5f, 5.25f, 6, 3.75f, 3.5f, 4.25f};
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        std::array<float, 8> left{}, right{};
        engine.process(nullptr, nullptr, left.data(), right.data(), left.size(), &on, 1U);
        for (std::size_t frame = 0U; frame < left.size(); ++frame) {
            const float sample = reverse ? 9.0f - expected[frame] : expected[frame];
            close(left[frame], sample, "both directions retain fractional and multi-region overshoot");
            close(right[frame], -10.0f * sample, "both directions interpolate across the trimmed seam");
        }
        check(engine.anyPlaybackActive(), "fast tune never exhausts a held loop");
    }
}

void gated_loop_release_and_edits()
{
    using namespace midichopper;
    for (const bool retrigger : {false, true})
      for (const bool reverse : {false, true}) {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import release loop");
        sms::dsp::SampleMixerSettings mixer;
        mixer.reverse = reverse ? 1.0f : 0.0f;
        engine.setPadMixerSettings(0U, mixer);
        sms::dsp::SamplePlaybackSettings region;
        region.start = 0.25f;
        region.end = 0.75f;
        region.releaseSeconds = 0.008f; // longer than two turns; no one-shot cap
        engine.setPadPlaybackSettings(0U, region);
        const MidiEvent events[] = {
            {0U, 36U, 127U, MidiEventType::NoteOn},
            {7U, 36U, 0U, MidiEventType::NoteOff},
            {11U, 36U, 127U, MidiEventType::NoteOn},
        };
        std::array<float, 18> left{}, right{};
        engine.process(nullptr, nullptr, left.data(), right.data(), left.size(), events,
                       retrigger ? 3U : 2U);
        for (std::size_t frame = 0U; frame < left.size(); ++frame) {
            const bool restarted = retrigger && frame >= 11U;
            const auto phase = restarted ? frame - 11U : frame;
            const float gain = restarted || frame < 7U ? 1.0f
                : frame < 15U ? static_cast<float>(15U - frame) / 8.0f : 0.0f;
            const float position = static_cast<float>(phase % 4U);
            const float expected = (reverse ? 6.0f - position : 3.0f + position) * gain;
            close(left[frame], expected, "Gate release crosses seams and retrigger resets cursor/envelope");
            close(right[frame], -10.0f * expected, "Gate release maintains stereo across multiple turns");
        }
        check(engine.anyPlaybackActive() == retrigger, "release finishes only after its full ADSR duration");
    }

    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Gated;
    engine.setSettings(settings);
    check(engine.importPad(0U, loop_source()), "import live End loop");
    sms::dsp::SamplePlaybackSettings region;
    region.start = 0.25f;
    region.end = 0.75f;
    engine.setPadPlaybackSettings(0U, region);
    const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
    float left[6]{}, right[6]{};
    engine.process(nullptr, nullptr, left, right, 3U, &on, 1U); // cursor is source frame 5
    region.end = 0.5f; // new exclusive End 4, region length 2
    engine.setPadPlaybackSettings(0U, region);
    engine.process(nullptr, nullptr, left, right, 6U);
    for (std::size_t frame = 0U; frame < 6U; ++frame) {
        const float expected = frame % 2U == 0U ? 4.0f : 3.0f;
        close(left[frame], expected, "moving End behind a loop cursor wraps its overshoot");
        close(right[frame], -10.0f * expected, "edited End keeps loop stereo intact");
    }
    check(engine.anyPlaybackActive(), "shrinking End preserves a held loop");

    engine.stopAllPlayback();
    region.end = 0.75f;
    engine.setPadPlaybackSettings(0U, region);
    sms::dsp::SampleMixerSettings reverse;
    reverse.reverse = 1.0f;
    reverse.tuneSemitones = -12.0f;
    engine.setPadMixerSettings(0U, reverse);
    engine.process(nullptr, nullptr, left, right, 1U, &on, 1U); // display cursor advances to 2.5
    region.end = 0.5f;
    engine.setPadPlaybackSettings(0U, region);
    engine.process(nullptr, nullptr, left, right, 6U);
    constexpr float reversed[]{5.5f, 5, 5.5f, 6, 5.5f, 5};
    for (std::size_t frame = 0U; frame < 6U; ++frame) {
        close(left[frame], reversed[frame], "shrinking End wraps reverse fractional overshoot");
        close(right[frame], -10.0f * reversed[frame], "reverse End edits preserve stereo");
    }
    check(engine.anyPlaybackActive(), "reverse loop survives shrinking End");

}

void live_loop_region_edits()
{
    using namespace midichopper;
    for (const bool audition : {false, true})
      for (const bool padReverse : {false, true})
        for (const bool globalReverse : {false, true}) {
            SamplerEngine engine(1000.0, 1.0);
            auto settings = engine.settings();
            settings.monitorInput = false;
            // Editor Loop must work even when the main mode is One Shot.
            settings.playbackMode = audition ? PlaybackMode::OneShot : PlaybackMode::Gated;
            settings.reverse = globalReverse;
            engine.setSettings(settings);
            check(engine.importPad(0U, loop_source()), "import live loop-boundary source");
            sms::dsp::SampleMixerSettings mixer;
            mixer.reverse = padReverse ? 1.0f : 0.0f;
            mixer.tuneSemitones = -12.0f;
            engine.setPadMixerSettings(0U, mixer);
            sms::dsp::SamplePlaybackSettings displayed;
            displayed.attackSeconds = 0.008f;
            displayed.releaseSeconds = 0.008f;
            const auto edit = [&](const float start, const float end) {
                displayed.start = start;
                displayed.end = end;
                // Use the same staged settings path as Sample Editor edits.
                engine.restorePadPlaybackSettings(0U, globalReverse
                    ? sms::dsp::mirroredRegion(displayed) : displayed);
            };
            edit(0.125f, 0.875f); // display frames [1, 7)
            float left[12]{}, right[12]{};
            const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
            if (audition) {
                engine.process(nullptr, nullptr, left, right, 0U); // publish staged cuts
                engine.auditionPad(0U, true);
            }
            engine.process(nullptr, nullptr, left, right, 3U,
                           audition ? nullptr : &on, audition ? 0U : 1U);
            // Cursor is now 2.5. Start alone changes the next seam and turn,
            // while the ongoing attack and in-range cursor must continue.
            unsigned elapsed = 3U;
            const auto expect = [&](const auto& positions) {
                engine.process(nullptr, nullptr, left, right, positions.size());
                for (std::size_t frame = 0U; frame < positions.size(); ++frame) {
                    const float sample = padReverse != globalReverse
                        ? 8.0f - positions[frame] : 1.0f + positions[frame];
                    const float gain = std::min(static_cast<float>(elapsed + frame) / 8.0f, 1.0f);
                    close(left[frame], sample * gain,
                          "loop edits refresh both cuts without restarting cursor or ADSR");
                    close(right[frame], -10.0f * sample * gain,
                          "edited loop retains stereo and fractional seam interpolation");
                }
                elapsed += positions.size();
                check(engine.anyPlaybackActive(), "boundary edit keeps loop running");
            };
            edit(0.25f, 0.875f); // [2, 7)
            expect(std::array<float, 12>{2.5f, 3, 3.5f, 4, 4.5f, 5, 5.5f, 6, 4, 2, 2.5f, 3});
            edit(0.5f, 0.75f); // [4, 6): wrap cursor 3.5 below the new Start to 5.5
            expect(std::array<float, 8>{4.5f, 4, 4.5f, 5, 4.5f, 4, 4.5f, 5});
            edit(0.25f, 1.0f); // [2, 8): expanding both cuts keeps cursor 5.5
            expect(std::array<float, 8>{5.5f, 6, 6.5f, 7, 4.5f, 2, 2.5f, 3});
            edit(0.0f, 0.25f); // [0, 2): End can move before the former Start
            expect(std::array<float, 6>{0.5f, 0, 0.5f, 1, 0.5f, 0});
            edit(0.875f, 1.0f); // a one-frame loop stays safe after a disjoint edit
            expect(std::array<float, 6>{7, 7, 7, 7, 7, 7});
            if (audition) {
                engine.stopPadAudition();
            } else {
                const MidiEvent off{0U, 36U, 0U, MidiEventType::NoteOff};
                engine.process(nullptr, nullptr, left, right, 8U, &off, 1U);
                for (unsigned frame = 0U; frame < 8U; ++frame) {
                    const float sample = padReverse != globalReverse ? 1.0f : 8.0f;
                    close(left[frame], sample * static_cast<float>(8U - frame) / 8.0f,
                          "Gate release continues looping through edited cuts");
                }
            }
            check(!engine.anyPlaybackActive(), "edited loop still stops normally");
        }
}

void playback_direction_latched_at_trigger()
{
    using namespace midichopper;
    for (const auto mode : {PlaybackMode::OneShot, PlaybackMode::Gated})
      for (const bool initiallyReverse : {false, true}) {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = mode;
        settings.maxVoices = 2U;
        engine.setSettings(settings);
        const auto source = loop_source();
        check(engine.importPad(0U, source) && engine.importPad(1U, source),
              "import independent direction pads");
        sms::dsp::SampleMixerSettings mixer;
        mixer.reverse = initiallyReverse ? 1.0f : 0.0f;
        engine.setPadMixerSettings(0U, mixer);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[16]{}, right[16]{};
        engine.process(nullptr, nullptr, left, right, 2U, &on, 1U);
        close(left[0], initiallyReverse ? 8.0f : 1.0f, "trigger selects correct boundary");
        mixer.reverse = initiallyReverse ? 0.0f : 1.0f;
        engine.setPadMixerSettings(0U, mixer);
        close(engine.padMixerSettings(1U).reverse, 0.0f, "direction belongs to one pad");
        const MidiEvent off{0U, 36U, 0U, MidiEventType::NoteOff};
        engine.process(nullptr, nullptr, left, right, 10U,
                       mode == PlaybackMode::OneShot ? &off : nullptr,
                       mode == PlaybackMode::OneShot ? 1U : 0U);
        for (std::size_t frame = 0; frame < 10U; ++frame) {
            const float phase = static_cast<float>((frame + 2U) % 8U);
            const float expected = mode == PlaybackMode::OneShot && frame >= 6U
                ? 0.0f : initiallyReverse ? 8.0f - phase : 1.0f + phase;
            close(left[frame], expected, "direction edit leaves active voice and note-off semantics intact");
            close(right[frame], -10.0f * expected, "latched direction preserves stereo");
        }
        // Two overlapping pads use independent directions. A same-pad retrigger
        // restarts the voice and takes the newest setting.
        const MidiEvent both[]{{0U, 36U, 127U, MidiEventType::NoteOn},
                               {0U, 37U, 127U, MidiEventType::NoteOn}};
        engine.process(nullptr, nullptr, left, right, 2U, both, 2U);
        for (std::size_t frame = 0; frame < 2U; ++frame) {
            const float phase = static_cast<float>(frame);
            const float expected = (initiallyReverse ? 1.0f + phase : 8.0f - phase) + 1.0f + phase;
            close(left[frame], expected, "retrigger uses latest direction alongside another forward voice");
            close(right[frame], -10.0f * expected, "overlapping directions preserve stereo");
        }
        PadData exported;
        check(engine.exportPad(0U, exported) && exported.stereo == source.stereo,
              "reverse playback and direction edits leave source PCM intact");
    }
}

void reverse_boundaries_and_envelopes()
{
    using namespace midichopper;
    // Fractional final interval clamps to Start for One Shot, and a one-frame
    // Gate region remains safe even when each step skips multiple turns.
    for (const auto mode : {PlaybackMode::OneShot, PlaybackMode::Gated})
      for (const bool singleFrame : {false, true}) {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = mode;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import reverse boundary source");
        sms::dsp::SamplePlaybackSettings region;
        region.start = singleFrame ? 0.0f : 0.25f;
        region.end = singleFrame ? 0.125f : 0.75f;
        engine.setPadPlaybackSettings(0U, region);
        sms::dsp::SampleMixerSettings mixer;
        mixer.reverse = 1.0f;
        mixer.tuneSemitones = singleFrame ? 24.0f : -12.0f;
        engine.setPadMixerSettings(0U, mixer);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[12]{}, right[12]{};
        engine.process(nullptr, nullptr, left, right, 12U, &on, 1U);
        for (std::size_t frame = 0; frame < 12U; ++frame) {
            float expected;
            if (singleFrame)
                expected = (mode == PlaybackMode::Gated || frame == 0U)
                    ? 8.0f : 0.0f;
            else if (mode == PlaybackMode::Gated) {
                constexpr float loop[]{6, 5.5f, 5, 4.5f, 4, 3.5f, 3, 4.5f};
                expected = loop[frame % 8U];
            } else
                expected = frame >= 8U ? 0.0f : std::max(3.0f, 6.0f - 0.5f * frame);
            close(left[frame], expected, "reverse fractional and one-frame boundaries are safe");
            close(right[frame], -10.0f * expected, "reverse boundary samples preserve stereo");
        }
    }
    // The envelope follows playback time after each direction maps the same
    // normalized region to its source frames.
    for (const bool reverse : {false, true}) {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import direction ADSR source");
        sms::dsp::SamplePlaybackSettings region;
        region.attackSeconds = 0.002f;
        region.decaySeconds = 0.002f;
        region.sustainLevel = 0.5f;
        region.releaseSeconds = 0.002f;
        engine.setPadPlaybackSettings(0U, region);
        sms::dsp::SampleMixerSettings mixer;
        mixer.reverse = reverse ? 1.0f : 0.0f;
        engine.setPadMixerSettings(0U, mixer);
        const MidiEvent events[]{{0U, 36U, 127U, MidiEventType::NoteOn},
                                 {1U, 36U, 0U, MidiEventType::NoteOff}};
        float left[10]{}, right[10]{};
        engine.process(nullptr, nullptr, left, right, 10U, events, 2U);
        constexpr float expected[]{0, 0.5f, 1, 0.75f, 0.5f, 0.5f, 0.5f, 0.25f, 0, 0};
        for (std::size_t frame = 0; frame < 10U; ++frame) {
            const float source = frame < 8U
                ? (reverse ? 8.0f - static_cast<float>(frame)
                           : 1.0f + static_cast<float>(frame))
                : 0.0f;
            close(left[frame], source * expected[frame],
                  "ADSR is applied in forward playback time after full-source reverse mapping");
            close(right[frame], -10.0f * source * expected[frame],
                  "reversed ADSR mapping preserves the stereo frame pair");
        }
    }
}

void asymmetric_trim_reverse_coordinates()
{
    using namespace midichopper;
    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    engine.setSettings(settings);
    check(engine.importPad(0U, loop_source()), "import asymmetric reverse region");
    sms::dsp::SamplePlaybackSettings region;
    region.start = 0.125f; // virtual frames [1, 5)
    region.end = 0.625f;
    engine.setPadPlaybackSettings(0U, region);
    sms::dsp::SampleMixerSettings mixer;
    mixer.reverse = 1.0f;
    engine.setPadMixerSettings(0U, mixer);

    const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
    float left[4]{}, right[4]{};
    engine.process(nullptr, nullptr, left, right, 2U, &on, 1U);
    close(left[0], 7.0f, "Start marker stays fixed while reverse maps the full source");
    close(left[1], 6.0f, "asymmetric trimmed reverse selects frames beyond the trimmed source tail");
    close(right[0], -70.0f, "reverse keeps the left/right source frame paired");
    close(right[1], -60.0f, "reverse trim retains stereo frame order");
    const float activePosition = engine.playbackPosition();
    close(activePosition, 1.375f, "reverse playhead advances in unchanged Start/End coordinates");

    mixer.reverse = 0.0f;
    engine.setPadMixerSettings(0U, mixer);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 5.0f, "active reverse voice keeps its direction after a mixer edit");
    check(engine.playbackPosition() > activePosition,
          "reverse display playhead advances toward the unchanged End marker");
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 4.0f, "latched reverse voice reaches the original trim boundary");
    check(!engine.anyPlaybackActive(), "asymmetric one-shot ends at its original trim boundary");

    engine.process(nullptr, nullptr, left, right, 4U, &on, 1U);
    constexpr float forward[]{2.0f, 3.0f, 4.0f, 5.0f};
    for (std::size_t frame = 0U; frame < 4U; ++frame)
        close(left[frame], forward[frame], "retrigger uses the newly selected forward direction");
}

void live_editor_flip_preserves_voice()
{
    using namespace midichopper;
    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Gated;
    settings.maxVoices = 2U;
    engine.setSettings(settings);
    const auto source = loop_source();
    check(engine.importPad(0U, source) && engine.importPad(1U, source),
          "import live editor flip voices");
    sms::dsp::SamplePlaybackSettings playback;
    playback.start = 0.125f;
    playback.end = 0.625f;
    playback.attackSeconds = 0.002f;
    playback.decaySeconds = 0.002f;
    playback.sustainLevel = 0.5f;
    playback.releaseSeconds = 0.002f;
    engine.setPadPlaybackSettings(0U, playback);
    sms::dsp::SampleMixerSettings forward;
    engine.setPadMixerSettings(0U, forward);
    const MidiEvent starts[]{{0U, 36U, 127U, MidiEventType::NoteOn},
                             {0U, 37U, 127U, MidiEventType::NoteOn}};
    float left[4]{}, right[4]{};
    engine.process(nullptr, nullptr, left, right, 2U, starts, 2U);
    close(left[0], 1.0f, "first editor-flip frame preserves attack phase");
    close(left[1], 3.5f, "second pad continues while selected voice attacks");

    // Mirroring [1, 5) to [3, 7) and reversing selects the same raw frames.
    // The action must map the live cursor in place and preserve its envelope.
    auto mirrored = playback;
    mirrored.start = 0.375f;
    mirrored.end = 0.875f;
    sms::dsp::SampleMixerSettings reverse;
    reverse.reverse = 1.0f;
    engine.restorePadEditorSettings(0U, mirrored, reverse);
    engine.process(nullptr, nullptr, left, right, 2U);
    close(left[0], 7.0f, "live flip continues at the same raw frame and envelope phase");
    close(left[1], 6.25f, "live reverse progresses through the mirrored raw region");
    check(engine.anyPlaybackActive(), "live flip preserves the held voice and its polyphony slot");

    // Coalesced editor requests before a callback leave the last orientation
    // active without retriggering or changing the other pad's voice.
    auto back = playback;
    sms::dsp::SampleMixerSettings forwardAgain;
    engine.restorePadEditorSettings(0U, mirrored, reverse);
    engine.restorePadEditorSettings(0U, back, forwardAgain);
    const MidiEvent offs[]{{0U, 36U, 0U, MidiEventType::NoteOff},
                           {0U, 37U, 0U, MidiEventType::NoteOff}};
    engine.process(nullptr, nullptr, left, right, 2U, offs, 2U);
    close(left[0], 1.0f, "coalesced double flip resumes forward with the existing release level");
    close(left[1], 0.75f, "double flip preserves cursor progression and release phase");
    close(right[0], -10.0f, "live flip keeps stereo frame pairing during release");
    engine.process(nullptr, nullptr, left, right, 2U);
    check(!engine.anyPlaybackActive(), "live-flipped voices finish their original release tails");
}

void live_editor_flip_fractional_and_single_frame()
{
    using namespace midichopper;
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import fractional live flip source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        engine.setPadPlaybackSettings(0U, playback);
        sms::dsp::SampleMixerSettings mixer;
        mixer.tuneSemitones = -12.0f;
        engine.setPadMixerSettings(0U, mixer);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[2]{}, right[2]{};
        engine.process(nullptr, nullptr, left, right, 1U, &on, 1U);
        close(left[0], 2.0f, "fractional live flip begins at the trim Start");
        playback.start = 0.375f;
        playback.end = 0.875f;
        mixer.reverse = 1.0f;
        engine.restorePadEditorSettings(0U, playback, mixer);
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 2.5f, "live flip maps fractional cursor without a phase jump");
        close(left[1], 2.0f, "fractional reverse keeps the same raw trim interval");
        close(right[0], -25.0f, "fractional live flip preserves stereo interpolation");
    }
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import one-frame live flip source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.end = 0.125f;
        engine.setPadPlaybackSettings(0U, playback);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[3]{}, right[3]{};
        engine.process(nullptr, nullptr, left, right, 1U, &on, 1U);
        close(left[0], 1.0f, "one-frame voice starts on its only raw frame");
        playback.start = 0.875f;
        playback.end = 1.0f;
        sms::dsp::SampleMixerSettings reverse;
        reverse.reverse = 1.0f;
        engine.restorePadEditorSettings(0U, playback, reverse);
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 1.0f, "one-frame live flip wraps into the mirrored range");
        close(left[1], 1.0f, "one-frame reverse loop remains bounded");
        check(engine.anyPlaybackActive(), "single-frame Gate remains held after orientation update");
    }
}

void live_editor_flip_during_one_shot_and_release()
{
    using namespace midichopper;
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::OneShot;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import live OneShot flip source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        playback.releaseSeconds = 0.002f;
        engine.setPadPlaybackSettings(0U, playback);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[4]{}, right[4]{};
        engine.process(nullptr, nullptr, left, right, 3U, &on, 1U);
        close(left[2], 4.0f, "OneShot reaches its automatic end-release phase");
        playback.start = 0.375f;
        playback.end = 0.875f;
        sms::dsp::SampleMixerSettings reverse;
        reverse.reverse = 1.0f;
        engine.restorePadEditorSettings(0U, playback, reverse);
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 2.5f, "OneShot flip continues its existing automatic release");
        close(left[1], 0.0f, "OneShot flip does not restart the completed release");
        check(!engine.anyPlaybackActive(), "OneShot flip retains the original release completion");
    }
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import Gate release live flip source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        playback.releaseSeconds = 0.004f;
        engine.setPadPlaybackSettings(0U, playback);
        const MidiEvent events[]{{0U, 36U, 127U, MidiEventType::NoteOn},
                                 {2U, 36U, 0U, MidiEventType::NoteOff}};
        float left[4]{}, right[4]{};
        engine.process(nullptr, nullptr, left, right, 3U, events, 2U);
        close(left[2], 4.0f, "Gate note-off begins release before live flip");
        playback.start = 0.375f;
        playback.end = 0.875f;
        sms::dsp::SampleMixerSettings reverse;
        reverse.reverse = 1.0f;
        engine.restorePadEditorSettings(0U, playback, reverse);
        engine.process(nullptr, nullptr, left, right, 4U);
        constexpr float release[]{3.75f, 2.0f, 0.75f, 0.0f};
        for (std::size_t frame = 0U; frame < 4U; ++frame)
            close(left[frame], release[frame], "live flip preserves manual release level and duration");
        check(!engine.anyPlaybackActive(), "flipped Gate finishes its existing release tail");
    }
}

void live_editor_flip_fractional_seam_and_coalesced_noop()
{
    using namespace midichopper;
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import fractional seam flip source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        engine.setPadPlaybackSettings(0U, playback);
        sms::dsp::SampleMixerSettings mixer;
        mixer.tuneSemitones = -12.0f;
        engine.setPadMixerSettings(0U, mixer);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[8]{}, right[8]{};
        engine.process(nullptr, nullptr, left, right, 7U, &on, 1U);
        playback.start = 0.375f;
        playback.end = 0.875f;
        mixer.reverse = 1.0f;
        engine.restorePadEditorSettings(0U, playback, mixer);
        engine.process(nullptr, nullptr, left, right, 2U);
        check(std::isfinite(left[0]) && std::isfinite(left[1]) &&
              std::isfinite(right[0]) && std::isfinite(right[1]),
              "fractional seam orientation wrap remains finite");
        close(left[0], 3.5f, "fractional seam cursor wraps safely into mirrored range");
        close(left[1], 5.0f, "fractional seam continues inside reversed region");
        check(engine.anyPlaybackActive(), "fractional seam flip keeps Gate active");
    }
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import coalesced no-op flip source");
        sms::dsp::SamplePlaybackSettings original;
        original.start = 0.125f;
        original.end = 0.625f;
        engine.setPadPlaybackSettings(0U, original);
        sms::dsp::SampleMixerSettings forward;
        engine.setPadMixerSettings(0U, forward);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[3]{}, right[3]{};
        engine.process(nullptr, nullptr, left, right, 1U, &on, 1U);
        auto mirrored = original;
        mirrored.start = 0.375f;
        mirrored.end = 0.875f;
        sms::dsp::SampleMixerSettings reverse;
        reverse.reverse = 1.0f;
        engine.restorePadEditorSettings(0U, mirrored, reverse);
        engine.restorePadEditorSettings(0U, original, forward);
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 3.0f, "coalesced flip pair leaves an initially forward voice unchanged");
        close(left[1], 4.0f, "coalesced forward cuts retain their original progression");
        sms::dsp::SamplePlaybackSettings actualPlayback;
        sms::dsp::SampleMixerSettings actualMixer;
        engine.snapshotPadSettings(0U, actualPlayback, actualMixer);
        check(actualPlayback.start == original.start && actualPlayback.end == original.end &&
              actualMixer.reverse == 0.0f,
              "coalesced no-op flip publishes the original cuts and orientation");
    }
}

void editor_flip_target_survives_later_mixer_restore()
{
    using namespace midichopper;
    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Gated;
    engine.setSettings(settings);
    check(engine.importPad(0U, loop_source()), "import editor-target merge source");
    const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
    float left[2]{}, right[2]{};
    engine.process(nullptr, nullptr, left, right, 2U, &on, 1U);
    close(left[0], 1.0f, "editor-target merge starts forward");
    close(left[1], 2.0f, "editor-target merge advances before queued edits");

    sms::dsp::SamplePlaybackSettings playback;
    sms::dsp::SampleMixerSettings reverse;
    reverse.reverse = 1.0f;
    engine.restorePadEditorSettings(0U, playback, reverse);
    sms::dsp::SampleMixerSettings forward;
    engine.restorePadMixerSettings(0U, forward);
    engine.process(nullptr, nullptr, left, right, 2U);
    close(left[0], 3.0f, "editor flip target applies despite later durable forward mixer state");
    close(left[1], 2.0f, "active voice follows the explicit editor flip target");
    sms::dsp::SamplePlaybackSettings actualPlayback;
    sms::dsp::SampleMixerSettings actualMixer;
    engine.snapshotPadSettings(0U, actualPlayback, actualMixer);
    check(actualMixer.reverse == 0.0f,
          "ordinary mixer restore remains the desired next-trigger direction");

    engine.process(nullptr, nullptr, left, right, 2U, &on, 1U);
    close(left[0], 1.0f, "retrigger follows durable forward mixer setting");
    close(left[1], 2.0f, "retrigger starts at the forward boundary");
}

void global_reverse_xor_matrix()
{
    using namespace midichopper;
    constexpr float envelopes[]{0.0f, 0.5f, 1.0f, 0.75f, 0.5f, 0.5f, 0.5f, 0.5f};
    constexpr float expected[2][2][8]{
        {{2, 2.5f, 3, 3.5f, 4, 4.5f, 5, 5},
         {7, 6.5f, 6, 5.5f, 5, 4.5f, 4, 4}},
        {{5, 4.5f, 4, 3.5f, 3, 2.5f, 2, 2},
         {4, 4.5f, 5, 5.5f, 6, 6.5f, 7, 7}}
    };
    for (const bool globalReverse : {false, true})
        for (const bool localReverse : {false, true}) {
            SamplerEngine engine(1000.0, 1.0);
            auto settings = engine.settings();
            settings.monitorInput = false;
            settings.playbackMode = PlaybackMode::OneShot;
            settings.reverse = globalReverse;
            engine.setSettings(settings);
            check(engine.importPad(0U, loop_source()), "import global XOR ramp source");
            sms::dsp::SamplePlaybackSettings playback;
            playback.start = 0.125f;
            playback.end = 0.625f;
            playback.attackSeconds = 0.002f;
            playback.decaySeconds = 0.002f;
            playback.sustainLevel = 0.5f;
            engine.setPadPlaybackSettings(0U, playback);
            sms::dsp::SampleMixerSettings mixer;
            mixer.reverse = localReverse ? 1.0f : 0.0f;
            mixer.tuneSemitones = -12.0f;
            engine.setPadMixerSettings(0U, mixer);
            const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
            float left[8]{}, right[8]{};
            engine.process(nullptr, nullptr, left, right, 8U, &on, 1U);
            for (std::size_t frame = 0U; frame < 8U; ++frame) {
                const float sample = expected[globalReverse ? 1U : 0U]
                                              [localReverse ? 1U : 0U][frame];
                close(left[frame], sample * envelopes[frame],
                      "global and pad reverse XOR with mirrored cuts and forward ADSR");
                close(right[frame], -10.0f * sample * envelopes[frame],
                      "global reverse XOR retains interpolated stereo frame pairs");
            }
            sms::dsp::SamplePlaybackSettings actualPlayback;
            sms::dsp::SampleMixerSettings actualMixer;
            engine.snapshotPadSettings(0U, actualPlayback, actualMixer);
            check(actualPlayback.start == playback.start && actualPlayback.end == playback.end &&
                  actualMixer.reverse == mixer.reverse,
                  "global reverse leaves saved per-pad cuts and orientation local");
        }
}

void global_reverse_live_voices_and_editor()
{
    using namespace midichopper;
    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Gated;
    settings.maxVoices = 2U;
    engine.setSettings(settings);
    check(engine.importPad(0U, loop_source()) && engine.importPad(1U, loop_source()),
          "import independent global reverse voices");
    sms::dsp::SampleMixerSettings forward, localReverse;
    localReverse.reverse = 1.0f;
    engine.setPadMixerSettings(0U, forward);
    engine.setPadMixerSettings(1U, localReverse);
    const MidiEvent first{0U, 36U, 127U, MidiEventType::NoteOn};
    constexpr float secondVelocityGain = 64.0f / 127.0f;
    float left[2]{}, right[2]{};
    engine.process(nullptr, nullptr, left, right, 2U, &first, 1U);
    close(left[0], 1.0f, "first global voice begins at its forward boundary");
    close(left[1], 2.0f, "first global voice advances independently");
    const MidiEvent second{0U, 37U, 64U, MidiEventType::NoteOn};
    engine.process(nullptr, nullptr, left, right, 1U, &second, 1U);
    close(left[0], 3.0f + 8.0f * secondVelocityGain,
          "local reverse voice starts at opposite source boundary with its own velocity");
    settings = engine.settings();
    settings.reverse = true;
    engine.setSettings(settings);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 4.0f + 7.0f * secondVelocityGain,
          "global flip turns both live voices from their own cursors");
    settings = engine.settings();
    engine.setSettings(settings); // Same-value update must not flip either voice again.
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 3.0f + 8.0f * secondVelocityGain,
          "reapplying the same global value is idempotent");

    // With the master on, flipping pad 0 changes its local reverse bit and
    // mirrors its stored region; the active target is the effective XOR.
    sms::dsp::SamplePlaybackSettings mirrored;
    localReverse.reverse = 1.0f;
    engine.restorePadEditorSettings(0U, mirrored, localReverse);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 2.0f + secondVelocityGain,
          "pad editor flip under global reverse changes effective direction at current point");
    settings = engine.settings();
    settings.reverse = false;
    engine.setSettings(settings);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 3.0f + 2.0f * secondVelocityGain,
          "global reverse toggles all active effective directions back");
    check(engine.padMixerSettings(0U).reverse == 1.0f &&
          engine.padMixerSettings(1U).reverse == 1.0f &&
          engine.padPlaybackSettings(0U).start == mirrored.start &&
          engine.padPlaybackSettings(0U).end == mirrored.end,
          "master and live flips preserve independent local pad settings");

    engine.stopAllPlayback();
    engine.process(nullptr, nullptr, left, right, 2U, &first, 1U);
    close(left[0], 8.0f, "future trigger combines local direction with global Forward");
    close(left[1], 7.0f, "future trigger uses the stored full range");
}

void global_reverse_audition_release_and_raw_preview()
{
    using namespace midichopper;
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import global reverse audition source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        engine.setPadPlaybackSettings(0U, playback);
        engine.togglePlayback(0U, false);
        float left[2]{}, right[2]{};
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 2.0f, "sample editor audition begins in the stored forward region");
        close(left[1], 3.0f, "audition advances before the master turn");
        settings = engine.settings();
        settings.reverse = true;
        engine.setSettings(settings);
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 4.0f, "global turn immediately redirects the active audition");
        close(left[1], 3.0f, "audition continues from its current sample point");
        check(engine.padPlaybackSettings(0U).start == playback.start &&
              engine.padPlaybackSettings(0U).end == playback.end,
              "global audition projection does not mutate stored editor cuts");
    }
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import global reverse release source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        playback.releaseSeconds = 0.004f;
        engine.setPadPlaybackSettings(0U, playback);
        const MidiEvent events[]{{0U, 36U, 127U, MidiEventType::NoteOn},
                                 {2U, 36U, 0U, MidiEventType::NoteOff}};
        float left[4]{}, right[4]{};
        engine.process(nullptr, nullptr, left, right, 3U, events, 2U);
        close(left[2], 4.0f, "manual Gate release has begun before master turn");
        settings = engine.settings();
        settings.reverse = true;
        engine.setSettings(settings);
        engine.process(nullptr, nullptr, left, right, 4U);
        constexpr float release[]{3.75f, 2.0f, 0.75f, 0.0f};
        for (std::size_t frame = 0U; frame < 4U; ++frame)
            close(left[frame], release[frame], "global turn preserves Gate release progression");
        check(!engine.anyPlaybackActive(), "globally turned Gate finishes its existing release");
    }
    {
        SamplerEngine engine(1000.0, 1.0);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = PlaybackMode::Gated;
        engine.setSettings(settings);
        check(engine.importPad(0U, loop_source()), "import global seam turn source");
        sms::dsp::SamplePlaybackSettings playback;
        playback.start = 0.125f;
        playback.end = 0.625f;
        engine.setPadPlaybackSettings(0U, playback);
        sms::dsp::SampleMixerSettings mixer;
        mixer.tuneSemitones = -12.0f;
        engine.setPadMixerSettings(0U, mixer);
        const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
        float left[8]{}, right[8]{};
        engine.process(nullptr, nullptr, left, right, 7U, &on, 1U);
        settings = engine.settings();
        settings.reverse = true;
        engine.setSettings(settings);
        engine.process(nullptr, nullptr, left, right, 2U);
        close(left[0], 3.5f, "global turn wraps fractional Gate seam into mirrored region");
        close(left[1], 5.0f, "global seam turn continues at mirrored Start");
        check(engine.anyPlaybackActive(), "global seam turn keeps held loop active");
    }
}

void independent_pad_audition()
{
    using namespace midichopper;
    for (const auto mode : {PlaybackMode::Gated, PlaybackMode::OneShot})
        for (const bool loop : {false, true})
          for (const bool reverse : {false, true}) {
            SamplerEngine engine(1000.0, 1.0);
            auto settings = engine.settings();
            settings.monitorInput = false;
            settings.playbackMode = mode;
            engine.setSettings(settings);
            check(engine.importPad(0U, loop_source()), "import editor audition");
            sms::dsp::SamplePlaybackSettings region;
            region.start = 0.25f;
            region.end = 0.75f;
            region.attackSeconds = 0.002f;
            region.sustainLevel = 0.5f;
            region.releaseSeconds = loop ? 0.008f : 0.0f;
            engine.setPadPlaybackSettings(0U, region);
            sms::dsp::SampleMixerSettings mixer;
            mixer.gainDecibels = -6.0205999f;
            mixer.pan = 0.5f;
            mixer.reverse = reverse ? 1.0f : 0.0f;
            engine.setPadMixerSettings(0U, mixer);
            engine.togglePlayback(0U, loop);
            const MidiEvent off{1U, 36U, 0U, MidiEventType::NoteOff};
            std::array<float, 10> left{}, right{};
            engine.process(nullptr, nullptr, left.data(), right.data(), left.size(), &off, 1U);
            for (std::size_t frame = 0U; frame < left.size(); ++frame) {
                const float envelope = frame == 0U ? 0.0f : 0.5f;
                const float phase = static_cast<float>(frame % 4U);
                const float sample = loop || frame < 4U
                    ? (reverse ? 6.0f - phase : 3.0f + phase) : 0.0f;
                close(left[frame], sample * envelope * 0.25f, "audition Play/Loop honours region, ADSR and pan/gain");
                close(right[frame], -10.0f * sample * envelope * 0.5f,
                      "audition is independent of Gate mode and MIDI note-off");
            }
            check(engine.anyPlaybackActive() == loop, "only Loop audition survives its region End");
            if (loop) {
                settings.playbackMode = mode == PlaybackMode::Gated ? PlaybackMode::OneShot : PlaybackMode::Gated;
                engine.setSettings(settings);
                engine.process(nullptr, nullptr, left.data(), right.data(), left.size());
                check(engine.anyPlaybackActive(), "changing global playback mode keeps an editor loop running");
            }
            engine.stopPadAudition();
            engine.process(nullptr, nullptr, left.data(), right.data(), left.size());
            check(!engine.anyPlaybackActive() && left[0] == 0.0f && right[0] == 0.0f,
                  "audition Stop hard-cuts its voice");
        }
}

void pad_audition_lifecycle()
{
    using namespace midichopper;
    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Gated;
    settings.maxVoices = 2U;
    engine.setSettings(settings);
    for (std::uint32_t pad = 0U; pad < 3U; ++pad)
        check(engine.importPad(pad, loop_source()), "import audition lifecycle pads");
    float left[2]{}, right[2]{};
    const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
    engine.process(nullptr, nullptr, left, right, 1U, &on, 1U);
    engine.auditionPad(1U, true);
    engine.auditionPad(2U, true);
    check(engine.padMetadata(0U).active && !engine.padMetadata(1U).active &&
          engine.padMetadata(2U).active, "new audition replaces previous audition and preserves unrelated MIDI");
    engine.auditionPad(3U, true);
    check(engine.padMetadata(0U).active && !engine.padMetadata(2U).active,
          "auditioning an empty pad cuts the previous audition");
    engine.auditionPad(1U, true);
    engine.stopPadAudition();
    check(engine.padMetadata(0U).active && !engine.padMetadata(1U).active,
          "audition Stop preserves an unrelated MIDI loop");
    engine.auditionPad(1U, true);
    const MidiEvent replace{0U, 37U, 127U, MidiEventType::NoteOn};
    engine.process(nullptr, nullptr, left, right, 1U, &replace, 1U);
    engine.stopPadAudition();
    check(engine.padMetadata(1U).active, "MIDI retrigger revokes audition ownership of its pad");
    engine.stopAllPlayback();
    engine.auditionPad(1U, true);
    settings.maxVoices = 1U;
    engine.setSettings(settings);
    const MidiEvent steal{0U, 38U, 127U, MidiEventType::NoteOn};
    engine.process(nullptr, nullptr, left, right, 1U, &steal, 1U);
    engine.stopPadAudition();
    check(!engine.padMetadata(1U).active && engine.padMetadata(2U).active,
          "voice stealing revokes audition ownership without stopping the MIDI replacement");
    engine.stopAllPlayback();
    check(!engine.anyPlaybackActive(), "Stop All cuts loops and clears audition ownership");
    engine.auditionPad(1U, true);
    settings.armed = true;
    engine.setSettings(settings);
    check(!engine.anyPlaybackActive(), "Arm cuts active audition loops");
    engine.auditionPad(1U, true);
    check(!engine.anyPlaybackActive(), "editor audition cannot start while armed");
}

void arm_stops_gate_without_interrupting_capture()
{
    using namespace midichopper;
    SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Gated;
    engine.setSettings(settings);
    check(engine.importPad(0U, loop_source()), "import Gate source before Arm");
    sms::dsp::SamplePlaybackSettings playback;
    playback.releaseSeconds = 0.008f;
    engine.setPadPlaybackSettings(0U, playback);
    const MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
    std::array<float, 16> left{}, right{};
    engine.process(nullptr, nullptr, left.data(), right.data(), 1U, &on, 1U);
    check(engine.anyPlaybackActive(), "held Gate voice starts before Arm");
    settings.armed = true;
    engine.setSettings(settings);
    check(!engine.anyPlaybackActive() && !engine.padMetadata(0U).active,
          "entering Arm immediately hard-cuts Gate voices without a release tail");
    const MidiEvent captureEvents[] = {
        {0U, 37U, 127U, MidiEventType::NoteOn},
        {2U, 36U, 0U, MidiEventType::NoteOff}, // release the former performance note
    };
    const float inputLeft[]{0.125f, 0.25f, 0.375f, 0.5f};
    const float inputRight[]{-0.25f, -0.375f, -0.5f, -0.625f};
    engine.process(inputLeft, inputRight, left.data(), right.data(), 4U, captureEvents, 2U);
    check(engine.padMetadata(1U).recording && !engine.anyPlaybackActive(),
          "performance note-off during Arm leaves sequential capture running");
    settings.armed = false;
    engine.setSettings(settings);
    engine.process(nullptr, nullptr, left.data(), right.data(), left.size());
    for (std::size_t frame = 0U; frame < left.size(); ++frame)
        check(left[frame] == 0.0f && right[frame] == 0.0f,
              "Gate voice released during Arm cannot resume after disarming");
    PadData captured;
    check(engine.exportPad(1U, captured) && captured.frames == 4U,
          "disarm finalizes every captured frame despite the performance note-off");
    for (std::size_t frame = 0U; frame < 4U; ++frame) {
        close(captured.stereo[frame * 2U], inputLeft[frame], "Arm transition preserves captured left PCM");
        close(captured.stereo[frame * 2U + 1U], inputRight[frame], "Arm transition preserves captured right PCM");
    }

    SamplerEngine oneShot(1000.0, 1.0);
    settings.armed = false;
    settings.playbackMode = PlaybackMode::OneShot;
    oneShot.setSettings(settings);
    check(oneShot.importPad(0U, loop_source()), "import OneShot source before Arm");
    oneShot.process(nullptr, nullptr, left.data(), right.data(), 1U, &on, 1U);
    settings.armed = true;
    oneShot.setSettings(settings);
    check(oneShot.anyPlaybackActive(), "Arm preserves existing finite OneShot performance playback");
    const MidiEvent off{0U, 36U, 0U, MidiEventType::NoteOff};
    oneShot.process(nullptr, nullptr, left.data(), right.data(), left.size(), &off, 1U);
    for (std::size_t frame = 0U; frame < left.size(); ++frame) {
        const float expected = frame < 7U ? static_cast<float>(frame + 2U) : 0.0f;
        close(left[frame], expected, "OneShot continues to its natural End while armed");
        close(right[frame], -10.0f * expected, "OneShot Arm transition preserves stereo");
    }
    check(!oneShot.anyPlaybackActive(), "OneShot still finishes while armed");
}

void sample_mixer_and_varispeed() {
    midichopper::SamplerEngine engine(1000.0, 1.0);
    auto engineSettings = engine.settings();
    engineSettings.monitorInput = false;
    engine.setSettings(engineSettings);

    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 6;
    source.stereo = {1,10, 2,20, 3,30, 4,40, 5,50, 6,60};
    source.peak = 60.0f;
    source.rms = 20.0f;
    check(engine.importPad(0, source), "import mixer source");

    sms::dsp::SampleMixerSettings mixer;
    mixer.gainDecibels = -6.0205999f;
    mixer.pan = -1.0f;
    mixer.tuneSemitones = 12.0f;
    engine.setPadMixerSettings(0, mixer);
    const auto stored = engine.padMixerSettings(0);
    close(stored.gainDecibels, mixer.gainDecibels, "mixer gain round-trips");
    close(stored.pan, mixer.pan, "mixer pan round-trips");
    close(stored.tuneSemitones, mixer.tuneSemitones, "mixer tune round-trips");

    float left[5]{};
    float right[5]{};
    const midichopper::MidiEvent on{0, 36, 127, midichopper::MidiEventType::NoteOn};
    engine.process(nullptr, nullptr, left, right, 5U, &on, 1U);
    close(left[0], 0.5f, "per-pad gain applies to playback");
    close(left[1], 1.5f, "one octave up advances the source by two frames");
    close(left[2], 2.5f, "varispeed keeps gain while advancing");
    close(left[3], 0.0f, "one octave up halves playback duration");
    close(right[0], 0.0f, "full-left stereo balance mutes the right channel");

    mixer = {};
    mixer.pan = 1.0f;
    mixer.tuneSemitones = -12.0f;
    engine.setPadMixerSettings(0, mixer);
    std::fill(std::begin(left), std::end(left), 0.0f);
    std::fill(std::begin(right), std::end(right), 0.0f);
    engine.process(nullptr, nullptr, left, right, 5U, &on, 1U);
    close(left[0], 0.0f, "full-right stereo balance mutes the left channel");
    close(right[0], 10.0f, "full-right balance retains the right source");
    close(right[1], 15.0f, "one octave down interpolates at half speed");
    close(right[4], 30.0f, "one octave down doubles playback duration");

    midichopper::SamplerEngine combined(1000.0, 1.0);
    auto combinedSettings = combined.settings();
    combinedSettings.monitorInput = false;
    combinedSettings.gain = 0.5f;
    combinedSettings.pan = 1.0f;
    combinedSettings.tuneSemitones = 5.0f;
    combined.setSettings(combinedSettings);
    check(combined.importPad(0, source), "import global mixer source");
    mixer = {};
    mixer.pan = -0.25f;
    mixer.tuneSemitones = 7.0f;
    combined.setPadMixerSettings(0, mixer);
    std::fill(std::begin(left), std::end(left), 0.0f);
    std::fill(std::begin(right), std::end(right), 0.0f);
    combined.process(nullptr, nullptr, left, right, 2U, &on, 1U);
    close(left[0], 0.125f, "global volume multiplies per-pad gain");
    close(right[0], 5.0f, "global and per-pad pan values add before balance");
    close(right[1], 15.0f, "global and per-pad tune values add before varispeed");

    mixer.gainDecibels = -12.0f;
    engine.setPadMixerSettings(0, mixer);
    sms::dsp::SamplePlaybackSettings editedPlayback;
    editedPlayback.start = 0.25f;
    engine.setPadPlaybackSettings(0, editedPlayback);
    check(engine.importPad(0, source), "replacement import succeeds");
    const auto reset = engine.padMixerSettings(0);
    close(reset.gainDecibels, 0.0f, "import resets mixer gain");
    close(reset.pan, 0.0f, "import resets mixer pan");
    close(reset.tuneSemitones, 0.0f, "import resets mixer tune");
    close(engine.padPlaybackSettings(0).start, 0.0f,
          "import continues to reset cut and ADSR settings");

    mixer.gainDecibels = -4.0f;
    mixer.pan = -0.5f;
    mixer.tuneSemitones = 3.0f;
    engine.setPadMixerSettings(0, mixer);
    editedPlayback.start = 0.125f;
    engine.setPadPlaybackSettings(0, editedPlayback);
    check(engine.importPad(0, source, false), "project-state audio restore succeeds");
    close(engine.padPlaybackSettings(0).start, editedPlayback.start,
          "project-state audio restore preserves cut and ADSR state ordering");
    close(engine.padMixerSettings(0).gainDecibels, mixer.gainDecibels,
          "project-state audio restore preserves mixer state ordering");
}

void live_sample_editor_updates() {
    midichopper::SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    settings.monitorInput = false;
    engine.setSettings(settings);

    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 100U;
    source.stereo.assign(200U, 1.0f);
    source.peak = source.rms = 1.0f;
    check(engine.importPad(0, source), "import live editor source");

    const midichopper::MidiEvent on{0, 36, 127, midichopper::MidiEventType::NoteOn};
    float left[4]{};
    float right[4]{};
    engine.process(nullptr, nullptr, left, right, 2U, &on, 1U);
    const float initialPosition = engine.playbackPosition();
    check(initialPosition > 1.0f && initialPosition < 2.0f,
          "active sample reports an encoded waveform playhead");

    sms::dsp::SampleMixerSettings mixer;
    mixer.gainDecibels = -6.0205999f;
    mixer.pan = 1.0f;
    mixer.tuneSemitones = 12.0f;
    engine.setPadMixerSettings(0, mixer);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 0.0f, "live pan update reaches an active voice on the next block");
    close(right[0], 0.5f, "live gain update reaches an active voice on the next block");
    check(engine.playbackPosition() > initialPosition + 0.015f,
          "live tune update changes active voice speed");

    settings.gain = 0.5f;
    settings.pan = -1.0f;
    settings.tuneSemitones = -12.0f;
    engine.setSettings(settings);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(left[0], 0.25f, "live global pan adds to the active pad on the next block");
    close(right[0], 0.25f, "live global volume reaches the active pad on the next block");
    settings.gain = 1.0f;
    settings.pan = 0.0f;
    settings.tuneSemitones = 0.0f;
    engine.setSettings(settings);

    sms::dsp::SamplePlaybackSettings playback;
    playback.sustainLevel = 0.25f;
    engine.setPadPlaybackSettings(0, playback);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(right[0], 0.125f, "live sustain update preserves phase and changes level");

    playback.end = 0.04f;
    engine.setPadPlaybackSettings(0, playback);
    engine.process(nullptr, nullptr, left, right, 1U);
    close(right[0], 0.0f, "moving End before the playhead stops the active voice");
    check(engine.playbackPosition() > 1.0f,
          "stopped short voice retains a terminal playhead long enough for the UI");
    std::array<float, 101> silentLeft{};
    std::array<float, 101> silentRight{};
    engine.process(nullptr, nullptr, silentLeft.data(), silentRight.data(),
                   static_cast<std::uint32_t>(silentLeft.size()));
    close(engine.playbackPosition(), 0.0f,
          "terminal waveform playhead clears after its display hold");

    midichopper::SamplerEngine extended(1000.0, 1.0);
    extended.setSettings(settings);
    check(extended.importPad(0, source), "import source for live End extension");
    playback = {};
    playback.end = 0.1f;
    playback.releaseSeconds = 0.004f;
    extended.setPadPlaybackSettings(0, playback);
    float extensionLeft[8]{};
    float extensionRight[8]{};
    extended.process(nullptr, nullptr, extensionLeft, extensionRight, 7U, &on, 1U);
    playback.end = 1.0f;
    extended.setPadPlaybackSettings(0, playback);
    extended.process(nullptr, nullptr, extensionLeft, extensionRight, 1U);
    close(extensionLeft[0], 1.0f,
          "extending End resumes a voice that entered its automatic release");
    check(extended.playbackPosition() > 1.0f,
          "extended voice keeps advancing its waveform playhead");
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

void pad_clipboard_snapshot() {
    midichopper::SamplerEngine engine(1000.0, 1.0);
    midichopper::PadData source;
    source.sampleRate = 1000.0;
    source.frames = 4;
    source.stereo = {-1.0f, -0.5f, -0.25f, 0.0f,
                     0.25f, 0.5f, 0.75f, 1.0f};
    source.peak = 1.0f;
    source.rms = 0.6f;
    check(engine.importPad(0, source), "clipboard source imports");

    sms::dsp::SamplePlaybackSettings settings;
    settings.start = 0.25f;
    settings.end = 0.75f;
    settings.attackSeconds = 0.01f;
    settings.decaySeconds = 0.02f;
    settings.sustainLevel = 0.6f;
    settings.releaseSeconds = 0.03f;
    engine.setPadPlaybackSettings(0, settings);
    sms::dsp::SampleMixerSettings mixerSettings;
    mixerSettings.gainDecibels = -9.0f;
    mixerSettings.pan = 0.4f;
    mixerSettings.tuneSemitones = -5.0f;
    mixerSettings.reverse = 1.0f;
    engine.setPadMixerSettings(0, mixerSettings);

    midichopper::PadClipboard clipboard;
    check(!clipboard.pasteTo(engine, 17),
          "paste fails while the clipboard is empty");
    check(!clipboard.hasSample() && clipboard.copyFrom(engine, 0) && clipboard.hasSample(),
          "copy captures an occupied pad");
    engine.clearPad(0);
    check(!clipboard.copyFrom(engine, 0),
          "copying an empty pad fails without replacing the clipboard");

    midichopper::PadData oldTarget = source;
    oldTarget.stereo.assign(oldTarget.stereo.size(), 0.1f);
    check(engine.importPad(17, oldTarget) && clipboard.pasteTo(engine, 17),
          "paste replaces an occupied target after the source is cleared");

    midichopper::PadData pasted;
    check(engine.exportPad(17, pasted) && pasted.frames == source.frames &&
          pasted.sampleRate == source.sampleRate && pasted.stereo == source.stereo,
          "paste restores the copied audio exactly");
    const auto pastedSettings = engine.padPlaybackSettings(17);
    close(pastedSettings.start, settings.start, "paste restores region start");
    close(pastedSettings.end, settings.end, "paste restores region end");
    close(pastedSettings.attackSeconds, settings.attackSeconds, "paste restores attack");
    close(pastedSettings.decaySeconds, settings.decaySeconds, "paste restores decay");
    close(pastedSettings.sustainLevel, settings.sustainLevel, "paste restores sustain");
    close(pastedSettings.releaseSeconds, settings.releaseSeconds, "paste restores release");
    const auto pastedMixer = engine.padMixerSettings(17);
    close(pastedMixer.reverse, 1.0f, "clipboard snapshot preserves reverse direction");
    close(pastedMixer.gainDecibels, mixerSettings.gainDecibels,
          "paste restores mixer gain");
    close(pastedMixer.pan, mixerSettings.pan, "paste restores mixer pan");
    close(pastedMixer.tuneSemitones, mixerSettings.tuneSemitones,
          "paste restores mixer tune");
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
    s.armed = true; s.monitorInput = true; s.gain = 0.5f; s.monitorGain = 0.25f;
    e.setSettings(s);
    float input[] = {1, 2, 3, 4}; float left[4]{}; float right[4]{};
    const midichopper::MidiEvent events[] = {
        {0, 60, 127, midichopper::MidiEventType::NoteOn},
        {2, 60, 0, midichopper::MidiEventType::NoteOff},
    };
    e.process(input, input, left, right, 4, events, 2);
    close(left[1], 0.5f, "monitor gain alone scales input during recording");
    check(e.padMetadata(0).recording, "note-off does not end sequential capture");

    s.armed = false; e.setSettings(s);
    e.process(nullptr, nullptr, left, right, 0);
    check(e.padMetadata(0).occupied && e.padMetadata(0).frames == 4,
          "disarming finalizes the open slice");
    midichopper::PadData captured;
    check(e.exportPad(0, captured), "export recording with independent gains");
    for (std::uint32_t frame = 0; frame < captured.frames; ++frame) {
        close(captured.stereo[frame * 2U], input[frame],
              "neither gain changes recorded left PCM");
        close(captured.stereo[frame * 2U + 1U], input[frame],
              "neither gain changes recorded right PCM");
    }

    sms::dsp::SampleMixerSettings mixer;
    mixer.gainDecibels = -3.0f;
    mixer.pan = 0.25f;
    mixer.tuneSemitones = 5.0f;
    e.setPadMixerSettings(0, mixer);
    e.setSampleRate(2000.0);
    check(e.padMetadata(0).occupied && e.padMetadata(0).frames == 4,
          "host sample-rate change preserves captured pads");
    const auto preservedMixer = e.padMixerSettings(0);
    close(preservedMixer.gainDecibels, mixer.gainDecibels,
          "host sample-rate change preserves mixer gain");
    close(preservedMixer.pan, mixer.pan, "host sample-rate change preserves mixer pan");
    close(preservedMixer.tuneSemitones, mixer.tuneSemitones,
          "host sample-rate change preserves mixer tune");
}

void independent_monitor_and_playback_gain() {
    using namespace midichopper::plugin;
    const auto monitorRange = parameterRange(kParameterMonitorGainDb);
    check(monitorRange.defaultValue == 0.0f && monitorRange.minimum == -24.0f &&
          monitorRange.maximum == 12.0f,
          "monitor gain exposes the playback gain range independently");

    midichopper::SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    const float inputLeft[] = {0.25f};
    const float inputRight[] = {-0.5f};
    float left[1]{}, right[1]{};
    settings.gain = 0.0f;
    settings.monitorGain = 0.5f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.125f, "monitor left remains audible with playback gain zero");
    close(right[0], -0.25f, "monitor gain preserves stereo input");
    settings.gain = 2.0f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.125f, "playback gain changes do not scale monitor input");
    close(right[0], -0.25f, "playback gain leaves right monitor input unchanged");
    settings.monitorGain = 2.0f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.5f, "monitor gain can boost monitor input");
    close(right[0], -1.0f, "monitor boost keeps stereo input independent");

    midichopper::PadData pad;
    pad.sampleRate = 1000.0;
    pad.frames = 16U;
    for (std::uint32_t frame = 0; frame < pad.frames; ++frame) {
        pad.stereo.push_back(0.8f);
        pad.stereo.push_back(-0.4f);
    }
    check(engine.importPad(0U, pad), "import stereo pad for independent gain paths");
    settings.monitorInput = false;
    settings.monitorGain = 0.0f;
    settings.gain = 0.5f;
    engine.setSettings(settings);
    const midichopper::MidiEvent on{
        0U, settings.baseNote, 127U, midichopper::MidiEventType::NoteOn};
    engine.process(inputLeft, inputRight, left, right, 1U, &on, 1U);
    close(left[0], 0.4f, "playback gain scales left pad with monitor gain zero");
    close(right[0], -0.2f, "playback gain scales right pad independently");
    settings.monitorGain = 2.0f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.4f, "monitor gain changes do not scale pad playback");
    close(right[0], -0.2f, "monitor gate stays closed despite monitor boost");

    settings.monitorInput = true;
    settings.monitorGain = 0.25f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.4625f, "left output sums separately scaled monitor and pad");
    close(right[0], -0.325f, "right output sums separately scaled monitor and pad");
    settings.gain = 0.0f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.0625f, "muting playback leaves only monitored left input");
    close(right[0], -0.125f, "muting playback leaves only monitored right input");
    settings.gain = 0.5f;
    settings.monitorGain = 0.0f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.4f, "muting monitor leaves only left pad playback");
    close(right[0], -0.2f, "muting monitor leaves only right pad playback");

    engine.stopAllPlayback();
    settings.monitorGain = 0.25f;
    engine.setSettings(settings);
    engine.startChopPreview(0U, 1U, 0U, 2U);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.4625f, "raw preview uses playback gain before monitor sum");
    close(right[0], -0.325f, "raw stereo preview uses its own gain path");
    settings.gain = 0.0f;
    engine.setSettings(settings);
    engine.process(inputLeft, inputRight, left, right, 1U);
    close(left[0], 0.0625f, "muted raw preview leaves monitored input audible");
    close(right[0], -0.125f, "muted raw preview leaves right monitor input audible");

    settings.gain = std::numeric_limits<float>::quiet_NaN();
    settings.monitorGain = std::numeric_limits<float>::infinity();
    engine.setSettings(settings);
    close(engine.settings().gain, 1.0f, "non-finite playback gain defaults to unity");
    close(engine.settings().monitorGain, 1.0f, "non-finite monitor gain defaults to unity");
    settings.gain = -1.0f;
    settings.monitorGain = -2.0f;
    engine.setSettings(settings);
    close(engine.settings().gain, 0.0f, "negative playback gain sanitizes to zero");
    close(engine.settings().monitorGain, 0.0f, "negative monitor gain sanitizes to zero");
}

void input_monitor_modes() {
    using namespace midichopper::plugin;
    check(parameterRange(kParameterInputMonitor).defaultValue == 2.0f &&
          parameterRange(kParameterInputMonitor).maximum == 2.0f,
          "monitor defaults to Auto and exposes all three modes");
    check(nextInputMonitorMode(0.0f) == 1.0f &&
          nextInputMonitorMode(1.0f) == 2.0f &&
          nextInputMonitorMode(2.0f) == 0.0f,
          "monitor control cycles Off, On, Auto");

    midichopper::SamplerEngine engine(1000.0, 1.0);
    auto settings = engine.settings();
    const float input[] = {0.25f};
    float left[1]{};
    float right[1]{};
    const auto checkMonitor = [&](const float mode, const bool armed,
                                  const float expected, const char* message) {
        settings.armed = armed;
        settings.monitorInput = inputMonitorEnabled(mode, armed);
        engine.setSettings(settings);
        engine.process(input, input, left, right, 1);
        close(left[0], expected, message);
        close(right[0], expected, message);
    };
    checkMonitor(0.0f, false, 0.0f, "Off mutes input in Play");
    checkMonitor(0.0f, true, 0.0f, "Off mutes input in Arm");
    checkMonitor(1.0f, true, 0.25f, "On passes input in Arm");
    checkMonitor(1.0f, false, 0.25f, "On passes input in Play");
    checkMonitor(2.0f, false, 0.0f, "Auto mutes input in Play");
    checkMonitor(2.0f, true, 0.25f, "Auto passes input in Arm");
    checkMonitor(2.0f, false, 0.0f, "Auto mutes input again after disarming");
}

void unbounded_midi_source_preserves_late_note_off() {
    midichopper::SamplerEngine engine(1000.0, 1.0);
    midichopper::PadData pad;
    pad.sampleRate = 1000.0;
    pad.frames = 20;
    pad.stereo.assign(40, 1.0f);
    check(engine.importPad(0, pad), "import gated-playback pad");
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = midichopper::PlaybackMode::Gated;
    engine.setSettings(settings);

    std::vector<midichopper::MidiEvent> events;
    events.emplace_back(0, settings.baseNote, 127, midichopper::MidiEventType::NoteOn);
    for (int i = 0; i < 1024; ++i)
        events.emplace_back(1, 127, 127, midichopper::MidiEventType::NoteOn);
    events.emplace_back(2, settings.baseNote, 0, midichopper::MidiEventType::NoteOff);
    struct Cursor {
        const std::vector<midichopper::MidiEvent>* events;
        std::size_t index = 0;
    } cursor{&events};
    const midichopper::MidiEventSource source{
        &cursor, [](void* opaque, midichopper::MidiEvent& event) noexcept {
            auto& current = *static_cast<Cursor*>(opaque);
            if (current.index == current.events->size()) return false;
            event = (*current.events)[current.index++];
            return true;
        }};
    float left[4]{};
    float right[4]{};
    engine.process(nullptr, nullptr, left, right, 4, source);
    check(left[0] > 0.0f && left[1] > 0.0f && left[2] == 0.0f,
          "late note-off survives more than 1024 events in one block");
    check(cursor.index == events.size(), "event source consumes every MIDI event");
}
}

int main() {
    newSessionDefaults();
    stableParameterIds();
    live_peak_meter();
    color_effects();
    visible_waveform_range();
    sequential_boundaries_and_preroll();
    rechop_and_raw_preview();
    rechop_empty_neighbors();
    long_sample_split_and_roll();
    pad_structure_edits();
    full_bank_and_undo();
    bank_and_layout_mapping();
    all_bank_midi_mapping();
    capture_target_selection();
    completed_capture_retargeting();
    playback_and_rate_conversion();
    playback_trigger_notifications();
    shared_storage_blocks();
    retained_storage_reserve_and_bounded_reclamation();
    maximum_voice_limit();
    stop_all_playback();
    sample_region_and_adsr();
    gated_region_looping();
    gated_loop_release_and_edits();
    live_loop_region_edits();
    asymmetric_trim_reverse_coordinates();
    live_editor_flip_preserves_voice();
    live_editor_flip_fractional_and_single_frame();
    live_editor_flip_during_one_shot_and_release();
    live_editor_flip_fractional_seam_and_coalesced_noop();
    editor_flip_target_survives_later_mixer_restore();
    global_reverse_xor_matrix();
    global_reverse_live_voices_and_editor();
    global_reverse_audition_release_and_raw_preview();
    playback_direction_latched_at_trigger();
    reverse_boundaries_and_envelopes();
    independent_pad_audition();
    pad_audition_lifecycle();
    arm_stops_gate_without_interrupting_capture();
    sample_mixer_and_varispeed();
    live_sample_editor_updates();
    pad_replacement_hardening();
    pad_clipboard_snapshot();
    fixed_duration();
    disarm_note_off_gain_and_rate_change();
    independent_monitor_and_playback_gain();
    input_monitor_modes();
    unbounded_midi_source_preserves_late_note_off();
    std::cout << "core tests passed\n";
}
