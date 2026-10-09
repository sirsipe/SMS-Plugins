#include "PadWorkflows.hpp"
#include "PadStructureProtocol.hpp"
#include "StateKeys.hpp"

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
}

int main()
{
    clipboardSnapshotAndReplies();
    splitPlanAndConflict();
    std::cout << "Pad workflow tests passed\n";
}
