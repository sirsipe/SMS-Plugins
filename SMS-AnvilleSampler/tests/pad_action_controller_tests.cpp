#include "PadActionController.hpp"
#include "Parameters.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace midichopper;
using namespace midichopper::plugin;
using namespace midichopper::ui;
using namespace std::chrono_literals;

void check(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Host final : PadActionHost, ChopEditorHost {
    std::vector<std::pair<std::string, std::string>> messages;
    std::string status;
    std::string defaultName;
    bool browserAvailable = true;
    bool saving = false;
    int refreshes = 0;
    int repaints = 0;
    int sampleEditorPad = -1;
    int colorPad = -1;
    int color = -1;
    PadActionController* actions = nullptr;

    void sendPadActionState(const char* key, const std::string& value) override
    { messages.emplace_back(key, value); }
    bool showPadFileBrowser(const bool save, const char* name) override
    { saving = save; defaultName = name != nullptr ? name : ""; return browserAvailable; }
    void setPadActionStatus(const char* message) override { status = message; }
    void repaintPadActions() override { ++repaints; }
    void clearPadActionHover() noexcept override {}
    void beginPadSampleEditor(const int pad) override { sampleEditorPad = pad; }
    void refreshPadWaveform() override { ++refreshes; }
    void setPadColor(const int pad, const int value) override { colorPad = pad; color = value; }

    void sendChopState(const char* key, const std::string& value) override
    { sendPadActionState(key, value); }
    void selectChopPad(int) override {}
    void leaveChopEditor() override {}
    void resetChopViewport(std::uint64_t) override {}
    void setChopStatus(const char* message) override { status = message; }
    void setChopStructureBusy(const bool busy) override { actions->setStructureBusy(busy); }
    void repaintChopEditor() override { ++repaints; }
};

struct Fixture {
    Host host;
    ChopEditorController chop{host};
    PadActionController actions{host, chop, 9};
    std::array<char, kPadsPerBank> pads{};
    PadActionContext context;

    Fixture()
    {
        host.actions = &actions;
        pads.fill('0');
        context.padState = pads;
        context.selectedPad = 3;
    }
    void open(const int pad) { actions.openMenu(context, pad, {100.0f, 100.0f}); }
    void invoke(const PadMenuAction action,
                const PadActionController::Clock::time_point now = {})
    { actions.invoke(context, {action}, now); }
    void parameter(const std::uint32_t index, const float value)
    { check(actions.acceptParameter(context, index, value), "workflow parameter was handled"); }
    void state(const char* key, const std::string& value)
    { check(actions.acceptState(context, key, value.c_str()), "workflow state was handled"); }
};

void eligibilityAndConfirmation()
{
    Fixture f;
    f.pads[3] = '1';
    f.open(3);
    check(f.actions.actionEnabled(f.context, PadMenuAction::editSample) &&
          !f.actions.actionEnabled(f.context, PadMenuAction::exportProcessed),
          "occupied pad permits editing and raw export, with processed export awaiting settings");
    f.context.editorSettingsPad = f.context.mixerSettingsPad = 3;
    f.context.mixerSettings.tuneSemitones = 1.0f;
    check(f.actions.actionEnabled(f.context, PadMenuAction::exportProcessed),
          "processed export uses matched non-default per-pad settings");
    f.context.editorMode = true;
    check(!f.actions.actionEnabled(f.context, PadMenuAction::editSample),
          "Edit Sample remains disabled in the sample editor");
    f.state("pad_file_busy", "1");
    check(!f.actions.actionEnabled(f.context, PadMenuAction::copy) &&
          !f.actions.actionEnabled(f.context, PadMenuAction::import) &&
          f.actions.actionEnabled(f.context, PadMenuAction::color),
          "busy state gates mutations while preserving slot coloring");
    f.state("pad_file_busy", "0");
    f.host.messages.clear();
    const auto start = PadActionController::Clock::time_point{};
    f.invoke(PadMenuAction::clear, start);
    check(f.host.messages.empty(), "first clear click only arms confirmation");
    f.actions.idle(start + 2s);
    check(std::string_view(f.actions.actionView(f.context, {PadMenuAction::clear}, "").label) ==
          "CLEAR PAD", "idle expires clear confirmation after two seconds");
    f.invoke(PadMenuAction::clear, start + 2s);
    check(f.host.messages.empty(), "expired confirmation must be armed again");
    f.invoke(PadMenuAction::clear, start + 2100ms);
    check(!f.actions.menuOpen() && f.host.messages.back() ==
          std::pair<std::string, std::string>{"pad_clear_request", "4"},
          "confirmed clear sends the one-based pad and closes the menu");

    f.open(0);
    f.pads[1] = f.pads[2] = '1';
    check(f.actions.collapseShiftCount(f.context) == 3,
          "collapse label counts the next contiguous occupied run");
    f.invoke(PadMenuAction::collapseGap, start);
    f.actions.idle(start + 2s);
    check(std::string_view(f.actions.actionView(f.context,
          {PadMenuAction::collapseGap}, "COLLAPSE GAP").label) == "COLLAPSE GAP",
          "collapse has its own expiring confirmation");
    f.invoke(PadMenuAction::collapseGap, start + 2s);
    f.invoke(PadMenuAction::collapseGap, start + 2100ms);
    PadStructureRequest collapse;
    check(f.actions.busy() && decodePadStructureRequest(f.host.messages.back().second, collapse) &&
          collapse.action == PadStructureAction::collapse && collapse.targetPad == 0U,
          "confirmed collapse captures the visible range before closing the menu");
    f.state("pad_structure_status", "PS1;O;0;C");
    check(!f.actions.busy() && f.host.refreshes == 1,
          "collapse completion refreshes an open sample editor");
    f.open(0);
    f.invoke(PadMenuAction::color);
    f.actions.selectColor(f.context, 8);
    check(f.host.colorPad == 0 && f.host.color == 8 && !f.actions.menuOpen(),
          "color selection works for an empty slot and closes both menus");
}

void dialogsAndFileCompletion()
{
    Fixture f;
    f.context.firstPad = 16;
    f.context.visibleCount = 8;
    f.context.selectedPad = 17;
    f.pads[1] = '1';
    f.open(17);
    f.invoke(PadMenuAction::exportRaw);
    check(f.actions.dialogPending() && !f.actions.menuOpen() && f.host.saving &&
          f.host.defaultName == "anvillesampler-pad-02.wav",
          "dialog remembers its target and uses local pad numbering");
    const auto before = f.host.messages.size();
    f.actions.fileSelected(nullptr);
    check(!f.actions.dialogPending() && !f.actions.busy() && f.host.messages.size() == before,
          "dialog cancellation sends no request");
    f.open(17);
    f.invoke(PadMenuAction::exportRaw);
    f.actions.fileSelected("/tmp/sample.aiff");
    check(!f.actions.busy() && f.host.messages.back().first != "pad_file_request" &&
          f.host.status == "Export filename must use the .wav extension",
          "conflicting extensions are rejected before starting file work");
    f.open(17);
    f.invoke(PadMenuAction::exportRaw);
    f.actions.fileSelected("/tmp/kit;take");
    PadFileRequest request;
    check(f.actions.busy() && decodePadFileRequest(f.host.messages.back().second, request) &&
          request.pad == 17U && request.path == "/tmp/kit;take.wav",
          "export appends WAV and retains the captured physical target");
    f.parameter(kParameterPadFileResultEvent,
                padFileResultEventValue(PadFileResultCode::rawExportSucceeded, false));
    check(!f.actions.busy() && f.host.status == "WAV exported", "file output completes export");

    for (const bool alternate : {false, true}) {
        f.open(17);
        f.invoke(PadMenuAction::import);
        f.actions.fileSelected("/tmp/音源.wav");
        if (alternate) {
            f.parameter(kParameterPadFileResultEvent,
                        padFileResultEventValue(PadFileResultCode::importSucceeded, false));
            check(f.actions.busy(), "replayed completion does not complete the next import");
        }
        f.parameter(kParameterPadFileResultEvent,
                    padFileResultEventValue(PadFileResultCode::importSucceeded, alternate));
    }
    check(!f.actions.busy() && f.host.refreshes == 2 && f.host.status == "WAV imported",
          "alternating same-code imports each refresh the selected waveform exactly once");
    f.open(17);
    f.invoke(PadMenuAction::import);
    f.actions.fileSelected("/tmp/another.wav");
    f.context.selectedPad = 18;
    f.state("pad_file_status", "WAV imported");
    check(!f.actions.busy() && f.host.refreshes == 2,
          "state completion for a previously selected pad does not refresh its replacement");

    f.host.browserAvailable = false;
    f.open(17);
    f.invoke(PadMenuAction::exportRaw);
    f.open(17);
    check(!f.actions.dialogPending() && !f.actions.actionEnabled(f.context, PadMenuAction::exportRaw) &&
          f.actions.actionEnabled(f.context, PadMenuAction::import),
          "failed Save is remembered while Import remains enabled");
}

void clipboardCompletion()
{
    Fixture f;
    f.pads[3] = '1';
    f.open(3);
    f.invoke(PadMenuAction::copy);
    check(f.actions.busy() && !f.actions.menuOpen(), "copy owns its pending operation");
    f.parameter(kParameterPadClipboardAvailable, 1.0f);
    f.parameter(kParameterPadClipboardResultEvent,
                padClipboardResultEventValue(PadClipboardResultCode::copied, false));
    f.open(3);
    check(f.actions.actionEnabled(f.context, PadMenuAction::paste), "availability enables Paste");
    f.invoke(PadMenuAction::paste);
    f.parameter(kParameterPadClipboardResultEvent,
                padClipboardResultEventValue(PadClipboardResultCode::copied, false));
    check(f.actions.busy(), "duplicate copy output cannot finish the pending paste");
    f.parameter(kParameterPadClipboardResultEvent,
                padClipboardResultEventValue(PadClipboardResultCode::pasted, false));
    check(!f.actions.busy() && f.host.status == "Pad pasted" && f.host.refreshes == 1,
          "paste completes and refreshes its selected target");
    f.open(3);
    f.invoke(PadMenuAction::paste);
    f.parameter(kParameterPadClipboardResultEvent,
                padClipboardResultEventValue(PadClipboardResultCode::failed, true));
    f.open(3);
    check(f.host.status == "Could not paste pad" &&
          f.actions.actionEnabled(f.context, PadMenuAction::paste),
          "paste failure uses the pending action and preserves clipboard availability");
    f.actions.clearClipboard();
    check(!f.actions.actionEnabled(f.context, PadMenuAction::paste), "Clear Pads clears availability");
}

void structureReplies()
{
    Fixture f;
    f.pads.fill('1');
    f.open(3);
    check(!f.actions.actionEnabled(f.context, PadMenuAction::splitSample),
          "a full visible range awaits cross-bank availability");
    f.state("pad_structure_status", "PS1;A;4;1");
    check(!f.actions.actionEnabled(f.context, PadMenuAction::splitSample),
          "availability for another menu target is ignored");
    f.state("pad_structure_status", "PS1;A;3;1");
    check(f.actions.actionEnabled(f.context, PadMenuAction::splitSample),
          "matching cross-bank availability enables Split");
    f.actions.closeMenu();
    f.state("pad_structure_status", "PS1;A;3;1");
    f.open(3);
    check(!f.actions.actionEnabled(f.context, PadMenuAction::splitSample),
          "dismissal and reopening cannot reuse stale availability");

    SplitPlanReady plan{42U, 3U, 4U, {3U, 128U, 48000.0, {}, {}}};
    const auto encoded = encodeSplitPlanReady(plan);
    f.pads[4] = '0';
    for (int invalidation = 0; invalidation < 3; ++invalidation) {
        f.open(3);
        f.invoke(PadMenuAction::splitSample);
        check(f.actions.busy(), "split preparation owns the pending visible range");
        if (invalidation == 0) f.context.firstPad = 16;
        if (invalidation == 1) f.context.armed = true;
        if (invalidation == 2) f.actions.invalidatePendingStructure();
        f.state("pad_structure_status", encoded);
        PadStructureRequest cancelled;
        check(!f.actions.busy() && !f.chop.active() &&
              decodePadStructureRequest(f.host.messages.back().second, cancelled) &&
              cancelled.action == PadStructureAction::cancelSplit && cancelled.planId == 42U,
              "a stale plan is released rather than opening the split editor");
        f.context.firstPad = 0;
        f.context.armed = false;
    }
    f.open(3);
    f.invoke(PadMenuAction::splitSample);
    f.state("pad_structure_status", "PS1;E;99;unrelated");
    check(f.actions.busy(), "an unrelated plan error cannot terminate split preparation");
    f.state("pad_structure_status", "PS1;E;0;plan failed");
    check(!f.actions.busy() && f.host.status == "plan failed", "pending plan failure clears busy");
    f.open(3);
    f.invoke(PadMenuAction::splitSample);
    f.state("pad_structure_status", encoded);
    check(!f.actions.busy() && f.chop.active() && f.chop.split() && f.chop.splitPlanId() == 42U,
          "a matching plan hands ownership to the split editor");
}
} // namespace

int main()
{
    eligibilityAndConfirmation();
    dialogsAndFileCompletion();
    clipboardCompletion();
    structureReplies();
    std::cout << "Pad action controller tests passed\n";
}
