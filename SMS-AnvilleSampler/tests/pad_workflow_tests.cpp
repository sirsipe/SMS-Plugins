#include "PadWorkflows.hpp"
#include "ChopEditorProtocol.hpp"
#include "PadStructureProtocol.hpp"
#include "StateCodec.hpp"
#include "StateKeys.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace midichopper;
using namespace midichopper::plugin;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Observer final : PadWorkflowObserver {
    std::vector<std::pair<std::string, std::string>> messages;
    PadClipboardResultCode clipboardResult = PadClipboardResultCode::none;
    PadFileResultCode fileResult = PadFileResultCode::none;
    bool available = false;
    void publishUiState(const char* key, const char* value) override
    { messages.emplace_back(key, value); }
    void publishFileResult(PadFileResultCode result) noexcept override { fileResult = result; }
    void publishClipboardResult(PadClipboardResultCode result) noexcept override
    { clipboardResult = result; }
    void clipboardAvailable() noexcept override { available = true; }
    std::string latest(const char* key) const
    {
        for (auto i = messages.rbegin(); i != messages.rend(); ++i)
            if (i->first == key) return i->second;
        return {};
    }
};

PadData sample()
{
    PadData result;
    result.frames = 8;
    result.peak = 0.5f;
    result.stereo = {0.1f,-0.1f,0.2f,-0.2f,0.3f,-0.3f,0.4f,-0.4f,
                     0.5f,-0.5f,0.4f,-0.4f,0.3f,-0.3f,0.2f,-0.2f};
    return result;
}

bool samePlayback(const sms::dsp::SamplePlaybackSettings& a,
                  const sms::dsp::SamplePlaybackSettings& b)
{
    return a.start == b.start && a.end == b.end && a.attackSeconds == b.attackSeconds &&
        a.decaySeconds == b.decaySeconds && a.sustainLevel == b.sustainLevel &&
        a.releaseSeconds == b.releaseSeconds;
}

bool sameMixer(const sms::dsp::SampleMixerSettings& a,
               const sms::dsp::SampleMixerSettings& b)
{
    return a.gainDecibels == b.gainDecibels && a.pan == b.pan &&
        a.tuneSemitones == b.tuneSemitones && a.lowpass == b.lowpass &&
        a.highpass == b.highpass && a.filterSlope == b.filterSlope && a.dirty == b.dirty;
}

void checkSettingsReply(const Observer& observer, const SamplerEngine& engine,
                        const std::uint32_t pad,
                        const sms::dsp::SamplePlaybackSettings& expectedPlayback,
                        const sms::dsp::SampleMixerSettings& expectedMixer,
                        const char* message)
{
    sms::dsp::SamplePlaybackSettings playbackReply;
    sms::dsp::SampleMixerSettings mixerReply;
    const auto playbackText = observer.latest(kPadEditStateKeys[pad].c_str());
    const auto mixerText = observer.latest(kPadMixerStateKeys[pad].c_str());
    check(!playbackText.empty() && !mixerText.empty() &&
          decodePlaybackSettings(playbackText.c_str(), playbackReply) &&
          decodeMixerSettings(mixerText.c_str(), mixerReply) &&
          samePlayback(playbackReply, expectedPlayback) &&
          sameMixer(mixerReply, expectedMixer) &&
          samePlayback(engine.padPlaybackSettings(pad), expectedPlayback) &&
          sameMixer(engine.padMixerSettings(pad), expectedMixer), message);
}

struct PadSnapshot {
    PadData audio;
    sms::dsp::SamplePlaybackSettings playback;
    sms::dsp::SampleMixerSettings mixer;
};

PadSnapshot snapshotPad(const SamplerEngine& engine, const std::uint32_t pad)
{
    PadSnapshot result;
    check(engine.exportPublishedPad(pad, result.audio, &result.playback, &result.mixer),
          "snapshot published pad");
    return result;
}

bool sameSnapshot(const PadSnapshot& a, const PadSnapshot& b)
{
    return a.audio.frames == b.audio.frames && a.audio.sampleRate == b.audio.sampleRate &&
        a.audio.peak == b.audio.peak && a.audio.rms == b.audio.rms &&
        a.audio.generation == b.audio.generation && a.audio.stereo == b.audio.stereo &&
        samePlayback(a.playback, b.playback) && sameMixer(a.mixer, b.mixer);
}

template <std::size_t Count>
std::array<PadSnapshot, Count> snapshotRange(const SamplerEngine& engine,
                                            const std::uint32_t firstPad)
{
    std::array<PadSnapshot, Count> result;
    for (std::size_t i = 0; i < Count; ++i)
        result[i] = snapshotPad(engine, firstPad + static_cast<std::uint32_t>(i));
    return result;
}

template <std::size_t Count>
bool sameSnapshots(const std::array<PadSnapshot, Count>& a,
                   const std::array<PadSnapshot, Count>& b)
{
    for (std::size_t i = 0; i < Count; ++i)
        if (!sameSnapshot(a[i], b[i])) return false;
    return true;
}

void clipboardSnapshotAndReplies()
{
    SamplerEngine engine(48000, 0.001);
    sms::audio::RealtimeCommandDispatcher dispatcher;
    engine.setControlDispatcher(&dispatcher,
        &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    Observer observer;
    PadWorkflows workflows(engine, dispatcher, observer);
    const auto original = sample();
    check(engine.importPad(0, original), "seed clipboard source");
    auto playback = engine.padPlaybackSettings(0);
    playback.start = 0.25f;
    engine.setPadPlaybackSettings(0, playback);
    auto mixer = engine.padMixerSettings(0);
    mixer.gainDecibels = -6;
    engine.setPadMixerSettings(0, mixer);
    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::copy, 0));
    check(observer.available && observer.clipboardResult == PadClipboardResultCode::copied,
          "copy publishes availability and completion");
    engine.clearPad(0);
    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::paste, 1));
    PadData restored;
    sms::dsp::SamplePlaybackSettings actualPlayback;
    sms::dsp::SampleMixerSettings actualMixer;
    check(engine.exportPublishedPad(1, restored, &actualPlayback, &actualMixer) &&
          restored.stereo == original.stereo && actualPlayback.start == playback.start &&
          actualMixer.gainDecibels == mixer.gainDecibels,
          "paste retains copy-time PCM and settings after source clear");
    check(observer.clipboardResult == PadClipboardResultCode::pasted &&
          !observer.latest(kWaveformDataKey).empty() &&
          !observer.latest(kPadEditStateKeys[1].c_str()).empty() &&
          !observer.latest(kPadMixerStateKeys[1].c_str()).empty(),
          "paste publishes waveform, both settings, and completion");
    workflows.handlePadClipboardRequest("invalid");
    check(observer.clipboardResult == PadClipboardResultCode::failed,
          "invalid clipboard action reports failure");
    workflows.handlePadFileRequest("invalid");
    check(observer.fileResult == PadFileResultCode::failed &&
          observer.latest(kPadFileStatusKey) == "Invalid pad file request",
          "invalid WAV action reports failure to adapter");
}

void clipboardEmptyAndSamePadReplies()
{
    SamplerEngine engine(48000, 0.001);
    sms::audio::RealtimeCommandDispatcher dispatcher;
    engine.setControlDispatcher(&dispatcher,
        &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    Observer observer;
    PadWorkflows workflows(engine, dispatcher, observer);

    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::paste, 2));
    check(observer.clipboardResult == PadClipboardResultCode::failed && !observer.available,
          "paste with an empty clipboard fails without advertising availability");
    check(engine.importPad(0, sample()), "seed pad for clipboard copy failure");
    auto sourcePlayback = engine.padPlaybackSettings(0);
    sourcePlayback.start = 0.2f;
    sourcePlayback.releaseSeconds = 0.15f;
    engine.setPadPlaybackSettings(0, sourcePlayback);
    auto sourceMixer = engine.padMixerSettings(0);
    sourceMixer.gainDecibels = -4.0f;
    sourceMixer.pan = 0.25f;
    engine.setPadMixerSettings(0, sourceMixer);
    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::copy, 0));
    check(observer.clipboardResult == PadClipboardResultCode::copied && observer.available,
          "occupied pad creates clipboard snapshot");
    engine.clearPad(0);
    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::copy, 0));
    check(observer.clipboardResult == PadClipboardResultCode::failed,
          "copying an empty pad fails");
    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::paste, 0));
    PadData restored;
    check(observer.clipboardResult == PadClipboardResultCode::pasted &&
          engine.exportPublishedPad(0, restored) && restored.stereo == sample().stereo,
          "failed empty-pad copy leaves clipboard snapshot available for same-pad paste");
    checkSettingsReply(observer, engine, 0, sourcePlayback, sourceMixer,
                       "same-pad paste publishes the complete playback and mixer settings");

    workflows.handlePadClipboardRequest(encodePadClipboardRequest(PadClipboardAction::paste, 16));
    PadData crossBank;
    check(observer.clipboardResult == PadClipboardResultCode::pasted &&
          engine.exportPublishedPad(16, crossBank) && crossBank.stereo == restored.stereo,
          "clipboard paste works across banks and publishes target settings");
    checkSettingsReply(observer, engine, 16, sourcePlayback, sourceMixer,
                       "cross-bank paste publishes the complete playback and mixer settings");
}

void splitPlanAndConflict()
{
    SamplerEngine engine(48000, 0.001);
    sms::audio::RealtimeCommandDispatcher dispatcher;
    engine.setControlDispatcher(&dispatcher,
        &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    Observer observer;
    PadWorkflows workflows(engine, dispatcher, observer);
    const auto original = sample();
    check(engine.importPad(0, original), "seed split source");
    const PadStructureRequest prepare{PadStructureAction::prepareSplit, 0, 16, 0};
    workflows.handlePadStructureRequest(encodePadStructureRequest(prepare));
    SplitPlanReady plan;
    check(decodeSplitPlanReady(observer.latest(kPadStructureStatusKey), plan),
          "prepare publishes a usable split plan");
    const PadStructureRequest apply{PadStructureAction::applySplit, 0, 0, 0, plan.planId, 4};
    workflows.handlePadStructureRequest(encodePadStructureRequest(apply));
    PadData prefix, suffix;
    check(engine.exportPublishedPad(0, prefix) && engine.exportPublishedPad(1, suffix) &&
          prefix.frames == 4 && suffix.frames == 4,
          "split workflow commits both halves");
    std::vector<float> joined = prefix.stereo;
    joined.insert(joined.end(), suffix.stereo.begin(), suffix.stereo.end());
    check(joined == original.stereo, "split workflow preserves stereo PCM order");
    const auto committedSplit = snapshotRange<2>(engine, 0);
    workflows.handlePadStructureRequest(encodePadStructureRequest(apply));
    check(observer.latest(kPadStructureStatusKey).starts_with("PS1;E;") &&
          sameSnapshots(committedSplit, snapshotRange<2>(engine, 0)),
          "successful split plan is consumed and cannot be replayed");
    workflows.handlePadStructureRequest(encodePadStructureRequest(prepare));
    check(decodeSplitPlanReady(observer.latest(kPadStructureStatusKey), plan),
          "prepare another split plan");
    auto mixer = engine.padMixerSettings(0);
    mixer.pan = 0.5f;
    engine.setPadMixerSettings(0, mixer);
    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::applySplit, 0, 0, 0, plan.planId, 2}));
    check(observer.latest(kPadStructureStatusKey).starts_with("PS1;E;") &&
          engine.padMetadata(0).frames == 4 && engine.padMetadata(1).frames == 4,
          "settings change invalidates split without modifying PCM");
}

void splitPlanLifecycle()
{
    SamplerEngine engine(48000, 0.001);
    sms::audio::RealtimeCommandDispatcher dispatcher;
    engine.setControlDispatcher(&dispatcher,
        &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    Observer observer;
    PadWorkflows workflows(engine, dispatcher, observer);
    check(engine.importPad(0, sample()), "seed split plan lifecycle source");
    const PadStructureRequest prepare{PadStructureAction::prepareSplit, 0, 16, 0};
    workflows.handlePadStructureRequest(encodePadStructureRequest(prepare));
    SplitPlanReady replaced;
    check(decodeSplitPlanReady(observer.latest(kPadStructureStatusKey), replaced),
          "first split plan is prepared");
    workflows.handlePadStructureRequest(encodePadStructureRequest(prepare));
    SplitPlanReady active;
    check(decodeSplitPlanReady(observer.latest(kPadStructureStatusKey), active) &&
          active.planId != replaced.planId, "new preparation replaces the prior plan");
    const auto beforeRejectedApply = snapshotRange<2>(engine, 0);
    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::applySplit, 0, 0, 0, replaced.planId, 2}));
    check(observer.latest(kPadStructureStatusKey).starts_with("PS1;E;") &&
          sameSnapshots(beforeRejectedApply, snapshotRange<2>(engine, 0)),
          "replaced split plan cannot be applied");
    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::cancelSplit, 0, 0, 0, active.planId}));
    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::applySplit, 0, 0, 0, active.planId, 2}));
    check(observer.latest(kPadStructureStatusKey).starts_with("PS1;E;") &&
          sameSnapshots(beforeRejectedApply, snapshotRange<2>(engine, 0)),
          "cancelled split plan cannot be applied");

    workflows.handlePadStructureRequest(encodePadStructureRequest(prepare));
    SplitPlanReady invalid;
    check(decodeSplitPlanReady(observer.latest(kPadStructureStatusKey), invalid),
          "prepare plan for invalid split frame");
    const auto beforeInvalidApply = snapshotRange<2>(engine, 0);
    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::applySplit, 0, 0, 0, invalid.planId, sample().frames}));
    check(observer.latest(kPadStructureStatusKey).starts_with("PS1;E;") &&
          sameSnapshots(beforeInvalidApply, snapshotRange<2>(engine, 0)),
          "out-of-range split frame fails without changing pads");
    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::applySplit, 0, 0, 0, invalid.planId, 2}));
    check(observer.latest(kPadStructureStatusKey).starts_with("PS1;E;") &&
          sameSnapshots(beforeInvalidApply, snapshotRange<2>(engine, 0)),
          "failed split plan is consumed and cannot be replayed");
}

void collapseGapPublishesMovedSettings()
{
    SamplerEngine engine(48000, 0.001);
    sms::audio::RealtimeCommandDispatcher dispatcher;
    engine.setControlDispatcher(&dispatcher,
        &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    Observer observer;
    PadWorkflows workflows(engine, dispatcher, observer);
    const auto movedAudio = sample();
    check(engine.importPad(0, sample()) && engine.importPad(1, movedAudio),
          "seed pads around a gap");
    auto movedPlayback = engine.padPlaybackSettings(1);
    movedPlayback.start = 0.3f;
    engine.setPadPlaybackSettings(1, movedPlayback);
    auto movedMixer = engine.padMixerSettings(1);
    movedMixer.pan = -0.4f;
    engine.setPadMixerSettings(1, movedMixer);
    engine.clearPad(0);

    workflows.handlePadStructureRequest(encodePadStructureRequest(
        {PadStructureAction::collapse, 0, 16, 0}));
    PadData collapsed;
    sms::dsp::SamplePlaybackSettings playback;
    sms::dsp::SampleMixerSettings mixer;
    check(engine.exportPublishedPad(0, collapsed, &playback, &mixer) &&
          collapsed.stereo == movedAudio.stereo && playback.start == movedPlayback.start &&
          mixer.pan == movedMixer.pan && !engine.padMetadata(1).occupied,
          "collapse moves following audio and settings into the cleared slot");
    check(observer.latest(kPadStructureStatusKey) == "PS1;O;0;C",
          "collapse publishes success and moved pad settings");
    checkSettingsReply(observer, engine, 0, movedPlayback, movedMixer,
                       "collapse publishes the complete moved playback and mixer settings");
}

void chopApplyRevisionAndReplies()
{
    SamplerEngine engine(48000, 0.001);
    sms::audio::RealtimeCommandDispatcher dispatcher;
    engine.setControlDispatcher(&dispatcher,
        &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    Observer observer;
    PadWorkflows workflows(engine, dispatcher, observer);
    for (std::uint32_t pad = 0; pad < 3; ++pad)
        check(engine.importPad(pad, sample()), "seed chop apply range");

    std::array<sms::audio::WaveformSummary, 3> summaries{};
    std::array<std::uint64_t, 3> generations{};
    check(engine.getChopSnapshot(0, 3, summaries, generations),
          "capture coherent chop baseline");
    ChopApplyRequest apply;
    apply.firstPad = 0;
    apply.padCount = 3;
    apply.sequence = 17;
    apply.revisionChecked = true;
    for (std::size_t i = 0; i < generations.size(); ++i)
        apply.expectedGenerations[i] = generations[i];
    apply.boundaryOffsets[0] = 1;
    std::vector<float> originalJoined;
    for (std::uint32_t pad = 0; pad < 3; ++pad) {
        PadData source;
        check(engine.exportPublishedPad(pad, source), "capture original chop PCM");
        originalJoined.insert(originalJoined.end(), source.stereo.begin(), source.stereo.end());
    }
    workflows.handleChopApplyRequest(encodeChopApplyRequest(apply));
    check(observer.latest(kChopStatusKey) == "CH2;17;OK" && engine.padMetadata(0).frames == 9 &&
          engine.padMetadata(1).frames == 7 && engine.padMetadata(2).frames == 8,
          "revision-checked chop apply commits the requested boundary");
    std::vector<float> choppedJoined;
    for (std::uint32_t pad = 0; pad < 3; ++pad) {
        PadData chopped;
        check(engine.exportPublishedPad(pad, chopped), "export chopped PCM");
        choppedJoined.insert(choppedJoined.end(), chopped.stereo.begin(), chopped.stereo.end());
        checkSettingsReply(observer, engine, pad, engine.padPlaybackSettings(pad),
                           engine.padMixerSettings(pad),
                           "successful chop publishes complete settings for every pad");
    }
    check(choppedJoined == originalJoined,
          "successful chop preserves concatenated source PCM exactly");

    check(engine.getChopSnapshot(0, 3, summaries, generations),
          "capture baseline for stale chop apply");
    apply.sequence = 18;
    for (std::size_t i = 0; i < generations.size(); ++i)
        apply.expectedGenerations[i] = generations[i];
    const auto replacement = sample();
    check(engine.importPad(1, replacement), "change chop range after baseline");
    const auto beforeConflict = snapshotRange<3>(engine, 0);
    workflows.handleChopApplyRequest(encodeChopApplyRequest(apply));
    check(observer.latest(kChopStatusKey).starts_with("CH2;18;ERROR;") &&
          sameSnapshots(beforeConflict, snapshotRange<3>(engine, 0)),
          "stale chop revision reports conflict and preserves replacement audio");

    check(engine.getChopSnapshot(0, 3, summaries, generations),
          "capture baseline for invalid chop boundary");
    apply.sequence = 19;
    for (std::size_t i = 0; i < generations.size(); ++i)
        apply.expectedGenerations[i] = generations[i];
    apply.boundaryOffsets[0] = -100;
    const auto beforeInvalidChop = snapshotRange<3>(engine, 0);
    workflows.handleChopApplyRequest(encodeChopApplyRequest(apply));
    check(observer.latest(kChopStatusKey).starts_with("CH2;19;ERROR;") &&
          sameSnapshots(beforeInvalidChop, snapshotRange<3>(engine, 0)),
          "invalid chop boundary reports failure without changing the pad range");

    apply.sequence = 20;
    apply.revisionChecked = false;
    const auto beforeLegacyChop = snapshotRange<3>(engine, 0);
    workflows.handleChopApplyRequest(encodeChopApplyRequest(apply));
    check(observer.latest(kChopStatusKey) == "CH2;0;ERROR;Reopen Adjust Cut Points" &&
          sameSnapshots(beforeLegacyChop, snapshotRange<3>(engine, 0)),
          "legacy chop apply without coherent revisions is rejected");
}
}

int main()
{
    clipboardSnapshotAndReplies();
    clipboardEmptyAndSamePadReplies();
    splitPlanAndConflict();
    splitPlanLifecycle();
    collapseGapPublishesMovedSettings();
    chopApplyRevisionAndReplies();
    std::cout << "Pad workflow tests passed\n";
}
