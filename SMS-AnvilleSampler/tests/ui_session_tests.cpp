#include "SamplerUiSession.hpp"
#include "CaptureActionProtocol.hpp"
#include "PadDirectionRequest.hpp"
#include "StateKeys.hpp"
#include "State/SampleMixerSettingsCodec.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace midichopper;
using namespace midichopper::plugin;
using namespace midichopper::ui;
using namespace std::chrono_literals;

void check(const bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Invalidation {
    UiInteractionInvalidation reason;
    bool armed;
    bool editor;
    int selected;
};

struct Host final : SamplerUiHost {
    SamplerUiSession *session = nullptr;
    std::vector<std::pair<std::string, std::string>> messages;
    std::vector<std::pair<std::uint32_t, float>> parameters;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> notes;
    std::vector<Invalidation> invalidations;
    std::vector<std::string> trace;
    int repaints = 0;
    bool parameterSawOptimisticState = false;

    void sendUiParameter(const std::uint32_t parameter, const float value) override
    {
        parameters.emplace_back(parameter, value);
        trace.push_back("parameter:" + std::to_string(parameter));
        if (parameter == kParameterOutputGainDb)
            parameterSawOptimisticState = session->state().gain == value;
    }
    void sendUiState(const char *key, const std::string &value) override
    {
        messages.emplace_back(key, value);
        trace.push_back(std::string("state:") + key);
    }
    void sendUiNote(const std::uint8_t note, const std::uint8_t velocity) override
    {
        notes.emplace_back(note, velocity);
        trace.push_back(velocity == 0 ? "note:off" : "note:on");
    }
    void repaintUi() override { ++repaints; }
    bool showUiPadFileBrowser(bool, const char *) override { return true; }
    void invalidateUiInteraction(const UiInteractionInvalidation reason) override
    {
        const auto &state = session->state();
        invalidations.push_back({reason, state.armed, state.editorMode, state.selectedPad});
        trace.push_back("invalidate:" + std::to_string(static_cast<int>(reason)));
    }

    void clearTrace()
    {
        messages.clear();
        parameters.clear();
        notes.clear();
        invalidations.clear();
        trace.clear();
        repaints = 0;
    }
    const std::string &latestMessage(const std::string &key) const
    {
        for (auto item = messages.rbegin(); item != messages.rend(); ++item)
            if (item->first == key)
                return item->second;
        check(false, "expected host message exists");
        return messages.front().second;
    }
    std::size_t position(const std::string &event) const
    {
        const auto found = std::find(trace.begin(), trace.end(), event);
        check(found != trace.end(), "expected effect occurs in trace");
        return static_cast<std::size_t>(found - trace.begin());
    }
    std::size_t position(const UiInteractionInvalidation reason) const
    {
        return position("invalidate:" + std::to_string(static_cast<int>(reason)));
    }
};

struct Fixture {
    Host host;
    SamplerUiSession session;
    explicit Fixture(const SamplerUiCapabilities capabilities = {}) : session(host, capabilities)
    {
        host.session = &session;
    }

    EditorSnapshotReply snapshot() const
    {
        EditorSnapshotReply reply;
        check(decodeEditorSnapshotRequest(host.latestMessage("waveform_request"), reply.request),
              "waveform request decodes");
        reply.waveform.pad = reply.request.pad;
        reply.waveform.frames = 48000;
        reply.waveform.sampleRate = 48000.0;
        return reply;
    }
    void deliver(const EditorSnapshotReply &reply)
    {
        check(session.acceptState("waveform_data", encodeEditorSnapshotReply(reply).c_str()),
              "snapshot state is handled");
    }
    void openReadyEditor(const int pad)
    {
        check(session.acceptState("pad_mask", "1111111111111111"), "bank mask accepted");
        session.openSampleEditor(pad);
        deliver(snapshot());
    }
    PlaybackRequest playbackRequest() const
    {
        PlaybackRequest request;
        check(decodePlaybackRequest(host.latestMessage("play_stop_request"), request),
              "playback request decodes");
        return request;
    }
};

void optimisticParametersAndArmOrdering()
{
    Fixture f;
    f.session.acceptParameter(kParameterOutputGainDb, -3.25f);
    check(f.host.parameters.empty() && f.session.state().gain == -3.25f,
          "host callback updates reported value without echoing it");
    f.session.setControlValue(kParameterOutputGainDb, -5.125f);
    check(f.host.parameterSawOptimisticState && f.host.parameters.back().second == -5.125f,
          "UI value is visible before sending the identical host value");

    f.openReadyEditor(3);
    f.session.auditionSelectedEditorPad();
    f.host.clearTrace();
    f.session.setControlValue(kParameterMode, 1.0f);
    const auto arm = std::find_if(
        f.host.invalidations.begin(), f.host.invalidations.end(),
        [](const auto &item) { return item.reason == UiInteractionInvalidation::armChanged; });
    const auto entered = std::find_if(
        f.host.invalidations.begin(), f.host.invalidations.end(),
        [](const auto &item) { return item.reason == UiInteractionInvalidation::armEntered; });
    check(arm != f.host.invalidations.end() && !arm->armed && arm->editor && arm->selected == 3,
          "mixer cancellation observes pre-Arm state");
    check(entered != f.host.invalidations.end() && entered->armed && !entered->editor &&
              entered->selected == -1,
          "region cancellation observes cleared Arm selection");
    check(f.host.position(UiInteractionInvalidation::armChanged) <
                  f.host.position("state:play_stop_request") &&
              f.host.position("state:play_stop_request") <
                  f.host.position(UiInteractionInvalidation::armEntered) &&
              f.host.position(UiInteractionInvalidation::armEntered) <
                  f.host.position("state:waveform_request") &&
              f.host.position("state:waveform_request") <
                  f.host.position("parameter:" + std::to_string(kParameterStartPad)) &&
              f.host.parameters.back().first == kParameterMode,
          "Arm effects retain cancellation, audition-stop, selection and parameter ordering");
    check(f.playbackRequest().action == PlaybackAction::StopAudition &&
              f.session.state().selectedPad == 0 && f.session.state().startPad == 0,
          "Arm stops the editor audition and selects first pad when the bank is full");
}

void captureAndPhysicalMidiOwnership()
{
    Fixture f;
    f.session.acceptParameter(kParameterMode, 1.0f);
    f.host.clearTrace();
    f.session.selectCaptureTarget(4);
    const auto first = f.host.parameters.back().second;
    f.session.selectCaptureTarget(4);
    const auto second = f.host.parameters.back().second;
    check(first != second && padFromCaptureTargetRequest(first) == 4U &&
              padFromCaptureTargetRequest(second) == 4U && f.session.state().startPad == 4,
          "repeated capture-target selection alternates transport halves for the same pad");
    f.host.clearTrace();
    f.session.captureSpace();
    check(f.host.notes.size() == 2 &&
              f.host.notes[0] == std::pair<std::uint8_t, std::uint8_t>{36, 127} &&
              f.host.notes[1] == std::pair<std::uint8_t, std::uint8_t>{36, 0},
          "capture Space emits an immediately balanced note at the visible bank's first note");

    f.session.acceptParameter(kParameterMode, 0.0f);
    f.host.clearTrace();
    f.session.pressPlaybackPad(2);
    f.session.acceptParameter(kParameterBaseMidiNote, 50.0f);
    f.session.pressPlaybackPad(3);
    f.session.releasePlaybackPad();
    check(f.host.notes ==
                  std::vector<std::pair<std::uint8_t, std::uint8_t>>{
                      {38, 127}, {38, 0}, {53, 127}, {53, 0}} &&
              f.session.padPress().pad() == -1,
          "new press and release retain original note ownership across MIDI mapping changes");

    Fixture silent({false, false, false, 8});
    silent.session.pressPlaybackPad(1);
    silent.session.releasePlaybackPad();
    silent.session.captureSpace();
    silent.session.openSampleEditor(1);
    check(silent.host.notes.empty() && silent.host.messages.empty(),
          "injected capabilities suppress unavailable MIDI and state effects");
}

void hostAndUserBankSelection()
{
    Fixture f;
    f.session.acceptParameter(kParameterMode, 1.0f);
    f.session.acceptParameter(kParameterCurrentCapturePad, 22.0f);
    check(f.session.state().bank == 1 && f.session.state().selectedPad == 21 &&
              f.session.state().startPad == 5,
          "capture output follows its pad, bank and local selection");
    f.host.clearTrace();
    f.session.selectBank(2);
    check(f.session.state().bank == 1 && f.host.parameters.empty(),
          "explicit bank selection is blocked during capture");
    f.session.acceptParameter(kParameterActiveBank, 3.0f);
    check(f.session.state().bank == 2 && f.host.parameters.empty(),
          "host bank callbacks remain authoritative during capture");

    f.session.acceptParameter(kParameterMode, 0.0f);
    f.session.chopEditor().open(34, f.session.globalPad(0), f.session.visiblePadCount(),
                                f.session.midiBankMode());
    f.host.clearTrace();
    f.session.acceptParameter(kParameterPlaybackPadEvent, playbackPadEventValue(2, false));
    check(f.session.chopEditor().active() && f.session.state().selectedPad == 34 &&
              f.session.state().bank == 2,
          "MIDI playback does not discard a cut-editor plan or bank");
    f.session.selectBank(1);
    check(f.session.state().bank == 2 && f.host.parameters.empty() &&
              std::string(f.session.state().status) == "Apply or Exit before changing bank",
          "explicit bank change reports the cut-editor guard");
    f.session.acceptParameter(kParameterActiveBank, 2.0f);
    check(!f.session.chopEditor().active() && f.session.state().bank == 1,
          "host bank change cancels the cut editor");
}

void snapshotsRejectStaleAndUnconfirmedSettings()
{
    Fixture f;
    f.session.openSampleEditor(3);
    const auto previous = f.snapshot();
    f.session.selectEditorPad(4);
    auto current = f.snapshot();
    f.deliver(previous);
    check(f.session.state().editorSettingsPad == -1 && !f.session.state().hasWaveform,
          "reply for a previously selected pad cannot populate the current editor");
    sms::dsp::SamplePlaybackSettings restored;
    restored.start = 0.2f;
    check(f.session.acceptState(kPadEditStateKeys[4].c_str(),
                                sms::state::encodeSamplePlaybackSettings(restored).c_str()),
          "individual settings handled");
    check(f.session.state().editorSettingsPad == -1,
          "individual restored settings wait while the coherent snapshot is pending");
    current.playback.start = 0.1f;
    current.mixer.pan = 0.25f;
    f.deliver(current);
    check(f.session.state().editorSettingsPad == 4 && f.session.state().mixerSettingsPad == 4 &&
              f.session.state().editorSettings.start == 0.1f &&
              f.session.state().mixerSettings.pan == 0.25f,
          "one matching reply publishes waveform and both settings groups together");

    f.session.requestWaveform();
    auto stale = f.snapshot();
    auto edited = f.session.state().editorSettings;
    edited.start = 0.3f;
    f.session.commitPlaybackSettings(edited);
    const auto now = SamplerUiSession::Clock::now();
    f.session.idle(now + 1s, 0.0f);
    auto confirmation = f.snapshot();
    check(confirmation.request.sequence != stale.request.sequence,
          "local edit replaces the pending request identity");
    f.deliver(stale);
    f.deliver(confirmation);
    check(f.session.state().editorSettings.start == 0.3f,
          "stale sequence and pre-edit worker settings cannot overwrite optimistic edits");
    f.session.idle(now + 1300ms, 0.0f);
    check(f.snapshot().request.sequence == confirmation.request.sequence,
          "retry retains its local-edit request identity");
    confirmation.playback = edited;
    confirmation.mixer = f.session.state().mixerSettings;
    f.deliver(confirmation);
    check(f.session.state().editorSettings.start == 0.3f && f.session.state().hasWaveform,
          "matching edited settings confirm the pending snapshot");
    f.session.selectMainPadForContextMenu(8);
    check(f.session.state().editorSettingsPad == -1 && f.session.state().mixerSettingsPad == -1,
          "main-view context selection invalidates settings associated with another pad");
}

void directionAndAuditionCommands()
{
    Fixture f;
    f.openReadyEditor(3);
    const auto now = SamplerUiSession::Clock::now();
    check(f.session.detail().setZoomPosition(0.6f, now), "editor can zoom before direction change");
    const auto viewport = f.session.detail().viewport();
    f.host.clearTrace();
    f.session.flipSampleDirection();
    PadDirectionRequest direction;
    check(decodePadDirectionRequest(f.host.latestMessage("pad_direction_request"), direction) &&
              direction.pad == 3U && direction.mixer.reverse == 1.0f &&
              f.host.position("state:pad_direction_request") <
                  f.host.position("state:waveform_request"),
          "direction change sends one combined edit before requesting confirmation");
    auto confirmation = f.snapshot();
    confirmation.playback = direction.playback;
    confirmation.mixer = direction.mixer;
    f.deliver(confirmation);
    check(f.session.effectiveSampleReverse() && f.session.detail().viewport().zoomed() &&
              f.session.detail().viewport().start == viewport.start &&
              f.session.detail().viewport().end == viewport.end,
          "confirmation for the same source retains the zoomed viewport after reversal");

    f.host.clearTrace();
    f.session.pressPlaybackPad(1);
    f.session.auditionSelectedEditorPad();
    check(f.playbackRequest().action == PlaybackAction::Play &&
              f.host.position("note:off") < f.host.position("state:play_stop_request") &&
              f.session.padPress().pad() == -1,
          "editor audition releases a physical note before Play");
    f.session.cycleEditorAuditionMode();
    check(f.session.state().editorAuditionMode == EditorAuditionMode::Loop &&
              f.playbackRequest().action == PlaybackAction::Loop,
          "cycling Play to Loop replaces the audition immediately");
    f.session.cycleEditorAuditionMode();
    check(f.session.state().editorAuditionMode == EditorAuditionMode::Off &&
              f.playbackRequest().action == PlaybackAction::StopAudition,
          "cycling Loop to Off stops only the UI audition");
    f.session.closeSampleEditor();
    check(!f.session.state().editorMode && f.session.state().selectedPad == -1,
          "closing without a last played selection restores the main-view sentinel");
}

void clearPadsAndMeterPacing()
{
    Fixture f;
    f.openReadyEditor(3);
    f.session.acceptParameter(kParameterGlobalPan, 0.25f);
    f.session.acceptParameter(kParameterOutputGainDb, -6.0f);
    f.session.cycleEditorAuditionMode();
    f.session.pressPlaybackPad(1);
    f.host.clearTrace();
    f.session.clearAllPadContents();
    const auto &state = f.session.state();
    check(state.selectedPad == -1 && state.editorSettingsPad == -1 && !state.hasWaveform &&
              !state.editorMode &&
              std::all_of(state.padState.begin(), state.padState.end(),
                          [](const auto item) { return item == '0'; }) &&
              state.globalPan == 0.25f && state.gain == -6.0f &&
              state.editorAuditionMode == EditorAuditionMode::Loop,
          "clear pads invalidates sample state while preserving global controls and audition mode");
    check(f.host.position("note:off") < f.host.position("state:play_stop_request") &&
              f.host.position("state:play_stop_request") <
                  f.host.position(UiInteractionInvalidation::clearPads) &&
              f.host.position(UiInteractionInvalidation::clearPads) <
                  f.host.position(UiInteractionInvalidation::clearPadsInputs) &&
              f.host.position(UiInteractionInvalidation::clearPadsInputs) <
                  f.host.position(std::string("state:") + kCaptureActionRequestKey) &&
              f.host.latestMessage(kCaptureActionRequestKey) == "reset",
          "clear releases notes, stops audition, invalidates interactions, then sends reset");

    Fixture meters;
    const auto now = SamplerUiSession::Clock::now();
    meters.session.rendered(now);
    meters.session.acceptParameter(kParameterInputLevelLeft, 0.75f, now);
    meters.session.acceptParameter(kParameterOutputLevelRight,
                                   std::numeric_limits<float>::infinity(), now);
    meters.session.idle(now + 32ms, 0.0f);
    check(meters.host.repaints == 0 && meters.session.state().inputLevels[0] == 0.75f &&
              meters.session.state().outputLevels[1] == 0.0f,
          "meter values sanitize without an early repaint");
    meters.session.idle(now + 33ms, 0.0f);
    check(meters.host.repaints == 1, "meter repaint occurs at the existing 33 ms cadence");
    meters.session.acceptParameter(kParameterInputLevelLeft, 0.9f, now + 34ms);
    meters.session.rendered(now + 35ms);
    meters.session.idle(now + 100ms, 0.0f);
    check(meters.host.repaints == 1, "full rendering consumes a pending meter-only repaint");
}

void missedRepliesCancelSplitAndReplaceSnapshot()
{
    Fixture f;
    f.openReadyEditor(3);
    f.session.requestWaveform();
    const auto stale = f.snapshot();
    SplitPlanReady plan;
    plan.planId = 17;
    plan.targetPad = 3;
    plan.emptyPad = 4;
    plan.waveform.pad = 3;
    plan.waveform.frames = 48000;
    plan.waveform.sampleRate = 48000.0;
    f.session.chopEditor().openSplit(plan);
    f.session.padActions().setStructureBusy(true);
    f.host.clearTrace();
    f.session.missedReplies();
    PadStructureRequest cancel;
    check(!f.session.chopEditor().active() && !f.session.padActions().busy() &&
              decodePadStructureRequest(f.host.latestMessage("pad_structure_request"), cancel) &&
              cancel.action == PadStructureAction::cancelSplit && cancel.planId == 17,
          "missed replies invalidate structure work and cancel the retained split plan");
    check(std::string(f.session.state().status) ==
              "UI replies were missed; check the pad and retry" &&
              f.snapshot().request.sequence != stale.request.sequence,
          "missed replies report recovery and request a fresh coherent snapshot");
    f.deliver(stale);
    auto fresh = f.snapshot();
    fresh.playback.start = 0.25f;
    f.deliver(fresh);
    check(f.session.state().editorSettings.start == 0.25f,
          "fresh recovery response remains eligible after a stale response");
}

} // namespace

int main()
{
    optimisticParametersAndArmOrdering();
    captureAndPhysicalMidiOwnership();
    hostAndUserBankSelection();
    snapshotsRejectStaleAndUnconfirmedSettings();
    directionAndAuditionCommands();
    clearPadsAndMeterPacing();
    missedRepliesCancelSplitAndReplaceSnapshot();
    std::cout << "UI session tests passed\n";
}
