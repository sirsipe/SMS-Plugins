#include "PadActionController.hpp"

#include "MidichopperLayout.hpp"
#include "../plugin/Parameters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace midichopper::ui {
namespace {
using namespace midichopper::plugin;
namespace uiLayout = layout;
inline constexpr auto kClearConfirmationTimeout = std::chrono::seconds(2);
constexpr auto padMenuItemKinds() noexcept
{
    std::array<sms::ui::ContextMenuItemKind, kPadMenuEntries.size()> kinds{};
    for (std::size_t index = 0; index < kinds.size(); ++index)
        kinds[index] = kPadMenuEntries[index].kind;
    return kinds;
}
inline constexpr auto kPadMenuItemKinds = padMenuItemKinds();
} // namespace

int PadActionContext::localPad(const int pad) const noexcept
{
    return static_cast<int>(midichopper::localPadInBank(
        static_cast<std::uint32_t>(std::max(pad, 0)),
        static_cast<std::uint8_t>(visibleCount), bankMode));
}

PadActionController::PadActionController(PadActionHost& host,
    ChopEditorController& chopEditor, const int colorItemCount,
    const bool stateTransport, const bool fileBrowser) noexcept
    : fHost(host), fChopEditor(chopEditor), fColorItemCount(colorItemCount),
      fStateTransport(stateTransport), fFileBrowser(fileBrowser) {}

void PadActionController::setLocalStatus(const char* message)
{
    fHost.setPadActionStatus(message);
    fHost.repaintPadActions();
}

void PadActionController::clearPendingSplit() noexcept
{
    fPendingSplitTarget = -1;
    fPendingSplitFirst = -1;
    fPendingSplitCount = 0;
}

void PadActionController::invalidatePendingStructure() noexcept
{
    fStructureBusy = false;
    clearPendingSplit();
}

void PadActionController::idle(const Clock::time_point now)
{
    if (fClearArmed && now >= fClearDeadline) {
        fClearArmed = false;
        fHost.repaintPadActions();
    }
    if (fCollapseArmed && now >= fCollapseDeadline) {
        fCollapseArmed = false;
        fHost.repaintPadActions();
    }
}
void PadActionController::openMenu(const PadActionContext& context, const int pad, const sms::ui::Point anchor)
{
    fTarget = pad;
    fMenu = sms::ui::ContextMenuGeometry(
        anchor, std::span<const sms::ui::ContextMenuItemKind>{kPadMenuItemKinds},
        uiLayout::contentBounds);
    fColorMenuOpen = false;
    fHost.clearPadActionHover();
    fClearArmed = false;
    fCollapseArmed = false;
    fMenuOpen = true;
    fSplitAvailablePad = -1;
    fSplitAvailable = false;
    fCollapseAvailablePad = -1;
    fCollapseAvailableCount = 0;
    if (!busy() && (targetOccupied(context) ||
        context.localPad(pad) == context.visibleCount - 1)) {
        midichopper::plugin::PadStructureRequest request;
        request.action = targetOccupied(context)
            ? PadStructureAction::querySplit : PadStructureAction::queryCollapse;
        request.firstPad = static_cast<std::uint32_t>(context.firstPad);
        request.padCount = static_cast<std::uint32_t>(context.visibleCount);
        request.targetPad = static_cast<std::uint32_t>(pad);
        fHost.sendPadActionState("pad_structure_request",
            midichopper::plugin::encodePadStructureRequest(request));
    }
}

void PadActionController::closeMenu() noexcept
{
    fMenuOpen = false;
    fColorMenuOpen = false;
    fTarget = -1;
    fHost.clearPadActionHover();
    fClearArmed = false;
    fCollapseArmed = false;
}

void PadActionController::openColorMenu(const PadActionContext& context)
{
    if (fColorMenuOpen || !actionEnabled(context, PadMenuAction::color))
        return;
    fColorMenu = sms::ui::ContextMenuGeometry::submenu(
        fMenu, kPadColorMenuRow,
        static_cast<int>(fColorItemCount), uiLayout::contentBounds);
    fColorMenuOpen = true;
    fHost.repaintPadActions();
}

void PadActionController::selectColor(const PadActionContext& context, const int item)
{
    if (!fColorMenuOpen || !actionEnabled(context, PadMenuAction::color) ||
        item < 0 || item >= static_cast<int>(fColorItemCount))
        return;
    fHost.setPadColor(fTarget, item);
    closeMenu();
    fHost.repaintPadActions();
}

bool PadActionController::targetOccupied(const PadActionContext& context) const noexcept
{
    if (!fMenuOpen || fTarget < context.firstPad ||
        fTarget >= context.firstPad + context.visibleCount)
        return false;
    return localPadOccupied(context, context.localPad(fTarget));
}

bool PadActionController::exportEnabled(const PadActionContext& context) const noexcept
{
    return fSaveDialogAvailable && !busy() && targetOccupied(context);
}

bool PadActionController::busy() const noexcept
{
    return fFileBusy || fClipboardBusy || fStructureBusy;
}

bool PadActionController::cutPointsEnabled(const PadActionContext& context) const noexcept
{
    if (busy() || !targetOccupied(context))
        return false;
    const auto index = midichopper::visiblePadIndex(
        static_cast<std::uint32_t>(fTarget),
        static_cast<std::uint8_t>(context.visibleCount), context.bankMode);
    return index > 0U && index + 1U <
        static_cast<std::uint32_t>(context.visibleCount) * midichopper::kBankCount;
}

bool PadActionController::localPadOccupied(const PadActionContext& context, const int localPad) const noexcept
{
    if (localPad < 0 || localPad >= context.visibleCount)
        return false;
    const char state = context.padState[static_cast<std::size_t>(localPad)];
    return state != '0' && state != '.';
}

bool PadActionController::splitEnabled(const PadActionContext& context) const noexcept
{
    if (busy() || !targetOccupied(context))
        return false;
    const int target = context.localPad(fTarget);
    for (int pad = target + 1; pad < context.visibleCount; ++pad) {
        if (!localPadOccupied(context, pad))
            return true;
    }
    return fSplitAvailablePad == fTarget && fSplitAvailable;
}

int PadActionController::collapseShiftCount(const PadActionContext& context) const noexcept
{
    if (!fMenuOpen || fTarget < 0 ||
        targetOccupied(context))
        return 0;
    if (context.localPad(fTarget) == context.visibleCount - 1)
        return fCollapseAvailablePad == fTarget ? fCollapseAvailableCount : 0;
    int pad = context.localPad(fTarget) + 1;
    while (pad < context.visibleCount && !localPadOccupied(context, pad))
        ++pad;
    int count = 0;
    while (pad < context.visibleCount && localPadOccupied(context, pad)) {
        ++pad;
        ++count;
    }
    return count;
}

bool PadActionController::processedExportEnabled(const PadActionContext& context) const noexcept
{
    if (!exportEnabled(context))
        return false;
    if (context.editorSettingsPad != fTarget)
        return false;
    if (context.mixerSettingsPad != fTarget)
        return false;
    const auto settings = sms::dsp::sanitize(context.editorSettings);
    const auto mixer = sms::dsp::sanitize(context.mixerSettings);
    return settings.start != 0.0f || settings.end != 1.0f ||
           settings.attackSeconds != 0.0f || settings.decaySeconds != 0.0f ||
           settings.sustainLevel != 1.0f || settings.releaseSeconds != 0.0f ||
           mixer.gainDecibels != 0.0f || mixer.pan != 0.0f ||
           mixer.tuneSemitones != 0.0f || mixer.reverse != 0.0f;
}

bool PadActionController::actionEnabled(const PadActionContext& context, const PadMenuAction action) const noexcept
{
    switch (action) {
    case PadMenuAction::editSample:
        return !context.editorMode && !busy() && targetOccupied(context);
    case PadMenuAction::color:
        return fMenuOpen && fTarget >= 0 &&
               fTarget < static_cast<int>(midichopper::kPadCount);
    case PadMenuAction::copy: return !busy() && targetOccupied(context);
    case PadMenuAction::paste:
        return !busy() && fClipboardAvailable && fMenuOpen && fTarget >= 0;
    case PadMenuAction::adjustCutPoints: return cutPointsEnabled(context);
    case PadMenuAction::splitSample: return splitEnabled(context);
    case PadMenuAction::collapseGap:
        return !busy() && fMenuOpen && !targetOccupied(context) &&
               collapseShiftCount(context) > 0;
    case PadMenuAction::exportRaw: return exportEnabled(context);
    case PadMenuAction::exportProcessed: return processedExportEnabled(context);
    case PadMenuAction::import: return !busy();
    case PadMenuAction::clear:
        return !busy() && targetOccupied(context);
    case PadMenuAction::count: return false;
    }
    return false;
}

sms::ui::ContextMenuItemView PadActionController::actionView(const PadActionContext& context,
    const PadMenuEntry entry, const char* const collapseLabel) const noexcept
{
    const PadMenuAction action = entry.action;
    const bool enabled = actionEnabled(context, action);
    switch (action) {
    case PadMenuAction::editSample:
        return {"EDIT SAMPLE", enabled, false};
    case PadMenuAction::color:
        return {"COLOR", enabled, false,
                sms::ui::ContextMenuItemKind::action, false, -1, true};
    case PadMenuAction::adjustCutPoints:
        return {"ADJUST CUT POINTS", enabled, false};
    case PadMenuAction::splitSample:
        return {"SPLIT SAMPLE...", enabled, false};
    case PadMenuAction::collapseGap:
        return {fCollapseArmed ? "CONFIRM COLLAPSE" : collapseLabel,
                enabled, false};
    case PadMenuAction::exportRaw:
        return {"EXPORT WAV...", enabled, false};
    case PadMenuAction::exportProcessed:
        return {"EXPORT PROCESSED...", enabled, false};
    case PadMenuAction::import:
        return {"IMPORT WAV...", enabled, false};
    case PadMenuAction::copy:
        return {"COPY PAD", enabled, false};
    case PadMenuAction::paste:
        return {"PASTE PAD", enabled, false};
    case PadMenuAction::clear:
        return {fClearArmed ? "CONFIRM CLEAR" : "CLEAR PAD",
                enabled, true};
    case PadMenuAction::count:
        return {};
    }
    return {};
}

void PadActionController::invoke(const PadActionContext& context, const PadMenuEntry entry, const Clock::time_point now)
{
    const PadMenuAction action = entry.action;
    if (!actionEnabled(context, action))
        return;
    switch (action) {
    case PadMenuAction::editSample: {
        const int pad = fTarget;
        closeMenu();
        fHost.beginPadSampleEditor(pad);
        return;
    }
    case PadMenuAction::color:
        openColorMenu(context);
        return;
    case PadMenuAction::copy:
        requestClipboard(PadClipboardAction::copy);
        return;
    case PadMenuAction::paste:
        requestClipboard(PadClipboardAction::paste);
        return;
    case PadMenuAction::adjustCutPoints: {
        const int pad = fTarget;
        closeMenu();
        fChopEditor.open(pad, context.firstPad, context.visibleCount, context.bankMode);
        return;
    }
    case PadMenuAction::splitSample: {
        const int pad = fTarget;
        closeMenu();
        requestSplitPlan(context, pad);
        return;
    }
    case PadMenuAction::collapseGap:
        if (!fCollapseArmed) {
            fCollapseArmed = true;
            fCollapseDeadline = now + kClearConfirmationTimeout;
            setLocalStatus("Click COLLAPSE GAP again to confirm");
        } else {
            requestCollapseGap(context, fTarget);
            closeMenu();
        }
        fHost.repaintPadActions();
        return;
    case PadMenuAction::exportRaw:
        openFileDialog(context, PadFileDialog::exportRaw);
        return;
    case PadMenuAction::exportProcessed:
        openFileDialog(context, PadFileDialog::exportProcessed);
        return;
    case PadMenuAction::import:
        openFileDialog(context, PadFileDialog::import);
        return;
    case PadMenuAction::clear:
        if (!fClearArmed) {
            fClearArmed = true;
            fClearDeadline = now + kClearConfirmationTimeout;
            setLocalStatus("Click CLEAR PAD again to confirm");
        } else {
            requestClear();
            closeMenu();
            setLocalStatus("Pad cleared");
        }
        fHost.repaintPadActions();
        return;
    case PadMenuAction::count:
        return;
    }
}

void PadActionController::requestSplitPlan(const PadActionContext& context, const int pad)
{
    if (!fStateTransport)
        return;
    midichopper::plugin::PadStructureRequest request;
    request.action = PadStructureAction::prepareSplit;
    request.firstPad = static_cast<std::uint32_t>(context.firstPad);
    request.padCount = static_cast<std::uint32_t>(context.visibleCount);
    request.targetPad = static_cast<std::uint32_t>(pad);
    fPendingSplitTarget = pad;
    fPendingSplitFirst = static_cast<int>(request.firstPad);
    fPendingSplitCount = static_cast<int>(request.padCount);
    fStructureBusy = true;
    setLocalStatus("Preparing sample split...");
    const auto encoded = encodePadStructureRequest(request);
    fHost.sendPadActionState("pad_structure_request", encoded);
}

void PadActionController::requestCollapseGap(const PadActionContext& context, const int pad)
{
    if (!fStateTransport)
        return;
    midichopper::plugin::PadStructureRequest request;
    request.action = PadStructureAction::collapse;
    request.firstPad = static_cast<std::uint32_t>(context.firstPad);
    request.padCount = static_cast<std::uint32_t>(context.visibleCount);
    request.targetPad = static_cast<std::uint32_t>(pad);
    clearPendingSplit();
    fStructureBusy = true;
    setLocalStatus("Collapsing pad gap...");
    const auto encoded = encodePadStructureRequest(request);
    fHost.sendPadActionState("pad_structure_request", encoded);
}

void PadActionController::openFileDialog(const PadActionContext& context, const PadFileDialog action)
{
    if (!fFileBrowser || fTarget < 0 || busy())
        return;
    const bool saving = action == PadFileDialog::exportRaw ||
                        action == PadFileDialog::exportProcessed;
    char defaultName[40];
    std::snprintf(defaultName, sizeof(defaultName), "anvillesampler-pad-%02d.wav",
                  context.localPad(fTarget) + 1);
    fPendingDialog = action;
    fPendingFilePad = fTarget;
    closeMenu();
    if (!fHost.showPadFileBrowser(saving, saving ? defaultName : nullptr)) {
        fPendingDialog = PadFileDialog::none;
        fPendingFilePad = -1;
        if (saving) {
            fSaveDialogAvailable = false;
            setLocalStatus("Save dialog unavailable; Linux Export requires a desktop portal");
        } else {
            setLocalStatus("Open dialog unavailable");
        }
    }
    fHost.repaintPadActions();
}

void PadActionController::requestClipboard(const PadClipboardAction action)
{
    if (!fStateTransport)
        return;
    if (fTarget < 0 || busy())
        return;
    const int pad = fTarget;
    fClipboardBusy = true;
    fActiveClipboardAction = action;
    fActiveClipboardPad = pad;
    closeMenu();
    setLocalStatus(action == PadClipboardAction::copy
        ? "Copying pad..." : "Pasting pad...");
    const std::string request = midichopper::plugin::encodePadClipboardRequest(
        action, static_cast<std::uint32_t>(pad));
    fHost.sendPadActionState("pad_clipboard_request", request);
    fHost.repaintPadActions();
}

bool PadActionController::hasWavFileExtension(const std::string_view path) noexcept
{
    if (path.size() < 4U)
        return false;
    const auto extension = path.substr(path.size() - 4U);
    return extension[0] == '.' && (extension[1] == 'w' || extension[1] == 'W') &&
           (extension[2] == 'a' || extension[2] == 'A') &&
           (extension[3] == 'v' || extension[3] == 'V');
}

bool PadActionController::hasAnyFileExtension(const std::string_view path) noexcept
{
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    return dot != std::string_view::npos && dot + 1U < path.size() &&
           (slash == std::string_view::npos || dot > slash + 1U);
}

void PadActionController::requestClear()
{
    if (!fStateTransport)
        return;
    if (fTarget < 0 || fTarget >= static_cast<int>(midichopper::kPadCount))
        return;
    char pad[4];
    std::snprintf(pad, sizeof(pad), "%u",
                  static_cast<unsigned int>(fTarget + 1));
    fHost.sendPadActionState("pad_clear_request", pad);
}

bool PadActionController::acceptParameter(const PadActionContext& context,
                                          const std::uint32_t index, const float value)
{
    if (index == kParameterPadFileResultEvent)
    {
        const auto result = fFileResultEvents.consume(value);
        if (result == midichopper::plugin::PadFileResultCode::none)
            return true;
        const bool wasImport = fActiveFileAction == PadFileDialog::import;
        const int completedPad = fActiveFilePad;
        if (fActiveFileAction != PadFileDialog::none)
        {
            if (result == midichopper::plugin::PadFileResultCode::failed)
                fHost.setPadActionStatus("WAV action failed");
            else if (result == midichopper::plugin::PadFileResultCode::importSucceeded)
                fHost.setPadActionStatus("WAV imported");
            else if (result == midichopper::plugin::PadFileResultCode::processedExportSucceeded)
                fHost.setPadActionStatus("Processed WAV exported");
            else
                fHost.setPadActionStatus("WAV exported");
        }
        fFileBusy = false;
        fActiveFileAction = PadFileDialog::none;
        fActiveFilePad = -1;
        if (wasImport && result == midichopper::plugin::PadFileResultCode::importSucceeded &&
            completedPad == context.selectedPad) {
            fHost.refreshPadWaveform();
        }
        fHost.repaintPadActions();
        return true;
    }
    if (index == kParameterPadClipboardAvailable)
    {
        const bool available = value >= 0.5f;
        if (available != fClipboardAvailable) {
            fClipboardAvailable = available;
            fHost.repaintPadActions();
        }
        return true;
    }
    if (index == kParameterPadClipboardResultEvent)
    {
        const auto result = fClipboardResultEvents.consume(value);
        if (result == midichopper::plugin::PadClipboardResultCode::none)
            return true;
        const int completedPad = fActiveClipboardPad;
        const auto completedAction = fActiveClipboardAction;
        if (result == midichopper::plugin::PadClipboardResultCode::copied)
            fHost.setPadActionStatus("Pad copied");
        else if (result == midichopper::plugin::PadClipboardResultCode::pasted)
            fHost.setPadActionStatus("Pad pasted");
        else
            fHost.setPadActionStatus(completedAction == PadClipboardAction::paste
                ? "Could not paste pad" : "Could not copy pad");
        fClipboardBusy = false;
        fActiveClipboardAction = PadClipboardAction::copy;
        fActiveClipboardPad = -1;
        if (result == midichopper::plugin::PadClipboardResultCode::pasted &&
            completedPad == context.selectedPad) {
            fHost.refreshPadWaveform();
        }
        fHost.repaintPadActions();
        return true;
    }
    return false;
}

bool PadActionController::acceptState(const PadActionContext& context,
                                      const std::string_view key, const char* value)
{
    if (key == "pad_structure_status")
    {
        midichopper::plugin::SplitPlanReady plan;
        if (std::string_view(value).starts_with("PS1;G;")) {
            const auto reply = std::string_view(value).substr(6U);
            const auto separator = reply.find(';');
            int pad = -1;
            int count = 0;
            if (separator != std::string_view::npos &&
                midichopper::plugin::parsePadStructureInteger(reply.substr(0U, separator), pad) &&
                midichopper::plugin::parsePadStructureInteger(reply.substr(separator + 1U), count) &&
                count >= 0 && count <= context.visibleCount &&
                fMenuOpen && pad == fTarget) {
                fCollapseAvailablePad = pad;
                fCollapseAvailableCount = count;
                fHost.repaintPadActions();
            }
        } else if (std::string_view(value).starts_with("PS1;A;")) {
            const auto reply = std::string_view(value).substr(6U);
            const auto separator = reply.find(';');
            int pad = -1;
            if (separator != std::string_view::npos &&
                midichopper::plugin::parsePadStructureInteger(reply.substr(0U, separator), pad) &&
                fMenuOpen && pad == fTarget &&
                (reply.substr(separator + 1U) == "0" || reply.substr(separator + 1U) == "1")) {
                fSplitAvailablePad = pad;
                fSplitAvailable = reply.substr(separator + 1U) == "1";
                fHost.repaintPadActions();
            }
        } else if (midichopper::plugin::decodeSplitPlanReady(value, plan)) {
            const bool expected = fStructureBusy && fPendingSplitTarget >= 0 &&
                plan.targetPad == static_cast<std::uint32_t>(fPendingSplitTarget) &&
                !context.armed && context.firstPad == fPendingSplitFirst &&
                context.visibleCount == fPendingSplitCount;
            fStructureBusy = false;
            clearPendingSplit();
            if (expected) {
                fChopEditor.openSplit(plan, static_cast<std::uint8_t>(context.visibleCount), context.bankMode);
            } else {
                fChopEditor.cancelPlan(plan.planId);
                fHost.setPadActionStatus("Split cancelled because pad context changed");
            }
        } else if (std::strcmp(value, "PS1;O;0;C") == 0) {
            fStructureBusy = false;
            fHost.setPadActionStatus("Pad gap collapsed");
            if (context.editorMode)
                fHost.refreshPadWaveform();
        } else if (fChopEditor.acceptSplitStatus(value, context.firstPad, context.visibleCount, context.bankMode)) {
            clearPendingSplit();
        } else if (std::strncmp(value, "PS1;E;", 6U) == 0) {
            char* idEnd = nullptr;
            const auto errorPlanId = std::strtoull(value + 6U, &idEnd, 10);
            if (idEnd == value + 6U || idEnd == nullptr || *idEnd != ';')
                return true;
            const bool pendingError = errorPlanId == 0U && fStructureBusy;
            if (!pendingError)
                return true;
            fStructureBusy = false;
            clearPendingSplit();
            fHost.setPadActionStatus(idEnd + 1U);
        }
    } else if (key == "pad_file_busy")
        fFileBusy = value[0] != '0' && value[0] != '\0';
    else if (key == "pad_file_status")
    {
        fFileBusy = false;
        fHost.setPadActionStatus(value);
        if (fActiveFileAction == PadFileDialog::import &&
            fActiveFilePad == context.selectedPad && std::strcmp(value, "WAV imported") == 0) {
            fHost.refreshPadWaveform();
        }
        fActiveFileAction = PadFileDialog::none;
        fActiveFilePad = -1;
    } else
        return false;
    return true;
}

void PadActionController::fileSelected(const char* const filename)
{
    const PadFileDialog action = fPendingDialog;
    const int pad = fPendingFilePad;
    fPendingDialog = PadFileDialog::none;
    fPendingFilePad = -1;
    if (filename == nullptr || filename[0] == '\0') {
        fHost.repaintPadActions();
        return;
    }

    std::string path(filename);
    const bool exporting = action == PadFileDialog::exportRaw ||
                           action == PadFileDialog::exportProcessed;
    if (exporting) {
        if (!hasAnyFileExtension(path))
            path += ".wav";
        else if (!hasWavFileExtension(path)) {
            setLocalStatus("Export filename must use the .wav extension");
            return;
        }
    }
    if (pad < 0 || pad >= static_cast<int>(midichopper::kPadCount) ||
        action == PadFileDialog::none)
        return;

    midichopper::plugin::PadFileAction fileAction =
        midichopper::plugin::PadFileAction::import;
    if (action == PadFileDialog::exportRaw)
        fileAction = midichopper::plugin::PadFileAction::exportRaw;
    else if (action == PadFileDialog::exportProcessed)
        fileAction = midichopper::plugin::PadFileAction::exportProcessed;

    fFileBusy = true;
    fActiveFileAction = action;
    fActiveFilePad = pad;
    setLocalStatus(exporting ? "Exporting WAV..." : "Importing WAV...");
    if (fStateTransport) {
        const std::string request = midichopper::plugin::encodePadFileRequest(
            fileAction, static_cast<std::uint32_t>(pad), path);
        fHost.sendPadActionState("pad_file_request", request);
    }
    fHost.repaintPadActions();
}

} // namespace midichopper::ui
