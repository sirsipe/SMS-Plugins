/*
 * SMS-Anvil Sampler - DPF/NanoVG user interface
 *
 * Parameter indices and ranges are shared with the DSP through Parameters.hpp.
 * Drawing is delegated to MidichopperView and reusable Common-UI components;
 * this class owns only host communication and interaction state.
 *
 * State contract used by the sample editor:
 *   pad_edit_01..64  compact non-destructive cut-point and ADSR settings
 *   pad_mix_01..64   independent gain, pan, and tune settings
 *   pad_color_01..64 persistent one-based theme tint indices (zero is none)
 *   waveform_request selected pad index sent from UI to DSP
 *   waveform_data    compact 128-bin min/max summary returned by DSP
 *   pad_clear_request one-based pad command consumed at an audio block boundary
 *   pad_file_request action, pad, and UTF-8 path sent from UI to DSP
 *   pad_file_busy/status small progress and result messages returned to the UI
 *   pad_clipboard_request internal per-instance Copy or Paste command
 *   chop_apply_request/status transactional rolling-boundary edit and result
 *   chop_preview_request raw allocation-free play/stop command
 *   chop_midi_preview exclusive editor MIDI audition map
 *   pad_structure_request/status atomic gap collapse and planned sample split
 *
 * The UI sends C1 + pad as MIDI note-on/off in PLAY mode only. In ARM mode a
 * clicked pad selects the first destination and the next incoming MIDI note is
 * expected to define each sequential chop boundary.
 */

// Ensure DPF exposes its NanoVG UI type in every UI translation unit.
#ifndef DISTRHO_UI_USE_NANOVG
# define DISTRHO_UI_USE_NANOVG 1
#endif
#include "DistrhoUI.hpp"

#include "Audio/WaveformSummary.hpp"
#include "ChopEditor.hpp"
#include "ChopEditorController.hpp"
#include "EditorSnapshotSession.hpp"
#include "PlaybackIndicator.hpp"
#include "MixerValueEntry.hpp"
#include "WaveformDetailSession.hpp"
#include "ChopEditorProtocol.hpp"
#include "Configuration.hpp"
#include "ContextMenu.hpp"
#include "DPF/NanoUI.hpp"
#include "DPF/Theme.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "LevelMeter.hpp"
#include "State/SampleMixerSettingsCodec.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"
#include "UI/Geometry.hpp"
#include "Interaction.hpp"
#include "PadLayout.hpp"
#include "WaveformEditor.hpp"
#include "WaveformViewport.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "MidichopperView.hpp"
#include "HelpLinks.hpp"
#include "WaveformDetailProtocol.hpp"
#include "PadClipboardProtocol.hpp"
#include "PadColorState.hpp"
#include "PadFileActionProtocol.hpp"
#include "PadStructureProtocol.hpp"
#include "Parameters.hpp"
#include "CaptureActionProtocol.hpp"
#include "PluginUiBridge.hpp"
#include "anvilsampler_artwork.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#if defined(__linux__)
# include <cerrno>
# include <spawn.h>
# include <sys/wait.h>
extern char** environ;
#endif

START_NAMESPACE_DISTRHO

namespace {

using namespace midichopper::plugin;
namespace uiLayout = midichopper::ui::layout;
using WaveformEditTarget = sms::ui::waveform::EditTarget;

inline constexpr auto kClearConfirmationTimeout = std::chrono::seconds(2);
inline constexpr auto kMeterFrameInterval = std::chrono::milliseconds(33);
inline constexpr auto kEditorSnapshotRetryInterval = std::chrono::milliseconds(250);

[[nodiscard]] bool openBrowser(const char* const url)
{
#if defined(__linux__)
    // The shell only starts xdg-open in the background; wait for the short-lived
    // shell so the host never inherits an unreaped child. The URL is passed as
    // a positional argument and is never parsed as shell code.
    char* const args[] = {
        const_cast<char*>("sh"), const_cast<char*>("-c"),
        const_cast<char*>("command -v xdg-open >/dev/null 2>&1 || exit 127; "
                          "xdg-open \"$1\" >/dev/null 2>&1 &"),
        const_cast<char*>("sh"), const_cast<char*>(url), nullptr};
    pid_t child = 0;
    if (posix_spawnp(&child, "sh", nullptr, nullptr, args, environ) != 0)
        return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)url;
    return false;
#endif
}

[[nodiscard]] midichopper::ui::KnobAdjustment knobAdjustment(
    const uint modifiers) noexcept
{
    if ((modifiers & DGL_NAMESPACE::kModifierControl) != 0U)
        return midichopper::ui::KnobAdjustment::stepped;
    if ((modifiers & DGL_NAMESPACE::kModifierShift) != 0U)
        return midichopper::ui::KnobAdjustment::fine;
    return midichopper::ui::KnobAdjustment::normal;
}

enum class PadMenuAction : int {
    editSample = 0,
    color,
    adjustCutPoints,
    splitSample,
    collapseGap,
    exportRaw,
    exportProcessed,
    import,
    copy,
    paste,
    clear,
    count,
};

struct PadMenuEntry {
    PadMenuAction action = PadMenuAction::count;
    sms::ui::ContextMenuItemKind kind = sms::ui::ContextMenuItemKind::action;
};

inline constexpr std::array kPadMenuEntries{
    PadMenuEntry{PadMenuAction::editSample},
    PadMenuEntry{PadMenuAction::color},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::adjustCutPoints},
    PadMenuEntry{PadMenuAction::splitSample},
    PadMenuEntry{PadMenuAction::collapseGap},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::exportRaw},
    PadMenuEntry{PadMenuAction::exportProcessed},
    PadMenuEntry{PadMenuAction::import},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::copy},
    PadMenuEntry{PadMenuAction::paste},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::clear},
};
inline constexpr int kPadColorMenuRow = 1;
inline constexpr std::size_t kPadColorMenuItemCount =
    sms::ui::dpf::Theme::padColorCount + 1U;

constexpr auto padMenuItemKinds() noexcept
{
    std::array<sms::ui::ContextMenuItemKind, kPadMenuEntries.size()> kinds{};
    for (std::size_t index = 0; index < kinds.size(); ++index)
        kinds[index] = kPadMenuEntries[index].kind;
    return kinds;
}

inline constexpr auto kPadMenuItemKinds = padMenuItemKinds();

enum class PendingFileDialog : std::uint8_t {
    none,
    exportRaw,
    exportProcessed,
    import,
};

std::array<std::string, midichopper::kPadCount> makePadStateKeys(const char* const prefix)
{
    std::array<std::string, midichopper::kPadCount> keys;
    for (uint pad = 0; pad < midichopper::kPadCount; ++pad) {
        char key[24];
        std::snprintf(key, sizeof(key), "%s%02u", prefix, pad + 1U);
        keys[pad] = key;
    }
    return keys;
}

const auto kPadEditStateKeys = makePadStateKeys("pad_edit_");
const auto kPadMixerStateKeys = makePadStateKeys("pad_mix_");
const auto kPadColorStateKeys = makePadStateKeys("pad_color_");

} // namespace

class MidichopperUI final : public sms::ui::dpf::NanoUI,
                            private midichopper::ui::ChopEditorHost
{
public:
    MidichopperUI()
        : NanoUI(midichopper::ui::layout::canvasWidth,
                 midichopper::ui::layout::canvasHeight,
                 midichopper::ui::layout::minimumWidth,
                 midichopper::ui::layout::minimumHeight),
          fArm(false),
          fRecordMode(0.0f),
          fFixedLength(parameterRanges::fixedLengthSeconds.defaultValue),
          fPlaybackMode(0.0f),
          fMonitor(parameterRanges::inputMonitor.defaultValue),
          fStartPad(0),
          fPreRoll(0.0f),
          fBaseNote(static_cast<int>(parameterRanges::baseMidiNote.defaultValue)),
          fMidiBankMode(static_cast<int>(midichopper::kDefaultMidiBankMode)),
          fGain(0.0f),
          fGlobalPan(0.0f),
          fGlobalTune(0.0f),
          fMaxVoices(static_cast<int>(parameterRanges::maxVoices.defaultValue)),
          fBank(0),
          fLayout(0),
          fSelectedPad(-1),
          fLastPlayedPad(-1),
          fCurrentPad(-1),
          fClearArmed(false),
          fMenuOpen(false),
          fPadContextMenuOpen(false),
          fPadContextTarget(-1),
          fPadContextClearArmed(false),
          fPadContextPointerCaptured(false),
          fEditorMode(false),
          fPlayOnSelect(true),
          fDragTarget(WaveformEditTarget::none),
          fMixerDragIndex(-1),
          fGlobalMixerDragIndex(-1),
          fDragStartX(0.0f),
          fDragStartY(0.0f),
          fHasWaveform(false),
          fCaptureTargetRequestAlternateHalf(false)
    {
        fPadState.fill('0');
        fPadStatus.fill('0');
        fPadColors.fill(0);
        fStatus[0] = '\0';

#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (void* const instance = getPluginInstancePointer()) {
            fUiBridge = static_cast<PluginUiBridge*>(static_cast<Plugin*>(instance));
            fUiMessageCursor = fUiBridge->uiMessageCursor();
        }
#endif

#ifndef DGL_NO_SHARED_RESOURCES
        loadSharedResources();
#endif
    }

    ~MidichopperUI() override
    {
        if (!fChopEditor.active())
            return;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fUiBridge != nullptr) {
            fUiBridge->stopUiPreview();
            return;
        }
#endif
        fChopEditor.disableMidi();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParameterInputLevelLeft && index <= kParameterOutputLevelRight)
        {
            auto& levels = index <= kParameterInputLevelRight
                ? fInputLevels : fOutputLevels;
            const std::size_t channel = static_cast<std::size_t>(
                (index - kParameterInputLevelLeft) % 2U);
            const float level = std::isfinite(value)
                ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
            const bool visibleChange =
                sms::ui::meter::visibleLevelChanged(levels[channel], level);
            levels[channel] = level;
            if (visibleChange)
                fMeterRepaintPending = true;
            return;
        }
        if (index == kParameterPadFileResultEvent)
        {
            const auto result = fPadFileResultEvents.consume(value);
            if (result == midichopper::plugin::PadFileResultCode::none)
                return;
            const bool wasImport = fActiveFileAction == PendingFileDialog::import;
            const int completedPad = fActiveFilePad;
            if (fActiveFileAction != PendingFileDialog::none)
            {
                if (result == midichopper::plugin::PadFileResultCode::failed)
                    copyString(fStatus, "WAV action failed");
                else if (result == midichopper::plugin::PadFileResultCode::importSucceeded)
                    copyString(fStatus, "WAV imported");
                else if (result == midichopper::plugin::PadFileResultCode::processedExportSucceeded)
                    copyString(fStatus, "Processed WAV exported");
                else
                    copyString(fStatus, "WAV exported");
            }
            fPadFileBusy = false;
            fActiveFileAction = PendingFileDialog::none;
            fActiveFilePad = -1;
            if (wasImport && result == midichopper::plugin::PadFileResultCode::importSucceeded &&
                completedPad == fSelectedPad) {
                refreshSelectedWaveform();
            }
            requestRepaint();
            return;
        }
        if (index == kParameterPadClipboardAvailable)
        {
            const bool available = value >= 0.5f;
            if (available != fPadClipboardAvailable) {
                fPadClipboardAvailable = available;
                requestRepaint();
            }
            return;
        }
        if (index == kParameterPadClipboardResultEvent)
        {
            const auto result = fPadClipboardResultEvents.consume(value);
            if (result == midichopper::plugin::PadClipboardResultCode::none)
                return;
            const int completedPad = fActivePadClipboardPad;
            const auto completedAction = fActivePadClipboardAction;
            if (result == midichopper::plugin::PadClipboardResultCode::copied)
                copyString(fStatus, "Pad copied");
            else if (result == midichopper::plugin::PadClipboardResultCode::pasted)
                copyString(fStatus, "Pad pasted");
            else
                copyString(fStatus, completedAction == PadClipboardAction::paste
                    ? "Could not paste pad" : "Could not copy pad");
            fPadClipboardBusy = false;
            fActivePadClipboardAction = PadClipboardAction::copy;
            fActivePadClipboardPad = -1;
            if (result == midichopper::plugin::PadClipboardResultCode::pasted &&
                completedPad == fSelectedPad) {
                refreshSelectedWaveform();
            }
            requestRepaint();
            return;
        }
        if (index == kParameterChopPreviewPosition)
        {
            fChopEditor.acceptPreviewPosition(value);
            requestRepaint();
            return;
        }
        if (index == kParameterPlaybackPosition)
        {
            fPlaybackIndicator.acceptHostPosition(value, std::chrono::steady_clock::now());
            if (fEditorMode)
                requestRepaint();
            return;
        }
        if (index == kParameterAnyPlaybackActive) {
            const bool active = value >= 0.5f;
            if (fAnyPlaybackActive != active) {
                fAnyPlaybackActive = active;
                requestRepaint();
            }
            return;
        }
        if (index >= kFirstPadStatusParameter && index < kFirstPadActivityParameter)
        {
            const auto localPad = static_cast<std::size_t>(index - kFirstPadStatusParameter);
            const int pad = globalPad(static_cast<int>(localPad));
            const bool wasOccupied = fPadState[localPad] != '0';
            const bool isOccupied = value >= 0.5f;
            if (wasOccupied == isOccupied)
                return;
            fPadState[localPad] = isOccupied ? '1' : '0';
            if (pad == fPadContextTarget)
                closePadContextMenu();
            if (pad == fSelectedPad)
                refreshSelectedWaveform();
            requestRepaint();
            return;
        }
        if (index >= kFirstPadActivityParameter && index < kParameterMaxVoices)
        {
            const int localPad = static_cast<int>(index - kFirstPadActivityParameter);
            const bool wasActive = fPadStatus[static_cast<std::size_t>(localPad)] != '0';
            const bool isActive = value >= 0.5f;
            if (wasActive == isActive)
                return;
            fPadStatus[static_cast<std::size_t>(localPad)] = isActive ? '1' : '0';
            if (isActive && !fArm)
                startLocalPlayhead(globalPad(localPad));
            else if (!isActive && globalPad(localPad) == fPlaybackIndicator.pad())
                stopLocalPlayhead(true);
            requestRepaint();
            return;
        }
        bool changed = true;
        switch (index)
        {
        case kParameterMode: {
            const bool armed = value >= 0.5f;
            changed = fArm != armed;
            if (!changed)
                break;
            closePadContextMenu();
            fMixerValueEntry.cancel();
            if (fGlobalMixerDragIndex == 5)
                editParameter(kParameterGlobalFilterSlope, false);
            fGlobalMixerDragIndex = -1;
            if (armed && fChopEditor.active())
                fChopEditor.cancel();
            if (armed) {
                fPendingSplitTarget = -1;
                fPendingSplitFirst = -1;
                fPendingSplitCount = 0;
                fPadStructureBusy = false;
            }
            fArm = armed;
            fStatus[0] = '\0';
            fSelectedPad = -1;
            fLastPlayedPad = -1;
            if (fArm) {
                fEditorMode = false;
                fChopEditor.stopPreview();
                stopLocalPlayhead(false);
                fDragTarget = WaveformEditTarget::none;
                fMixerDragIndex = -1;
                selectAutomaticArmTarget();
            }
            break;
        }
        case kParameterCaptureMode:
            changed = fRecordMode != value;
            fRecordMode = value;
            break;
        case kParameterFixedLengthSeconds:
            changed = fFixedLength != value;
            fFixedLength = value;
            break;
        case kParameterPlaybackMode:
            changed = fPlaybackMode != value;
            fPlaybackMode = value;
            break;
        case kParameterInputMonitor:
            changed = fMonitor != value;
            fMonitor = value;
            break;
        case kParameterStartPad: {
            const int previousStartPad = fStartPad;
            const int previousSelectedPad = fSelectedPad;
            fStartPad = clampLocalPad(value - parameterRanges::startPad.minimum);
            if (fArm && fCurrentPad < 0)
                fSelectedPad = globalPad(fStartPad);
            changed = previousStartPad != fStartPad || previousSelectedPad != fSelectedPad;
            break;
        }
        case kParameterPreRollMs:
            changed = fPreRoll != value;
            fPreRoll = value;
            break;
        case kParameterBaseMidiNote: {
            const int baseNote = clampBaseNote(value);
            changed = fBaseNote != baseNote;
            fBaseNote = baseNote;
            break;
        }
        case kParameterMidiBankMode: {
            const int localPad = hasSelectedPad() ? localPadForGlobalPad(fSelectedPad) : -1;
            const int mode = value >= 0.5f ? 1 : 0;
            changed = fMidiBankMode != mode;
            fMidiBankMode = mode;
            if (changed) {
                closePadContextMenu();
                if (fChopEditor.active())
                    fChopEditor.cancel();
                else if (fEditorMode && localPad >= 0)
                    selectEditorPad(globalPad(std::clamp(localPad, 0, visiblePadCount() - 1)));
                else {
                    fSelectedPad = -1;
                    if (!fArm)
                        fLastPlayedPad = -1;
                }
            }
            break;
        }
        case kParameterOutputGainDb:
            changed = fGain != value;
            fGain = value;
            break;
        case kParameterMonitorGainDb:
            changed = fMonitorGain != value;
            fMonitorGain = value;
            break;
        case kParameterGlobalPan:
            changed = fGlobalPan != value;
            fGlobalPan = value;
            break;
        case kParameterGlobalTuneSemitones:
            changed = fGlobalTune != value;
            fGlobalTune = value;
            break;
        case kParameterGlobalLowpass:
            changed = fGlobalLowpass != value;
            fGlobalLowpass = value;
            break;
        case kParameterGlobalHighpass:
            changed = fGlobalHighpass != value;
            fGlobalHighpass = value;
            break;
        case kParameterGlobalFilterSlope:
            changed = fGlobalFilterSlope != value;
            fGlobalFilterSlope = value;
            break;
        case kParameterGlobalDirty:
            changed = fGlobalDirty != value;
            fGlobalDirty = value;
            break;
        case kParameterMaxVoices: {
            const int maxVoices = std::clamp(static_cast<int>(std::lround(value)),
                static_cast<int>(parameterRanges::maxVoices.minimum),
                static_cast<int>(parameterRanges::maxVoices.maximum));
            changed = fMaxVoices != maxVoices;
            fMaxVoices = maxVoices;
            break;
        }
        case kParameterActiveBank: {
            const int bank = std::clamp(static_cast<int>(std::lround(value)) - 1, 0,
                                        static_cast<int>(parameterRanges::activeBank.maximum - 1.0f));
            changed = fBank != bank;
            if (!changed)
                break;
            closePadContextMenu();
            const int localPad = hasSelectedPad()
                ? std::clamp(localPadForGlobalPad(fSelectedPad), 0, visiblePadCount() - 1)
                : 0;
            fBank = bank;
            if (fChopEditor.active())
                fChopEditor.cancel();
            else if (fEditorMode)
                selectEditorPad(globalPad(localPad));
            else {
                fSelectedPad = -1;
                if (!fArm)
                    fLastPlayedPad = -1;
            }
            break;
        }
        case kParameterPadLayout: {
            const int layout = std::clamp(static_cast<int>(std::lround(value)),
                static_cast<int>(parameterRanges::padLayout.minimum),
                static_cast<int>(parameterRanges::padLayout.maximum));
            changed = fLayout != layout;
            if (!changed)
                break;
            closePadContextMenu();
            fLayout = layout;
            if (fStartPad >= visiblePadCount())
                fStartPad = 0;
            if (!fArm && !fEditorMode && !fChopEditor.active())
                fLastPlayedPad = -1;
            if (fChopEditor.active())
                fChopEditor.cancel();
            normalizeSelectionForContext();
            break;
        }
        case kParameterCurrentCapturePad: {
            const int pad = static_cast<int>(std::lround(value)) - 1;
            const int currentPad = pad >= 0 && pad < static_cast<int>(midichopper::kPadCount) ? pad : -1;
            changed = fCurrentPad != currentPad;
            if (!changed)
                break;
            fCurrentPad = currentPad;
            if (fArm && fCurrentPad >= 0) {
                const int bank = bankForGlobalPad(fCurrentPad);
                if (bank >= 0 && bank < static_cast<int>(midichopper::kBankCount)) {
                    const int localPad = localPadForGlobalPad(fCurrentPad);
                    changed = changed || fBank != bank || fSelectedPad != fCurrentPad ||
                              fStartPad != localPad;
                    fBank = bank;
                    fSelectedPad = fCurrentPad;
                    fStartPad = localPad;
                }
            }
            break;
        }
        case kParameterPlaybackPadEvent: {
            const std::uint32_t pad = fPlaybackPadEvents.consume(value);
            // MIDI performance remains audible in the Cut Point Editor, but it must not
            // silently switch the editor bank or discard a pending boundary plan.
            changed = !fArm && !fChopEditor.active() && pad < midichopper::kPadCount;
            if (!changed)
                break;
            closePadContextMenu();
            fLastPlayedPad = static_cast<int>(pad);
            fBank = bankForGlobalPad(fLastPlayedPad);
            if (fEditorMode)
                selectEditorPad(fLastPlayedPad);
            else {
                if (fSelectedPad != fLastPlayedPad) {
                    fEditorSettings = {};
                    fEditorSettingsPad = -1;
                    fMixerSettings = {};
                    fMixerSettingsPad = -1;
                }
                fSelectedPad = fLastPlayedPad;
                refreshSelectedWaveform();
            }
            startLocalPlayhead(fLastPlayedPad);
            break;
        }
        case kParameterCaptureTargetPad: {
            if (!fArm) {
                changed = false;
                break;
            }
            int target = -1;
            if (std::isfinite(value) && value >= 0.5f &&
                value <= static_cast<float>(midichopper::kPadCount) + 0.5f) {
                const int pad = static_cast<int>(std::lround(value)) - 1;
                if (pad >= 0 && pad < static_cast<int>(midichopper::kPadCount))
                    target = pad;
            }
            changed = fSelectedPad != target;
            fSelectedPad = target;
            if (target >= 0) {
                const int bank = bankForGlobalPad(target);
                const int localPad = localPadForGlobalPad(target);
                changed = changed || fBank != bank || fStartPad != localPad;
                fBank = bank;
                fStartPad = localPad;
            }
            break;
        }
        default: return;
        }
        if (changed)
            requestRepaint();
    }

#if DISTRHO_PLUGIN_WANT_STATE
    void stateChanged(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr)
            return;

        if (std::strcmp(key, "pad_mask") == 0) {
            parsePadMask(value, fPadState);
            closePadContextMenu();
        }
        else if (std::strcmp(key, "pad_status") == 0)
            copyChars(fPadStatus, value);
        else if (std::strcmp(key, "current_pad") == 0)
        {
            fCurrentPad = parsePad(value, -1);
            if (fCurrentPad >= 0)
                fBank = bankForGlobalPad(fCurrentPad);
        }
        else if (std::strcmp(key, "selected_pad") == 0)
        {
            const int selectedPad = clampPad(std::strtof(value, nullptr));
            const bool selectionChanged = selectedPad != fSelectedPad;
            fSelectedPad = selectedPad;
            fBank = std::clamp(bankForGlobalPad(fSelectedPad), 0,
                               static_cast<int>(midichopper::kBankCount - 1));
            if (selectionChanged) {
                if (fEditorMode)
                    requestWaveform();
                else
                    fEditorSnapshot.complete();
            }
        }
        else if (std::strcmp(key, "status") == 0)
            copyString(fStatus, value);
        else if (std::strcmp(key, "waveform_data") == 0)
        {
            midichopper::plugin::EditorSnapshotReply reply;
            if (midichopper::plugin::decodeEditorSnapshotReply(value, reply) &&
                fEditorSnapshot.accept(reply))
                commitEditorSnapshotIfReady();
        }
        else if (std::strcmp(key, "waveform_detail_data") == 0)
        {
            midichopper::plugin::WaveformDetailReply reply;
            if (midichopper::plugin::decodeWaveformDetailReply(value, reply))
                static_cast<void>(fWaveformDetail.accept(reply));
        }
        else if (std::strcmp(key, "chop_snapshot_data") == 0 ||
                 std::strcmp(key, "chop_status") == 0)
        {
            static_cast<void>(fChopEditor.acceptState(key, value));
        }
        else if (std::strcmp(key, "pad_structure_status") == 0)
        {
            midichopper::plugin::SplitPlanReady plan;
            if (midichopper::plugin::decodeSplitPlanReady(value, plan)) {
                const bool expected = fPadStructureBusy && fPendingSplitTarget >= 0 &&
                    plan.targetPad == static_cast<std::uint32_t>(fPendingSplitTarget) &&
                    !fArm && globalPad(0) == fPendingSplitFirst &&
                    visiblePadCount() == fPendingSplitCount;
                fPadStructureBusy = false;
                fPendingSplitTarget = -1;
                fPendingSplitFirst = -1;
                fPendingSplitCount = 0;
                if (expected) {
                    fChopEditor.openSplit(plan);
                } else {
                    fChopEditor.cancelPlan(plan.planId);
                    copyString(fStatus, "Split cancelled because pad context changed");
                }
            } else if (std::strcmp(value, "PS1;O;0;C") == 0) {
                fPadStructureBusy = false;
                copyString(fStatus, "Pad gap collapsed");
                if (fEditorMode)
                    refreshSelectedWaveform();
            } else if (fChopEditor.acceptSplitStatus(value, globalPad(0), visiblePadCount())) {
                fPendingSplitTarget = -1;
                fPendingSplitFirst = -1;
                fPendingSplitCount = 0;
            } else if (std::strncmp(value, "PS1;E;", 6U) == 0) {
                char* idEnd = nullptr;
                const auto errorPlanId = std::strtoull(value + 6U, &idEnd, 10);
                if (idEnd == value + 6U || idEnd == nullptr || *idEnd != ';')
                    return;
                const bool pendingError = errorPlanId == 0U && fPadStructureBusy;
                if (!pendingError)
                    return;
                fPadStructureBusy = false;
                fPendingSplitTarget = -1;
                fPendingSplitFirst = -1;
                fPendingSplitCount = 0;
                copyString(fStatus, idEnd + 1U);
            }
        }
        else if (std::strcmp(key, "pad_file_busy") == 0)
            fPadFileBusy = value[0] != '0' && value[0] != '\0';
        else if (std::strcmp(key, "pad_file_status") == 0)
        {
            fPadFileBusy = false;
            copyString(fStatus, value);
            if (fActiveFileAction == PendingFileDialog::import &&
                fActiveFilePad == fSelectedPad && std::strcmp(value, "WAV imported") == 0) {
                refreshSelectedWaveform();
            }
            fActiveFileAction = PendingFileDialog::none;
            fActiveFilePad = -1;
        }
        else if (parseEditorState(key, value))
        {
        }
        else if (parseMixerState(key, value))
        {
        }
        else if (parsePadColorState(key, value))
        {
        }
        else
            return;
        requestRepaint();
    }
#endif

    void onUiIdle() override
    {
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fUiBridge != nullptr) {
            midichopper::plugin::UiMessageBus::Message message;
            bool skipped = false;
            while (fUiBridge->readUiMessage(fUiMessageCursor, message, skipped)) {
                if (skipped) {
                    fPadStructureBusy = false;
                    fPendingSplitTarget = -1;
                    fPendingSplitFirst = -1;
                    fPendingSplitCount = 0;
                    if (fChopEditor.split() || fChopEditor.applying())
                        fChopEditor.cancel();
                    refreshSelectedWaveform();
                    setLocalStatus("UI replies were missed; check the pad and retry");
                }
                stateChanged(message.key.data(), message.value.data());
            }
        }
#endif
        const auto now = std::chrono::steady_clock::now();
        if (fClearArmed && now >= fClearDeadline) {
            fClearArmed = false;
            requestRepaint();
        }
        if (fPadContextClearArmed && now >= fPadContextClearDeadline) {
            fPadContextClearArmed = false;
            requestRepaint();
        }
        if (fPadContextCollapseArmed && now >= fPadContextCollapseDeadline) {
            fPadContextCollapseArmed = false;
            requestRepaint();
        }
        if (fMeterRepaintPending && now >= fNextMeterRepaint) {
            fMeterRepaintPending = false;
            fNextMeterRepaint = now + kMeterFrameInterval;
            requestRepaint();
        }
        fChopEditor.idle(now);
        if (fEditorSnapshot.pending() && now >= fEditorSnapshotRetryDeadline)
            sendEditorSnapshotRequest(now);
        if (fWaveformDetail.requestDue(now))
            requestWaveformDetail();
        updateLocalPlayhead(now);
    }

    void onNanoDisplay() override
    {
        // Any full redraw presents the latest levels, even when another control
        // caused it, so do not schedule a redundant meter-only frame.
        fMeterRepaintPending = false;
        fNextMeterRepaint = std::chrono::steady_clock::now() + kMeterFrameInterval;

        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());

        beginPath();
        rect(0.0f, 0.0f, w, h);
        fillColor(sms::ui::dpf::theme().canvas);
        fill();

        beginLogicalDisplay();
        if (!fLogo.isValid())
            fLogo = createImageFromMemory(anvilsampler_artwork::anvilData,
                                          anvilsampler_artwork::anvilDataSize,
                                          DGL_NAMESPACE::NanoVG::IMAGE_GENERATE_MIPMAPS);
        char collapseLabel[48];
        const int collapsingPads = collapseShiftCount();
        if (collapsingPads == 0) {
            std::snprintf(collapseLabel, sizeof(collapseLabel), "COLLAPSE GAP");
        } else {
            std::snprintf(collapseLabel, sizeof(collapseLabel), "COLLAPSE GAP (%d %s)",
                          collapsingPads, collapsingPads == 1 ? "PAD" : "PADS");
        }
        std::array<sms::ui::ContextMenuItemView, kPadMenuEntries.size()>
            contextMenuItems{};
        for (std::size_t index = 0; index < contextMenuItems.size(); ++index) {
            const PadMenuEntry entry = kPadMenuEntries[index];
            contextMenuItems[index] = entry.kind == sms::ui::ContextMenuItemKind::separator
                ? sms::ui::ContextMenuItemView{
                    "", false, false, sms::ui::ContextMenuItemKind::separator}
                : padMenuActionView(entry, collapseLabel);
        }
        std::array<sms::ui::ContextMenuItemView, kPadColorMenuItemCount>
            colorMenuItems{};
        const int selectedColor = fPadContextTarget >= 0
            ? fPadColors[static_cast<std::size_t>(fPadContextTarget)] : 0;
        colorMenuItems[0] = {"NONE", true, false,
                             sms::ui::ContextMenuItemKind::action,
                             selectedColor == 0};
        const auto& palette = sms::ui::dpf::theme().padColors;
        for (std::size_t index = 0; index < palette.size(); ++index)
            colorMenuItems[index + 1U] = {
                palette[index].name, true, false,
                sms::ui::ContextMenuItemKind::action,
                selectedColor == static_cast<int>(index + 1U),
                static_cast<int>(index)};
        const midichopper::ui::ViewState view{
            fArm, fRecordMode, fFixedLength, fPlaybackMode, fMonitor,
            fStartPad, fPreRoll, fBaseNote, fMidiBankMode, fGain, fMonitorGain, fGlobalPan, fGlobalTune,
            fGlobalLowpass, fGlobalHighpass, fGlobalFilterSlope, fGlobalDirty,
            fMaxVoices, fBank, fLayout,
            fSelectedPad, fCurrentPad, fPadPress.pad(), fClearArmed, fMenuOpen,
            fPadContextMenuOpen, fPadContextMenu,
            std::span<const sms::ui::ContextMenuItemView>{contextMenuItems},
            fPadColorMenuOpen, fPadColorMenu,
            std::span<const sms::ui::ContextMenuItemView>{colorMenuItems},
            fHover.target(),
            fMixerValueEntry.target(), fMixerValueEntry.text(),
            fEditorMode, fChopEditor.active(), fChopEditor.split(), fPlayOnSelect,
            fAnyPlaybackActive, fHasWaveform,
            fInputLevels, fOutputLevels, fPadState, fPadStatus, fPadColors,
            fEditorSettings, fMixerSettings, fWaveform, fPlaybackIndicator.position(),
            std::span<const sms::audio::WaveformSummary>{
                fChopEditor.waveforms().data(), fChopEditor.waveforms().size()},
            std::span<const std::int64_t>{
                fChopEditor.offsets().data(), fChopEditor.offsets().size()},
            fChopEditor.firstPad(), fSelectedPad, fChopEditor.previewPosition(), fChopEditor.previewPad(),
            fChopEditor.activeBoundary(), fChopEditor.ready(),
            fChopEditor.dirty(), fChopEditor.applying(),
            fChopEditor.canNavigate(-1, globalPad(0), visiblePadCount()),
            fChopEditor.canNavigate(1, globalPad(0), visiblePadCount()), fStatus,
            fWaveformDetail.viewport(), fWaveformDetail.detail(),
            &fLogo,
        };
        midichopper::ui::draw(*this, view);
        endLogicalDisplay();

    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.press)
            getWindow().focus();
        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX() - uiLayout::contentOffsetX;
        const float y = position.getY();

        if (ev.button == DGL_NAMESPACE::kMouseButtonMiddle)
        {
            if (!ev.press) {
                const bool captured = fResetPointerCaptured;
                fResetPointerCaptured = false;
                return captured;
            }
            const auto clicked = resolveInteractiveTarget(x, y);
            fMixerValueEntry.cancel();
            if (resetMixerControl(clicked)) {
                fResetPointerCaptured = true;
                return true;
            }
            return false;
        }

        if (ev.button == DGL_NAMESPACE::kMouseButtonRight)
        {
            if (!ev.press)
                return fPadContextMenuOpen;
            fMixerValueEntry.cancel();
            if (fArm || fChopEditor.active())
                return false;

            const int visualIndex = fEditorMode
                ? editorPadGrid().hit({x, y}) : mainPadGrid().hit({x, y});
            const int localPad = localPadFromVisualIndex(visualIndex);
            if (localPad >= 0)
            {
                fMenuOpen = false;
                const int pad = globalPad(localPad);
                if (fEditorMode)
                    selectEditorPad(pad);
                else {
                    if (fSelectedPad != pad) {
                        fEditorSettings = {};
                        fEditorSettingsPad = -1;
                        fMixerSettings = {};
                        fMixerSettingsPad = -1;
                    }
                    fSelectedPad = pad;
                    refreshSelectedWaveform();
                }
                openPadContextMenu(pad, {x, y});
                requestRepaint();
                return true;
            }
            if (fPadContextMenuOpen)
            {
                closePadContextMenu();
                requestRepaint();
                return true;
            }
            return false;
        }

        if (ev.button != DGL_NAMESPACE::kMouseButtonLeft)
            return false;

        if (ev.press)
        {
            const auto clicked = resolveInteractiveTarget(x, y);
            if (fMixerValueEntry.target().valid() && clicked != fMixerValueEntry.target())
                fMixerValueEntry.cancel();
            if (fHover.update(clicked))
                requestRepaint();
            if (midichopper::ui::isTarget(clicked,
                    midichopper::ui::InteractiveType::levelFader) ||
                midichopper::ui::isTarget(clicked,
                    midichopper::ui::InteractiveType::levelValueLabel)) {
                const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                const bool doubleClick = fDoubleClick.press(clicked, {x, y},
                    static_cast<std::uint64_t>(milliseconds));
                if (midichopper::ui::isTarget(clicked,
                        midichopper::ui::InteractiveType::levelValueLabel)) {
                    if (doubleClick)
                        beginMixerValueEntry(clicked);
                } else if (doubleClick) {
                    fResetPointerCaptured = resetMixerControl(clicked);
                } else {
                    fLevelFaderDrag = clicked.index;
                    const auto bounds = uiLayout::levelFader(clicked.index);
                    const auto track = uiLayout::levelFaderTrack(bounds);
                    const auto range = parameterRanges::outputGainDb;
                    const float value = clicked.index == 0 ? fMonitorGain : fGain;
                    const float capY = track.y + track.height *
                        (1.0f - (value - range.minimum) / (range.maximum - range.minimum));
                    fLevelFaderGrabY = std::abs(y - capY) <= 17.0f ? y - capY : 0.0f;
                    editParameter(levelParameter(clicked.index), true);
                    updateLevelFaderDrag(y);
                }
                return true;
            }
            if (fPadContextMenuOpen)
            {
                fPadContextPointerCaptured = true;
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::padColorItem)) {
                    selectPadColor(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::padContextItem)) {
                    const auto row = static_cast<std::size_t>(clicked.index);
                    if (row < kPadMenuEntries.size())
                        invokePadMenuAction(kPadMenuEntries[row]);
                    return true;
                }
                closePadContextMenu();
                requestRepaint();
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::menuButton))
            {
                fMenuOpen = !fMenuOpen;
                requestRepaint();
                return true;
            }
            if (fMenuOpen)
            {
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::menuLayout)) {
                    fMenuOpen = false;
                    selectLayout(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::menuMidiBankMode)) {
                    fMenuOpen = false;
                    selectMidiBankMode(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::menuLink)) {
                    fMenuOpen = false;
                    const std::string helpUrl = midichopper::ui::onlineHelpUrl(
                        ANVILSAMPLER_HELP_REF);
                    const char* const url = clicked.index == 0
                        ? helpUrl.c_str() : midichopper::ui::kIssueUrl.data();
                    if (!openBrowser(url))
                        setLocalStatus("Could not open the browser");
                    requestRepaint();
                    return true;
                }
                fMenuOpen = false;
                requestRepaint();
                return true;
            }
            if (fChopEditor.active())
            {
                if (beginViewportDrag(clicked, x, y))
                    return true;
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopBoundary)) {
                    fChopEditor.beginBoundaryDrag(clicked.index, x);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopPadPreview)) {
                    fChopEditor.preview(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopApply)) {
                    if (fChopEditor.dirty() && !fChopEditor.applying())
                        fChopEditor.apply();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopPrevious)) {
                    fChopEditor.navigate(-1, globalPad(0), visiblePadCount());
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopExit)) {
                    if (!fChopEditor.applying())
                        fChopEditor.cancel();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopNext)) {
                    fChopEditor.navigate(1, globalPad(0), visiblePadCount());
                    return true;
                }
                return false;
            }
            if (fEditorMode)
            {
                if (beginViewportDrag(clicked, x, y))
                    return true;
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::playStop)) {
                    fPlayButtonPressCaptured = true;
                    togglePlayStop();
                    return true;
                }
                if ((midichopper::ui::isTarget(clicked,
                         midichopper::ui::InteractiveType::mixerKnob) ||
                     midichopper::ui::isTarget(clicked,
                         midichopper::ui::InteractiveType::mixerValueLabel)) &&
                    clicked.index >= 5) {
                    if (clicked.index == 5) {
                        fMixerDragIndex = 5;
                        updateMixerDrag(x, y);
                    } else {
                        fMixerSettings.dirty = fMixerSettings.dirty >= 0.5f ? 0.0f : 1.0f;
                        commitMixerSettings();
                    }
                    requestRepaint();
                    return true;
                }
                const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::mixerValueLabel)) {
                    if (clicked == fMixerValueEntry.target())
                        return true;
                    if (fDoubleClick.press(clicked, {x, y},
                            static_cast<std::uint64_t>(now))) {
                        fMixerDragIndex = -1;
                        beginMixerValueEntry(clicked);
                    } else {
                        fMixerDragIndex = clicked.index;
                        fMixerDragStartY = y;
                        fMixerDragStartSettings = fMixerSettings;
                        fMixerDragAdjustment = knobAdjustment(ev.mod);
                    }
                    return true;
                }
                if (midichopper::ui::isResettableMixerControl(clicked)) {
                    if (fDoubleClick.press(clicked, {x, y},
                            static_cast<std::uint64_t>(now))) {
                        fDragTarget = WaveformEditTarget::none;
                        fMixerDragIndex = -1;
                        fResetPointerCaptured = resetMixerControl(clicked);
                        return fResetPointerCaptured;
                    }
                } else
                    static_cast<void>(fDoubleClick.press(
                        sms::ui::kNoInteractiveTarget, {x, y},
                        static_cast<std::uint64_t>(now)));
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::closeEditor))
                {
                    releasePressedPad();
                    fEditorMode = false;
                    fDragTarget = WaveformEditTarget::none;
                    fMixerDragIndex = -1;
                    restoreLastPlayedSelection();
                    requestRepaint();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::bank)) {
                    selectBank(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::pad)) {
                    selectEditorPad(globalPad(clicked.index));
                    if (fPlayOnSelect)
                        pressPlaybackPad(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::playOnSelect)) {
                    fPlayOnSelect = !fPlayOnSelect;
                    requestRepaint();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::regionHandle))
                {
                    fDragTarget = static_cast<WaveformEditTarget>(clicked.index);
                    updateEditorDrag(x, y);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::envelopeNode))
                {
                    fDragTarget = static_cast<WaveformEditTarget>(clicked.index);
                    fDragStartX = x;
                    fDragStartY = y;
                    fDragStartSettings = fEditorSettings;
                    return true;
                }
                constexpr std::array sliderTargets{
                    WaveformEditTarget::attackSlider,
                    WaveformEditTarget::decaySlider,
                    WaveformEditTarget::sustainSlider,
                    WaveformEditTarget::releaseSlider,
                };
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::envelopeSlider)) {
                    fDragTarget = sliderTargets[static_cast<std::size_t>(clicked.index)];
                    updateEditorDrag(x, y);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::mixerKnob)) {
                    fMixerDragIndex = clicked.index;
                    fMixerDragStartY = y;
                    fMixerDragStartSettings = fMixerSettings;
                    fMixerDragAdjustment = knobAdjustment(ev.mod);
                    return true;
                }
                return false;
            }

            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::playStop)) {
                fPlayButtonPressCaptured = true;
                togglePlayStop();
                return true;
            }
            if ((midichopper::ui::isTarget(clicked,
                     midichopper::ui::InteractiveType::globalMixerKnob) ||
                 midichopper::ui::isTarget(clicked,
                     midichopper::ui::InteractiveType::globalMixerValueLabel)) &&
                clicked.index >= 5) {
                if (clicked.index == 5) {
                    fGlobalMixerDragIndex = 5;
                    editParameter(kParameterGlobalFilterSlope, true);
                    updateGlobalMixerDrag(x, y);
                } else {
                    editParameter(kParameterGlobalDirty, true);
                    setControlValue(kParameterGlobalDirty,
                        fGlobalDirty >= 0.5f ? 0.0f : 1.0f);
                    editParameter(kParameterGlobalDirty, false);
                }
                requestRepaint();
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
                if (clicked == fMixerValueEntry.target())
                    return true;
                if (fDoubleClick.press(clicked, {x, y}, static_cast<std::uint64_t>(now))) {
                    fGlobalMixerDragIndex = -1;
                    beginMixerValueEntry(clicked);
                } else {
                    fGlobalMixerDragIndex = clicked.index;
                    fGlobalMixerDragStartY = y;
                    fGlobalMixerDragStart = {fGain, fGlobalPan, fGlobalTune,
                        fGlobalLowpass, fGlobalHighpass, fGlobalFilterSlope, fGlobalDirty};
                    fGlobalMixerDragAdjustment = knobAdjustment(ev.mod);
                }
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::globalMixerKnob)) {
                if (fDoubleClick.press(clicked, {x, y}, static_cast<std::uint64_t>(now))) {
                    fGlobalMixerDragIndex = -1;
                    fResetPointerCaptured = resetMixerControl(clicked);
                    return fResetPointerCaptured;
                }
            } else {
                static_cast<void>(fDoubleClick.press(
                    sms::ui::kNoInteractiveTarget, {x, y},
                    static_cast<std::uint64_t>(now)));
            }

            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::openEditor))
            {
                const int target = !hasSelectedPad() || bankForGlobalPad(fSelectedPad) != fBank
                    ? globalPad(0) : fSelectedPad;
                beginSampleEditor(target);
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::bank)) {
                selectBank(clicked.index);
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::pad))
            {
                const int pad = clicked.index;
                if (fArm)
                {
                    fSelectedPad = globalPad(pad);
                    fHasWaveform = false;
                    requestWaveform();
                    setControlValue(kParameterStartPad, static_cast<float>(pad + 1));
                    fCaptureTargetRequestAlternateHalf = !fCaptureTargetRequestAlternateHalf;
                    setParameterValue(kParameterCaptureTargetRequest,
                        captureTargetRequestValue(
                            static_cast<std::uint32_t>(globalPad(pad)),
                            fCaptureTargetRequestAlternateHalf));
                    setLocalStatus("Press Space or MIDI to start");
                }
                else
                {
                    pressPlaybackPad(pad);
                }
                requestRepaint();
                return true;
            }

            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::modeToggle))
            {
                setControlValue(kParameterMode, fArm ? 0.0f : 1.0f);
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::sequentialMode))
            {
                setControlValue(kParameterCaptureMode, 0.0f);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::fixedMode))
            {
                setControlValue(kParameterCaptureMode, 1.0f);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::fixedLength))
            {
                fMainSliderDrag = clicked.type;
                editParameter(kParameterFixedLengthSeconds, true);
                updateMainSliderDrag(x);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::oneShotMode))
            {
                setControlValue(kParameterPlaybackMode, 0.0f);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::gatedMode))
            {
                setControlValue(kParameterPlaybackMode, 1.0f);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::voiceLimit))
            {
                fMainSliderDrag = clicked.type;
                editParameter(kParameterMaxVoices, true);
                updateMainSliderDrag(x);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::preRoll))
            {
                fMainSliderDrag = clicked.type;
                editParameter(kParameterPreRollMs, true);
                updateMainSliderDrag(x);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::monitor))
            {
                setControlValue(kParameterInputMonitor, nextInputMonitorMode(fMonitor));
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::globalMixerKnob)) {
                fGlobalMixerDragIndex = clicked.index;
                fGlobalMixerDragStartY = y;
                fGlobalMixerDragStart = {fGain, fGlobalPan, fGlobalTune,
                    fGlobalLowpass, fGlobalHighpass, fGlobalFilterSlope, fGlobalDirty};
                fGlobalMixerDragAdjustment = knobAdjustment(ev.mod);
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::finalizeAction))
            {
#if DISTRHO_PLUGIN_WANT_STATE
                setState(midichopper::plugin::kCaptureActionRequestKey, "finalize");
#endif
                setLocalStatus("Chop finalized");
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::undoAction))
            {
#if DISTRHO_PLUGIN_WANT_STATE
                setState(midichopper::plugin::kCaptureActionRequestKey, "undo");
#endif
                setLocalStatus("Last chop undone");
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::clearAction))
            {
                if (!fClearArmed)
                {
                    fClearArmed = true;
                    fClearDeadline = std::chrono::steady_clock::now() +
                                     kClearConfirmationTimeout;
                    setLocalStatus("Click CLEAR again to confirm");
                }
                else
                {
#if DISTRHO_PLUGIN_WANT_STATE
                    setState(midichopper::plugin::kCaptureActionRequestKey, "clear");
#endif
                    fClearArmed = false;
                    setLocalStatus("Pads cleared");
                }
                requestRepaint();
                return true;
            }
        }
        else if (fResetPointerCaptured)
        {
            fResetPointerCaptured = false;
            return true;
        }
        else if (fPadContextPointerCaptured)
        {
            fPadContextPointerCaptured = false;
            return true;
        }
        else if (fChopEditor.active() && fChopEditor.activeBoundary() >= 0)
        {
            fChopEditor.endBoundaryDrag();
            requestRepaint();
            return true;
        }
        else if (fViewportDrag >= 0)
        {
            fViewportDrag = -1;
            return true;
        }
        else if (fPlayButtonPressCaptured)
        {
            fPlayButtonPressCaptured = false;
            return true;
        }
        else if (fLevelFaderDrag >= 0)
        {
            editParameter(levelParameter(fLevelFaderDrag), false);
            fLevelFaderDrag = -1;
            return true;
        }
        else if (fEditorMode && fDragTarget != WaveformEditTarget::none)
        {
            commitEditorSettings();
            fDragTarget = WaveformEditTarget::none;
            requestRepaint();
            return true;
        }
        else if (fEditorMode && fMixerDragIndex >= 0)
        {
            commitMixerSettings();
            fMixerDragIndex = -1;
            requestRepaint();
            return true;
        }
        else if (fGlobalMixerDragIndex >= 0)
        {
            if (fGlobalMixerDragIndex == 5)
                editParameter(kParameterGlobalFilterSlope, false);
            fGlobalMixerDragIndex = -1;
            requestRepaint();
            return true;
        }
        else if (fMainSliderDrag >= 0)
        {
            const auto type = static_cast<midichopper::ui::InteractiveType>(fMainSliderDrag);
            const uint32_t parameter = type == midichopper::ui::InteractiveType::fixedLength
                ? kParameterFixedLengthSeconds
                : type == midichopper::ui::InteractiveType::voiceLimit
                    ? kParameterMaxVoices : kParameterPreRollMs;
            editParameter(parameter, false);
            fMainSliderDrag = -1;
            return true;
        }
        else if (fPadPress.pad() >= 0)
        {
            releasePressedPad();
            requestRepaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX() - uiLayout::contentOffsetX;
        const float y = position.getY();
        if (fLevelFaderDrag >= 0) {
            updateLevelFaderDrag(y);
            return true;
        }
        if (fViewportDrag >= 0) {
            updateViewportDrag(x, y);
            return true;
        }
        if (fChopEditor.active() && fChopEditor.activeBoundary() >= 0) {
            fChopEditor.dragBoundary(x, uiLayout::chopWaveform.width,
                                    fWaveformDetail.viewport());
            return true;
        }
        if (fEditorMode && fDragTarget != WaveformEditTarget::none) {
            updateEditorDrag(x, y);
            return true;
        }
        if (fEditorMode && fMixerDragIndex >= 0) {
            updateMixerDrag(x, y);
            return true;
        }
        if (fGlobalMixerDragIndex >= 0) {
            updateGlobalMixerDrag(x, y);
            return true;
        }
        if (fMainSliderDrag >= 0) {
            updateMainSliderDrag(x);
            return true;
        }
        const auto hovered = resolveInteractiveTarget(x, y);
        if (fPadContextMenuOpen) {
            if (midichopper::ui::isTarget(
                    hovered, midichopper::ui::InteractiveType::padContextItem,
                    kPadColorMenuRow)) {
                openPadColorMenu();
            } else if (fPadColorMenuOpen &&
                       midichopper::ui::isTarget(
                           hovered, midichopper::ui::InteractiveType::padContextItem)) {
                fPadColorMenuOpen = false;
                requestRepaint();
            }
        }
        if (fHover.update(hovered))
            requestRepaint();
        return hovered.valid() || fMenuOpen || fPadContextMenuOpen;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX() - uiLayout::contentOffsetX;
        const float y = position.getY();
        const auto hovered = resolveInteractiveTarget(x, y);
        if (fHover.update(hovered))
            requestRepaint();

        float delta = static_cast<float>(ev.delta.getY());
        if (delta == 0.0f) {
            if (ev.direction == DGL_NAMESPACE::kScrollUp)
                delta = 1.0f;
            else if (ev.direction == DGL_NAMESPACE::kScrollDown)
                delta = -1.0f;
            else if ((ev.mod & DGL_NAMESPACE::kModifierShift) != 0U) {
                delta = -static_cast<float>(ev.delta.getX());
                if (delta == 0.0f && ev.direction == DGL_NAMESPACE::kScrollLeft)
                    delta = 1.0f;
                else if (delta == 0.0f && ev.direction == DGL_NAMESPACE::kScrollRight)
                    delta = -1.0f;
            }
        }
        if (delta == 0.0f || !std::isfinite(delta))
            return false;

        if (midichopper::ui::isTarget(hovered, midichopper::ui::InteractiveType::levelFader) ||
            midichopper::ui::isTarget(hovered, midichopper::ui::InteractiveType::levelValueLabel)) {
            const auto range = parameterRanges::outputGainDb;
            setControlValueFromWheel(levelParameter(hovered.index),
                midichopper::ui::knobWheelAdjustedValue(
                    hovered.index == 0 ? fMonitorGain : fGain, delta, 0.5f, 1.0f,
                    range.minimum, range.maximum, knobAdjustment(ev.mod)));
            return true;
        }

        const auto waveformBounds = fChopEditor.active() ? uiLayout::chopWaveform :
            uiLayout::editorWaveform;
        if ((fEditorMode || (fChopEditor.active() && fChopEditor.ready() && !fChopEditor.applying())) &&
            waveformBounds.contains({x, y}) && fWaveformDetail.viewport().total != 0U &&
            (ev.mod & (DGL_NAMESPACE::kModifierControl |
                       DGL_NAMESPACE::kModifierShift)) != 0U) {
            const bool changed = (ev.mod & DGL_NAMESPACE::kModifierControl) != 0U
                ? fWaveformDetail.zoom(delta, x, waveformBounds, std::chrono::steady_clock::now())
                : fWaveformDetail.pan(delta, std::chrono::steady_clock::now());
            if (changed) {
                requestRepaint();
            }
            return true;
        }

        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::chopBoundary)) {
            fChopEditor.wheelBoundary(hovered.index, delta, uiLayout::chopWaveform,
                                     fWaveformDetail.viewport());
            return true;
        }
        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::regionHandle)) {
            sms::ui::waveform::adjustRegionByWheel(
                fEditorSettings, static_cast<WaveformEditTarget>(hovered.index), delta,
                fWaveform.frames, uiLayout::editorWaveform, fWaveformDetail.viewport());
            commitEditorSettings();
            requestRepaint();
            return true;
        }

        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::fixedLength)) {
            setControlValueFromWheel(kParameterFixedLengthSeconds,
                sms::ui::wheelAdjustedValue(fFixedLength, delta, 0.1f,
                    parameterRanges::fixedLengthSeconds.minimum,
                    parameterRanges::fixedLengthSeconds.maximum));
            return true;
        }
        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::voiceLimit)) {
            setControlValueFromWheel(kParameterMaxVoices,
                sms::ui::wheelAdjustedValue(static_cast<float>(fMaxVoices), delta, 1.0f,
                    parameterRanges::maxVoices.minimum,
                    parameterRanges::maxVoices.maximum, true));
            return true;
        }
        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::preRoll)) {
            setControlValueFromWheel(kParameterPreRollMs,
                sms::ui::wheelAdjustedValue(fPreRoll, delta, 1.0f,
                    parameterRanges::preRollMs.minimum,
                    parameterRanges::preRollMs.maximum, true));
            return true;
        }
        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::mixerKnob)) {
            const auto adjustment = knobAdjustment(ev.mod);
            switch (hovered.index) {
            case 0:
                fMixerSettings.gainDecibels = midichopper::ui::knobWheelAdjustedValue(
                    fMixerSettings.gainDecibels, delta, 0.5f, 1.0f,
                    sms::dsp::kMinimumSampleGainDecibels,
                    sms::dsp::kMaximumSampleGainDecibels, adjustment);
                break;
            case 1:
                fMixerSettings.pan = midichopper::ui::knobWheelAdjustedValue(
                    fMixerSettings.pan, delta, 0.05f, 0.1f,
                    sms::dsp::kMinimumSamplePan, sms::dsp::kMaximumSamplePan,
                    adjustment);
                break;
            case 2:
                fMixerSettings.tuneSemitones = midichopper::ui::knobWheelAdjustedValue(
                    fMixerSettings.tuneSemitones, delta, 0.25f, 1.0f,
                    sms::dsp::kMinimumTuneSemitones,
                    sms::dsp::kMaximumTuneSemitones, adjustment);
                break;
            case 3:
                fMixerSettings.lowpass = midichopper::ui::knobWheelAdjustedValue(
                    fMixerSettings.lowpass, delta, 0.025f, 0.1f, 0.0f, 1.0f,
                    adjustment);
                break;
            case 4:
                fMixerSettings.highpass = midichopper::ui::knobWheelAdjustedValue(
                    fMixerSettings.highpass, delta, 0.025f, 0.1f, 0.0f, 1.0f,
                    adjustment);
                break;
            case 5:
                fMixerSettings.filterSlope = std::clamp(fMixerSettings.filterSlope +
                    static_cast<float>(delta > 0 ? 1 : -1), 0.0f, 2.0f);
                break;
            case 6:
                fMixerSettings.dirty = fMixerSettings.dirty >= 0.5f ? 0.0f : 1.0f;
                break;
            default:
                return false;
            }
            fMixerSettings = sms::dsp::sanitize(fMixerSettings);
            commitMixerSettings();
            requestRepaint();
            return true;
        }
        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::globalMixerKnob)) {
            const auto adjustment = knobAdjustment(ev.mod);
            switch (hovered.index) {
            case 0:
                setControlValueFromWheel(kParameterOutputGainDb,
                    midichopper::ui::knobWheelAdjustedValue(fGain, delta, 0.5f, 1.0f,
                        parameterRanges::outputGainDb.minimum,
                        parameterRanges::outputGainDb.maximum, adjustment));
                break;
            case 1:
                setControlValueFromWheel(kParameterGlobalPan,
                    midichopper::ui::knobWheelAdjustedValue(fGlobalPan, delta, 0.05f, 0.1f,
                        parameterRanges::globalPan.minimum,
                        parameterRanges::globalPan.maximum, adjustment));
                break;
            case 2:
                setControlValueFromWheel(kParameterGlobalTuneSemitones,
                    midichopper::ui::knobWheelAdjustedValue(fGlobalTune, delta, 0.25f, 1.0f,
                        parameterRanges::globalTuneSemitones.minimum,
                        parameterRanges::globalTuneSemitones.maximum, adjustment));
                break;
            case 3:
                setControlValueFromWheel(kParameterGlobalLowpass,
                    midichopper::ui::knobWheelAdjustedValue(fGlobalLowpass, delta,
                        0.025f, 0.1f, 0.0f, 1.0f, adjustment));
                break;
            case 4:
                setControlValueFromWheel(kParameterGlobalHighpass,
                    midichopper::ui::knobWheelAdjustedValue(fGlobalHighpass, delta,
                        0.025f, 0.1f, 0.0f, 1.0f, adjustment));
                break;
            case 5:
                setControlValueFromWheel(kParameterGlobalFilterSlope,
                    std::clamp(fGlobalFilterSlope + static_cast<float>(delta > 0 ? 1 : -1),
                        0.0f, 2.0f));
                break;
            case 6:
                setControlValueFromWheel(kParameterGlobalDirty,
                    fGlobalDirty >= 0.5f ? 0.0f : 1.0f);
                break;
            default:
                return false;
            }
            return true;
        }
        if (!midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::envelopeSlider))
            return false;

        switch (hovered.index) {
        case 0:
            fEditorSettings.attackSeconds = sms::ui::wheelAdjustedValue(
                fEditorSettings.attackSeconds, delta, 0.01f, 0.0f,
                sms::ui::waveform::kEnvelopeControlMaximumSeconds);
            break;
        case 1:
            fEditorSettings.decaySeconds = sms::ui::wheelAdjustedValue(
                fEditorSettings.decaySeconds, delta, 0.01f, 0.0f,
                sms::ui::waveform::kEnvelopeControlMaximumSeconds);
            break;
        case 2:
            fEditorSettings.sustainLevel = sms::ui::wheelAdjustedValue(
                fEditorSettings.sustainLevel, delta, 0.01f, 0.0f, 1.0f);
            break;
        case 3:
            fEditorSettings.releaseSeconds = sms::ui::wheelAdjustedValue(
                fEditorSettings.releaseSeconds, delta, 0.01f, 0.0f,
                sms::ui::waveform::kEnvelopeControlMaximumSeconds);
            break;
        default:
            return false;
        }
        fEditorSettings = sms::dsp::sanitize(fEditorSettings);
        commitEditorSettings();
        requestRepaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (ev.key == ' ') {
            // Consume Space even under overlays or numeric entry. X11 repeat
            // includes synthetic releases, so suppress it while Space is held.
            getWindow().setIgnoringKeyRepeat(ev.press);
            const bool blocked = fMixerValueEntry.target().valid() || fMenuOpen ||
                fPadContextMenuOpen || fChopEditor.active() ||
                fPendingFileDialog != PendingFileDialog::none;
            const auto action = fSpaceKey.update(ev.press, blocked, fArm);
            if (action == midichopper::ui::SpaceAction::playStop)
                togglePlayStop();
            else if (action == midichopper::ui::SpaceAction::chop) {
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
                const auto note = static_cast<uint8_t>(mappedMidiNote(globalPad(0)));
                // Capture ignores note identity and release. Balance immediately
                // so a mode change while the key is held cannot leave a note on.
                sendNote(0, note, 127);
                sendNote(0, note, 0);
#endif
            }
            return true;
        }
        if (fMixerValueEntry.target().valid()) {
            if (!ev.press)
                return true;
            if (ev.key == DGL_NAMESPACE::kKeyEscape) {
                fMixerValueEntry.cancel();
                requestRepaint();
            } else if (ev.key == DGL_NAMESPACE::kKeyEnter) {
                commitMixerValueEntry();
            } else if (ev.key == DGL_NAMESPACE::kKeyBackspace) {
                fMixerValueEntry.backspace();
                requestRepaint();
            } else if (ev.key == DGL_NAMESPACE::kKeyDelete) {
                fMixerValueEntry.clear();
                requestRepaint();
            }
            return true;
        }
        if (!ev.press || ev.key != DGL_NAMESPACE::kKeyEscape)
            return false;
        if (fPadContextMenuOpen) {
            closePadContextMenu();
            requestRepaint();
            return true;
        }
        if (fChopEditor.active() && !fChopEditor.applying()) {
            fChopEditor.cancel();
            return true;
        }
        return false;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (ev.character == ' ')
            return true;
        if (!fMixerValueEntry.target().valid())
            return false;
        if (fMixerValueEntry.type(ev.character))
            requestRepaint();
        return true;
    }

    void uiFocus(const bool focus, DGL_NAMESPACE::CrossingMode) override
    {
        if (!focus) {
            fSpaceKey.reset();
            getWindow().setIgnoringKeyRepeat(false);
            if (fLevelFaderDrag >= 0) {
                editParameter(levelParameter(fLevelFaderDrag), false);
                fLevelFaderDrag = -1;
            }
        }
    }

#if DISTRHO_UI_FILE_BROWSER
    void uiFileBrowserSelected(const char* const filename) override
    {
        const PendingFileDialog action = fPendingFileDialog;
        const int pad = fPendingFilePad;
        fPendingFileDialog = PendingFileDialog::none;
        fPendingFilePad = -1;
        if (filename == nullptr || filename[0] == '\0') {
            requestRepaint();
            return;
        }

        std::string path(filename);
        const bool exporting = action == PendingFileDialog::exportRaw ||
                               action == PendingFileDialog::exportProcessed;
        if (exporting) {
            if (!hasAnyFileExtension(path))
                path += ".wav";
            else if (!hasWavFileExtension(path)) {
                setLocalStatus("Export filename must use the .wav extension");
                return;
            }
        }
        if (pad < 0 || pad >= static_cast<int>(midichopper::kPadCount) ||
            action == PendingFileDialog::none)
            return;

        midichopper::plugin::PadFileAction fileAction =
            midichopper::plugin::PadFileAction::import;
        if (action == PendingFileDialog::exportRaw)
            fileAction = midichopper::plugin::PadFileAction::exportRaw;
        else if (action == PendingFileDialog::exportProcessed)
            fileAction = midichopper::plugin::PadFileAction::exportProcessed;

        fPadFileBusy = true;
        fActiveFileAction = action;
        fActiveFilePad = pad;
        setLocalStatus(exporting ? "Exporting WAV..." : "Importing WAV...");
#if DISTRHO_PLUGIN_WANT_STATE
        const std::string request = midichopper::plugin::encodePadFileRequest(
            fileAction, static_cast<std::uint32_t>(pad), path);
        setState("pad_file_request", request.c_str());
#endif
        requestRepaint();
    }
#endif

private:
    DGL_NAMESPACE::NanoImage fLogo;
    bool fArm;
    float fRecordMode;
    float fFixedLength;
    float fPlaybackMode;
    float fMonitor;
    int fStartPad;
    float fPreRoll;
    int fBaseNote;
    int fMidiBankMode;
    float fGain;
    float fMonitorGain = 0.0f;
    float fGlobalPan;
    float fGlobalTune;
    float fGlobalLowpass = 0.0f;
    float fGlobalHighpass = 0.0f;
    float fGlobalFilterSlope = 0.0f;
    float fGlobalDirty = 0.0f;
    int fMaxVoices;
    bool fAnyPlaybackActive = false;
    bool fPlayButtonPressCaptured = false;
    int fBank;
    int fLayout;
    int fSelectedPad;
    int fLastPlayedPad;
    int fCurrentPad;
    midichopper::ui::PadPressTracker fPadPress;
    midichopper::ui::SpaceKeyTracker fSpaceKey;
    bool fClearArmed;
    std::chrono::steady_clock::time_point fClearDeadline{};
    bool fMenuOpen;
    bool fPadContextMenuOpen;
    bool fPadColorMenuOpen = false;
    int fPadContextTarget;
    sms::ui::ContextMenuGeometry fPadContextMenu;
    sms::ui::ContextMenuGeometry fPadColorMenu;
    sms::ui::HoverState fHover;
    bool fPadContextClearArmed;
    std::chrono::steady_clock::time_point fPadContextClearDeadline{};
    bool fPadContextCollapseArmed = false;
    std::chrono::steady_clock::time_point fPadContextCollapseDeadline{};
    bool fPadContextPointerCaptured;
    bool fResetPointerCaptured = false;
    bool fPadFileBusy = false;
    bool fPadClipboardAvailable = false;
    bool fPadClipboardBusy = false;
    bool fPadStructureBusy = false;
    int fPendingSplitTarget = -1;
    int fPendingSplitFirst = -1;
    int fPendingSplitCount = 0;
    PadClipboardAction fActivePadClipboardAction = PadClipboardAction::copy;
    int fActivePadClipboardPad = -1;
    bool fSaveDialogAvailable = true;
    PendingFileDialog fPendingFileDialog = PendingFileDialog::none;
    int fPendingFilePad = -1;
    PendingFileDialog fActiveFileAction = PendingFileDialog::none;
    int fActiveFilePad = -1;
    bool fEditorMode;
    bool fPlayOnSelect;
    WaveformEditTarget fDragTarget;
    int fMixerDragIndex;
    float fMixerDragStartY = 0.0f;
    sms::dsp::SampleMixerSettings fMixerDragStartSettings{};
    midichopper::ui::KnobAdjustment fMixerDragAdjustment =
        midichopper::ui::KnobAdjustment::normal;
    int fGlobalMixerDragIndex;
    int fMainSliderDrag = -1;
    int fLevelFaderDrag = -1;
    float fLevelFaderGrabY = 0.0f;
    int fViewportDrag = -1;
    float fViewportScrollGrabX = 0.0f;
    float fGlobalMixerDragStartY = 0.0f;
    sms::dsp::SampleMixerSettings fGlobalMixerDragStart{};
    midichopper::ui::KnobAdjustment fGlobalMixerDragAdjustment =
        midichopper::ui::KnobAdjustment::normal;
    midichopper::ui::DoubleClickTracker fDoubleClick;
    midichopper::ui::MixerValueEntry fMixerValueEntry;
    float fDragStartX;
    float fDragStartY;
    bool fHasWaveform;
    midichopper::ui::EditorSnapshotSession fEditorSnapshot;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
    PluginUiBridge* fUiBridge = nullptr;
    std::uint64_t fUiMessageCursor = 0U;
#endif
    std::chrono::steady_clock::time_point fEditorSnapshotRetryDeadline{};
    std::array<float, 2> fInputLevels{};
    std::array<float, 2> fOutputLevels{};
    bool fMeterRepaintPending = false;
    std::chrono::steady_clock::time_point fNextMeterRepaint{};
    bool fCaptureTargetRequestAlternateHalf;
    PlaybackPadEventTracker fPlaybackPadEvents;
    midichopper::plugin::PadFileResultEventTracker fPadFileResultEvents;
    midichopper::plugin::PadClipboardResultEventTracker fPadClipboardResultEvents;
    std::array<char, midichopper::kPadsPerBank> fPadState;
    std::array<char, midichopper::kPadsPerBank> fPadStatus;
    std::array<int, midichopper::kPadCount> fPadColors{};
    sms::dsp::SamplePlaybackSettings fEditorSettings{};
    int fEditorSettingsPad = -1;
    sms::dsp::SamplePlaybackSettings fDragStartSettings{};
    sms::dsp::SampleMixerSettings fMixerSettings{};
    int fMixerSettingsPad = -1;
    sms::audio::WaveformSummary fWaveform{};
    midichopper::ui::WaveformDetailSession fWaveformDetail;
    midichopper::ui::ChopEditorController fChopEditor{*this};
    midichopper::ui::PlaybackIndicator fPlaybackIndicator;
    char fStatus[160];

    [[nodiscard]] sms::ui::InteractiveTarget
    resolveInteractiveTarget(const float x, const float y) const noexcept
    {
        std::array<bool, kPadMenuEntries.size()> menuEnabled{};
        for (std::size_t index = 0; index < menuEnabled.size(); ++index)
            menuEnabled[index] = kPadMenuEntries[index].kind ==
                    sms::ui::ContextMenuItemKind::action &&
                padMenuActionEnabled(kPadMenuEntries[index].action);
        const auto envelope = envelopeGraphGeometry();
        midichopper::ui::InteractionContext context;
        context.editorMode = fEditorMode;
        context.chopEditorMode = fChopEditor.active();
        context.chopSplitMode = fChopEditor.split();
        context.menuOpen = fMenuOpen;
        context.padContextMenuOpen = fPadContextMenuOpen;
        context.padColorMenuOpen = fPadColorMenuOpen;
        context.armed = fArm;
        context.fixedCapture = fRecordMode >= 0.5f;
        context.captureActive = fArm && fCurrentPad >= 0;
        context.chopReady = fChopEditor.ready();
        context.chopApplying = fChopEditor.applying();
        context.chopApplyEnabled = fChopEditor.ready() && fChopEditor.dirty() && !fChopEditor.applying();
        context.chopPreviousEnabled =
            fChopEditor.canNavigate(-1, globalPad(0), visiblePadCount()) && !fChopEditor.applying();
        context.chopExitEnabled = !fChopEditor.applying();
        context.chopNextEnabled =
            fChopEditor.canNavigate(1, globalPad(0), visiblePadCount()) && !fChopEditor.applying();
        context.padLayout = fLayout;
        context.padContextMenu = fPadContextMenu;
        context.padContextMenuEnabled = menuEnabled;
        context.padColorMenu = fPadColorMenu;
        context.padColorMenuItemCount = static_cast<int>(kPadColorMenuItemCount);
        context.editorSettings = &fEditorSettings;
        context.envelope = &envelope;
        context.chopWaveforms = fChopEditor.waveforms();
        context.chopOffsets = fChopEditor.offsets();
        context.waveformViewport = fWaveformDetail.viewport();
        return midichopper::ui::interactiveTargetAt({x, y}, context);
    }

    static float normalizedX(const float x, const sms::ui::Rect bounds) noexcept
    {
        return std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
    }

    [[nodiscard]] static std::uint32_t levelParameter(const int index) noexcept
    {
        return index == 0 ? kParameterMonitorGainDb : kParameterOutputGainDb;
    }

    void updateLevelFaderDrag(const float y)
    {
        setControlValue(levelParameter(fLevelFaderDrag), midichopper::ui::levelFaderValueAtY(
            y - fLevelFaderGrabY, uiLayout::levelFader(fLevelFaderDrag),
            parameterRanges::outputGainDb));
    }

    bool beginViewportDrag(const sms::ui::InteractiveTarget clicked,
                           const float x, const float y)
    {
        if (!midichopper::ui::isTarget(clicked,
                midichopper::ui::InteractiveType::waveformZoom) &&
            !midichopper::ui::isTarget(clicked,
                midichopper::ui::InteractiveType::waveformScroll))
            return false;
        fViewportDrag = clicked.type;
        if (midichopper::ui::isTarget(clicked,
                midichopper::ui::InteractiveType::waveformScroll)) {
            const auto track = fChopEditor.active() ? uiLayout::chopScroll : uiLayout::editorScroll;
            const auto thumb = fWaveformDetail.viewport().scrollThumb(track);
            fViewportScrollGrabX = thumb.contains({x, y})
                ? x - thumb.x : thumb.width * 0.5f;
        }
        updateViewportDrag(x, y);
        return true;
    }

    void updateViewportDrag(const float x, const float y)
    {
        bool changed = false;
        if (fViewportDrag == static_cast<int>(
                midichopper::ui::InteractiveType::waveformZoom)) {
            const auto track = fChopEditor.active() ? uiLayout::chopZoom : uiLayout::editorZoom;
            changed = fWaveformDetail.setZoomPosition(std::clamp(
                (track.y + track.height - 6.0f - y) / (track.height - 12.0f),
                0.0f, 1.0f), std::chrono::steady_clock::now());
        } else if (fViewportDrag == static_cast<int>(
                midichopper::ui::InteractiveType::waveformScroll)) {
            const auto track = fChopEditor.active() ? uiLayout::chopScroll : uiLayout::editorScroll;
            const auto thumb = fWaveformDetail.viewport().scrollThumb(track);
            if (track.width > thumb.width)
                changed = fWaveformDetail.setScrollPosition(
                    (x - fViewportScrollGrabX - track.x) / (track.width - thumb.width),
                    std::chrono::steady_clock::now());
        }
        if (changed)
            requestRepaint();
    }

    void updateMainSliderDrag(const float x)
    {
        const auto type = static_cast<midichopper::ui::InteractiveType>(fMainSliderDrag);
        if (type == midichopper::ui::InteractiveType::fixedLength) {
            setControlValue(kParameterFixedLengthSeconds,
                midichopper::ui::mainSliderValueAtX(x, uiLayout::fixedLength,
                    parameterRanges::fixedLengthSeconds));
        } else if (type == midichopper::ui::InteractiveType::voiceLimit) {
            setControlValue(kParameterMaxVoices,
                midichopper::ui::mainSliderValueAtX(x, uiLayout::voiceLimit,
                    parameterRanges::maxVoices, true));
        } else if (type == midichopper::ui::InteractiveType::preRoll) {
            setControlValue(kParameterPreRollMs,
                midichopper::ui::mainSliderValueAtX(x,
                    uiLayout::preRoll(fRecordMode >= 0.5f),
                    parameterRanges::preRollMs, true));
        }
    }

    void openPadContextMenu(const int pad, const sms::ui::Point anchor)
    {
        fPadContextTarget = pad;
        fPadContextMenu = sms::ui::ContextMenuGeometry(
            anchor, std::span<const sms::ui::ContextMenuItemKind>{kPadMenuItemKinds},
            uiLayout::contentBounds);
        fPadColorMenuOpen = false;
        static_cast<void>(fHover.clear());
        fPadContextClearArmed = false;
        fPadContextCollapseArmed = false;
        fPadContextMenuOpen = true;
    }

    void closePadContextMenu() noexcept
    {
        fPadContextMenuOpen = false;
        fPadColorMenuOpen = false;
        fPadContextTarget = -1;
        static_cast<void>(fHover.clear());
        fPadContextClearArmed = false;
        fPadContextCollapseArmed = false;
    }

    void openPadColorMenu()
    {
        if (fPadColorMenuOpen || !padMenuActionEnabled(PadMenuAction::color))
            return;
        fPadColorMenu = sms::ui::ContextMenuGeometry::submenu(
            fPadContextMenu, kPadColorMenuRow,
            static_cast<int>(kPadColorMenuItemCount), uiLayout::contentBounds);
        fPadColorMenuOpen = true;
        requestRepaint();
    }

    void selectPadColor(const int item)
    {
        if (!fPadColorMenuOpen || !padMenuActionEnabled(PadMenuAction::color) ||
            item < 0 || item >= static_cast<int>(kPadColorMenuItemCount))
            return;
        const auto pad = static_cast<std::size_t>(fPadContextTarget);
        fPadColors[pad] = item;
        setState(kPadColorStateKeys[pad].c_str(), std::to_string(item).c_str());
        closePadContextMenu();
        requestRepaint();
    }

    [[nodiscard]] bool padContextTargetOccupied() const noexcept
    {
        if (!fPadContextMenuOpen || fPadContextTarget < 0 ||
            bankForGlobalPad(fPadContextTarget) != fBank)
            return false;
        const int localPad = localPadForGlobalPad(fPadContextTarget);
        if (localPad < 0 || localPad >= visiblePadCount())
            return false;
        const char state = fPadState[static_cast<std::size_t>(localPad)];
        return state != '0' && state != '.';
    }

    [[nodiscard]] bool padExportEnabled() const noexcept
    {
        return fSaveDialogAvailable && !padActionBusy() && padContextTargetOccupied();
    }

    [[nodiscard]] bool padActionBusy() const noexcept
    {
        return fPadFileBusy || fPadClipboardBusy || fPadStructureBusy;
    }

    [[nodiscard]] bool padCopyEnabled() const noexcept
    {
        return !padActionBusy() && padContextTargetOccupied();
    }

    [[nodiscard]] bool padEditSampleEnabled() const noexcept
    {
        return !fEditorMode && !padActionBusy() && padContextTargetOccupied();
    }

    [[nodiscard]] bool padPasteEnabled() const noexcept
    {
        return !padActionBusy() && fPadClipboardAvailable &&
               fPadContextMenuOpen && fPadContextTarget >= 0;
    }

    [[nodiscard]] bool padCutPointsEnabled() const noexcept
    {
        if (padActionBusy() || !padContextTargetOccupied())
            return false;
        const int localPad = localPadForGlobalPad(fPadContextTarget);
        if (localPad <= 0 || localPad + 1 >= visiblePadCount())
            return false;
        return true;
    }

    [[nodiscard]] bool localPadOccupied(const int localPad) const noexcept
    {
        if (localPad < 0 || localPad >= visiblePadCount())
            return false;
        const char state = fPadState[static_cast<std::size_t>(localPad)];
        return state != '0' && state != '.';
    }

    [[nodiscard]] bool padSplitEnabled() const noexcept
    {
        if (padActionBusy() || !padContextTargetOccupied())
            return false;
        const int target = localPadForGlobalPad(fPadContextTarget);
        for (int pad = target + 1; pad < visiblePadCount(); ++pad) {
            if (!localPadOccupied(pad))
                return true;
        }
        return false;
    }

    [[nodiscard]] int collapseShiftCount() const noexcept
    {
        if (!fPadContextMenuOpen || fPadContextTarget < 0 ||
            padContextTargetOccupied())
            return 0;
        int pad = localPadForGlobalPad(fPadContextTarget) + 1;
        while (pad < visiblePadCount() && !localPadOccupied(pad))
            ++pad;
        int count = 0;
        while (pad < visiblePadCount() && localPadOccupied(pad)) {
            ++pad;
            ++count;
        }
        return count;
    }

    [[nodiscard]] bool padCollapseEnabled() const noexcept
    {
        return !padActionBusy() && fPadContextMenuOpen &&
               !padContextTargetOccupied() && collapseShiftCount() > 0;
    }

    [[nodiscard]] bool padProcessedExportEnabled() const noexcept
    {
        if (!padExportEnabled())
            return false;
        if (fEditorSettingsPad != fPadContextTarget)
            return false;
        if (fMixerSettingsPad != fPadContextTarget)
            return false;
        const auto settings = sms::dsp::sanitize(fEditorSettings);
        const auto mixer = sms::dsp::sanitize(fMixerSettings);
        return settings.start != 0.0f || settings.end != 1.0f ||
               settings.attackSeconds != 0.0f || settings.decaySeconds != 0.0f ||
               settings.sustainLevel != 1.0f || settings.releaseSeconds != 0.0f ||
               mixer.gainDecibels != 0.0f || mixer.pan != 0.0f ||
               mixer.tuneSemitones != 0.0f;
    }

    [[nodiscard]] bool padMenuActionEnabled(const PadMenuAction action) const noexcept
    {
        switch (action) {
        case PadMenuAction::editSample: return padEditSampleEnabled();
        case PadMenuAction::color:
            return fPadContextMenuOpen && fPadContextTarget >= 0 &&
                   fPadContextTarget < static_cast<int>(midichopper::kPadCount);
        case PadMenuAction::copy: return padCopyEnabled();
        case PadMenuAction::paste: return padPasteEnabled();
        case PadMenuAction::adjustCutPoints: return padCutPointsEnabled();
        case PadMenuAction::splitSample: return padSplitEnabled();
        case PadMenuAction::collapseGap: return padCollapseEnabled();
        case PadMenuAction::exportRaw: return padExportEnabled();
        case PadMenuAction::exportProcessed: return padProcessedExportEnabled();
        case PadMenuAction::import: return !padActionBusy();
        case PadMenuAction::clear:
            return !padActionBusy() && padContextTargetOccupied();
        case PadMenuAction::count: return false;
        }
        return false;
    }

    [[nodiscard]] sms::ui::ContextMenuItemView padMenuActionView(
        const PadMenuEntry entry, const char* const collapseLabel) const noexcept
    {
        const PadMenuAction action = entry.action;
        const bool enabled = padMenuActionEnabled(action);
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
            return {fPadContextCollapseArmed ? "CONFIRM COLLAPSE" : collapseLabel,
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
            return {fPadContextClearArmed ? "CONFIRM CLEAR" : "CLEAR PAD",
                    enabled, true};
        case PadMenuAction::count:
            return {};
        }
        return {};
    }

    void invokePadMenuAction(const PadMenuEntry entry)
    {
        const PadMenuAction action = entry.action;
        if (!padMenuActionEnabled(action))
            return;
        switch (action) {
        case PadMenuAction::editSample: {
            const int pad = fPadContextTarget;
            closePadContextMenu();
            beginSampleEditor(pad);
            return;
        }
        case PadMenuAction::color:
            openPadColorMenu();
            return;
        case PadMenuAction::copy:
            requestPadClipboard(PadClipboardAction::copy);
            return;
        case PadMenuAction::paste:
            requestPadClipboard(PadClipboardAction::paste);
            return;
        case PadMenuAction::adjustCutPoints: {
            const int pad = fPadContextTarget;
            closePadContextMenu();
            fChopEditor.open(pad, globalPad(0), visiblePadCount());
            return;
        }
        case PadMenuAction::splitSample: {
            const int pad = fPadContextTarget;
            closePadContextMenu();
            requestSplitPlan(pad);
            return;
        }
        case PadMenuAction::collapseGap:
            if (!fPadContextCollapseArmed) {
                fPadContextCollapseArmed = true;
                fPadContextCollapseDeadline = std::chrono::steady_clock::now() +
                                               kClearConfirmationTimeout;
                setLocalStatus("Click COLLAPSE GAP again to confirm");
            } else {
                requestCollapseGap(fPadContextTarget);
                closePadContextMenu();
            }
            requestRepaint();
            return;
        case PadMenuAction::exportRaw:
            openPadFileDialog(PendingFileDialog::exportRaw);
            return;
        case PadMenuAction::exportProcessed:
            openPadFileDialog(PendingFileDialog::exportProcessed);
            return;
        case PadMenuAction::import:
            openPadFileDialog(PendingFileDialog::import);
            return;
        case PadMenuAction::clear:
            if (!fPadContextClearArmed) {
                fPadContextClearArmed = true;
                fPadContextClearDeadline = std::chrono::steady_clock::now() +
                                           kClearConfirmationTimeout;
                setLocalStatus("Click CLEAR PAD again to confirm");
            } else {
                requestPadClear();
                closePadContextMenu();
                setLocalStatus("Pad cleared");
            }
            requestRepaint();
            return;
        case PadMenuAction::count:
            return;
        }
    }

    void requestSplitPlan(const int pad)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        midichopper::plugin::PadStructureRequest request;
        request.action = PadStructureAction::prepareSplit;
        request.firstPad = static_cast<std::uint32_t>(globalPad(0));
        request.padCount = static_cast<std::uint32_t>(visiblePadCount());
        request.targetPad = static_cast<std::uint32_t>(pad);
        fPendingSplitTarget = pad;
        fPendingSplitFirst = static_cast<int>(request.firstPad);
        fPendingSplitCount = static_cast<int>(request.padCount);
        fPadStructureBusy = true;
        setLocalStatus("Preparing sample split...");
        const auto encoded = encodePadStructureRequest(request);
        setState("pad_structure_request", encoded.c_str());
#else
        static_cast<void>(pad);
#endif
    }

    void requestCollapseGap(const int pad)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        midichopper::plugin::PadStructureRequest request;
        request.action = PadStructureAction::collapse;
        request.firstPad = static_cast<std::uint32_t>(globalPad(0));
        request.padCount = static_cast<std::uint32_t>(visiblePadCount());
        request.targetPad = static_cast<std::uint32_t>(pad);
        fPendingSplitTarget = -1;
        fPendingSplitFirst = -1;
        fPendingSplitCount = 0;
        fPadStructureBusy = true;
        setLocalStatus("Collapsing pad gap...");
        const auto encoded = encodePadStructureRequest(request);
        setState("pad_structure_request", encoded.c_str());
#else
        static_cast<void>(pad);
#endif
    }

    void openPadFileDialog(const PendingFileDialog action)
    {
#if DISTRHO_UI_FILE_BROWSER
        if (fPadContextTarget < 0 || padActionBusy())
            return;
        FileBrowserOptions options;
        const bool saving = action == PendingFileDialog::exportRaw ||
                            action == PendingFileDialog::exportProcessed;
        options.saving = saving;
        char defaultName[40];
        std::snprintf(defaultName, sizeof(defaultName), "anvilsampler-pad-%02d.wav",
                      localPadForGlobalPad(fPadContextTarget) + 1);
        options.defaultName = saving ? defaultName : nullptr;
        options.title = saving ? "Export pad as WAV" : "Import WAV into pad";

        fPendingFileDialog = action;
        fPendingFilePad = fPadContextTarget;
        closePadContextMenu();
        if (!openFileBrowser(options)) {
            fPendingFileDialog = PendingFileDialog::none;
            fPendingFilePad = -1;
            if (saving) {
                fSaveDialogAvailable = false;
                setLocalStatus("Save dialog unavailable; Linux Export requires a desktop portal");
            } else {
                setLocalStatus("Open dialog unavailable");
            }
        }
        requestRepaint();
#else
        static_cast<void>(action);
#endif
    }

    void requestPadClipboard(const PadClipboardAction action)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (fPadContextTarget < 0 || padActionBusy())
            return;
        const int pad = fPadContextTarget;
        fPadClipboardBusy = true;
        fActivePadClipboardAction = action;
        fActivePadClipboardPad = pad;
        closePadContextMenu();
        setLocalStatus(action == PadClipboardAction::copy
            ? "Copying pad..." : "Pasting pad...");
        const std::string request = midichopper::plugin::encodePadClipboardRequest(
            action, static_cast<std::uint32_t>(pad));
        setState("pad_clipboard_request", request.c_str());
        requestRepaint();
#else
        static_cast<void>(action);
#endif
    }

    [[nodiscard]] static bool hasWavFileExtension(const std::string_view path) noexcept
    {
        if (path.size() < 4U)
            return false;
        const auto extension = path.substr(path.size() - 4U);
        return extension[0] == '.' && (extension[1] == 'w' || extension[1] == 'W') &&
               (extension[2] == 'a' || extension[2] == 'A') &&
               (extension[3] == 'v' || extension[3] == 'V');
    }

    [[nodiscard]] static bool hasAnyFileExtension(const std::string_view path) noexcept
    {
        const auto slash = path.find_last_of("/\\");
        const auto dot = path.find_last_of('.');
        return dot != std::string_view::npos && dot + 1U < path.size() &&
               (slash == std::string_view::npos || dot > slash + 1U);
    }

    void requestPadClear()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (fPadContextTarget < 0 || fPadContextTarget >= static_cast<int>(midichopper::kPadCount))
            return;
        char pad[4];
        std::snprintf(pad, sizeof(pad), "%u",
                      static_cast<unsigned int>(fPadContextTarget + 1));
        setState("pad_clear_request", pad);
#endif
    }

    static int clampPad(float value)
    {
        return std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(midichopper::kPadCount - 1));
    }

    int clampLocalPad(const float value) const
    {
        return std::clamp(static_cast<int>(std::lround(value)), 0, visiblePadCount() - 1);
    }

    static int clampBaseNote(const float value)
    {
        return std::clamp(static_cast<int>(std::lround(value)),
            static_cast<int>(parameterRanges::baseMidiNote.minimum),
            static_cast<int>(parameterRanges::baseMidiNote.maximum));
    }

    int visiblePadCount() const noexcept
    {
        return padLayout().visiblePadCount();
    }

    int globalPad(const int localPad) const noexcept
    {
        return fBank * static_cast<int>(midichopper::bankStride(
            static_cast<std::uint8_t>(visiblePadCount()), midiBankMode())) + localPad;
    }

    midichopper::MidiBankMode midiBankMode() const noexcept
    {
        return fMidiBankMode == 0
            ? midichopper::MidiBankMode::SelectedBank : midichopper::MidiBankMode::AllBanks;
    }

    int bankForGlobalPad(const int pad) const noexcept
    {
        return static_cast<int>(midichopper::bankForPad(
            static_cast<std::uint32_t>(std::max(pad, 0)),
            static_cast<std::uint8_t>(visiblePadCount()), midiBankMode()));
    }

    int localPadForGlobalPad(const int pad) const noexcept
    {
        return static_cast<int>(midichopper::localPadInBank(
            static_cast<std::uint32_t>(std::max(pad, 0)),
            static_cast<std::uint8_t>(visiblePadCount()), midiBankMode()));
    }

    int mappedMidiNote(const int pad) const noexcept
    {
        return static_cast<int>(midichopper::midiNoteForPad(
            static_cast<std::uint32_t>(pad), static_cast<std::uint8_t>(fBaseNote),
            midiBankMode()));
    }

    int localPadFromVisualIndex(const int visualIndex) const noexcept
    {
        return padLayout().localIndex(visualIndex);
    }

    bool hasSelectedPad() const noexcept
    {
        return fSelectedPad >= 0 && fSelectedPad < static_cast<int>(midichopper::kPadCount);
    }

    int firstEmptyLocalPad() const noexcept
    {
        for (int localPad = 0; localPad < visiblePadCount(); ++localPad) {
            const char state = fPadState[static_cast<std::size_t>(localPad)];
            if (state == '0' || state == '.')
                return localPad;
        }
        return 0;
    }

    void selectAutomaticArmTarget()
    {
        const int localPad = firstEmptyLocalPad();
        fStartPad = localPad;
        fSelectedPad = globalPad(localPad);
        fHasWaveform = false;
        requestWaveform();
        setParameterValue(kParameterStartPad, static_cast<float>(localPad + 1));
    }

    void restoreLastPlayedSelection()
    {
        if (fLastPlayedPad >= 0 && bankForGlobalPad(fLastPlayedPad) == fBank) {
            fSelectedPad = fLastPlayedPad;
            refreshSelectedWaveform();
            return;
        }
        fSelectedPad = -1;
        fHasWaveform = false;
        fEditorSnapshot.complete();
    }

    void normalizeSelectionForContext()
    {
        if (!hasSelectedPad())
            return;
        const int localPad = std::clamp(localPadForGlobalPad(fSelectedPad),
                                        0, visiblePadCount() - 1);
        if (fEditorMode) {
            selectEditorPad(globalPad(localPad));
            return;
        }
        if (fArm && fCurrentPad < 0) {
            fStartPad = localPad;
            fSelectedPad = globalPad(localPad);
            return;
        }
        if (!fArm && bankForGlobalPad(fSelectedPad) != fBank)
            fSelectedPad = -1;
    }

    sms::ui::PadGridLayout mainPadGrid() const
    {
        return padLayout().grid(uiLayout::mainPadBounds, 10.0f);
    }

    sms::ui::PadGridLayout editorPadGrid() const
    {
        return padLayout().grid(uiLayout::editorPadBounds, 6.0f);
    }

    sms::ui::BankedPadLayout padLayout() const noexcept
    {
        return sms::ui::BankedPadLayout(fLayout);
    }

    sms::ui::waveform::EnvelopeGeometry envelopeGraphGeometry() const noexcept
    {
        return sms::ui::waveform::envelopeGeometry(uiLayout::envelopeGraph, fWaveform,
                                                   fEditorSettings);
    }

    void setLocalStatus(const char* status)
    {
        copyString(fStatus, status);
        requestRepaint();
    }

    void setControlValue(const uint32_t parameter, const float value)
    {
        // Hosts are not required to echo a UI-originated parameter change back
        // to this UI immediately. Keep the visible state responsive, then send
        // the exact same value to the plugin.
        parameterChanged(parameter, value);
        setParameterValue(parameter, value);
    }

    void setControlValueFromWheel(const uint32_t parameter, const float value)
    {
        editParameter(parameter, true);
        setControlValue(parameter, value);
        editParameter(parameter, false);
    }

    void pressPlaybackPad(const int localPad)
    {
        const int pad = globalPad(localPad);
        const int midiNote = mappedMidiNote(pad);
        const int previousMidiNote = fPadPress.press(localPad, midiNote);
        startLocalPlayhead(pad);
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        if (previousMidiNote >= 0)
            sendNote(0, static_cast<uint8_t>(previousMidiNote), 0);
        sendNote(0, static_cast<uint8_t>(midiNote), 127);
#else
        static_cast<void>(previousMidiNote);
#endif
    }

    void togglePlayStop()
    {
        // The DSP decides whether to stop or play from its actual voice state.
        // VST3 hosts do not always deliver UI writes to hidden trigger parameters.
        const bool selected = hasSelectedPad() &&
            bankForGlobalPad(fSelectedPad) == fBank &&
            localPadForGlobalPad(fSelectedPad) < visiblePadCount() &&
            fPadState[static_cast<std::size_t>(localPadForGlobalPad(fSelectedPad))] != '0' &&
            fPadState[static_cast<std::size_t>(localPadForGlobalPad(fSelectedPad))] != '.';
        char request[4];
        std::snprintf(request, sizeof(request), "%d", selected ? fSelectedPad + 1 : 0);
#if DISTRHO_PLUGIN_WANT_STATE
        setState("play_stop_request", request);
#endif
        releasePressedPad();
        if (fAnyPlaybackActive) {
            fAnyPlaybackActive = false;
            stopLocalPlayhead(false);
        } else if (selected) {
            fAnyPlaybackActive = true;
            startLocalPlayhead(fSelectedPad);
        }
        requestRepaint();
    }

    void startLocalPlayhead(const int pad)
    {
        if (fPlaybackIndicator.start(pad, fEditorSettingsPad, fEditorSettings,
                                     std::chrono::steady_clock::now()) && fEditorMode)
            requestRepaint();
    }

    void stopLocalPlayhead(const bool holdFinal)
    {
        fPlaybackIndicator.stop(holdFinal, std::chrono::steady_clock::now());
        if (fEditorMode)
            requestRepaint();
    }

    void updateLocalPlayhead(const std::chrono::steady_clock::time_point now)
    {
        if (fPlaybackIndicator.update(now, fSelectedPad, fHasWaveform, fWaveform,
                fEditorSettings, fMixerSettings, fGlobalTune) && fEditorMode)
            requestRepaint();
    }

    void releasePressedPad()
    {
        const int midiNote = fPadPress.release();
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        if (midiNote >= 0)
            sendNote(0, static_cast<uint8_t>(midiNote), 0);
#else
        static_cast<void>(midiNote);
#endif
    }

    void requestWaveform()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        fWaveformDetail.reset();
        if (!hasSelectedPad())
            return;
        fEditorSnapshot.begin(fSelectedPad);
        sendEditorSnapshotRequest(std::chrono::steady_clock::now());
#endif
    }

    void requestWaveformDetail()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        const auto request = fWaveformDetail.request(
            fChopEditor.active() ? fChopEditor.firstPad() : fSelectedPad,
            static_cast<std::uint32_t>(fChopEditor.active() && !fChopEditor.split() ? 3 : 1),
            fEditorMode || fChopEditor.active(), std::chrono::steady_clock::now());
        if (request) {
            const auto encoded = midichopper::plugin::encodeWaveformDetailRequest(*request);
            setState("waveform_detail_request", encoded.c_str());
        }
#endif
    }

    void sendEditorSnapshotRequest(const std::chrono::steady_clock::time_point now)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (!fEditorSnapshot.pending())
            return;
        const auto encoded = midichopper::plugin::encodeEditorSnapshotRequest(fEditorSnapshot.request());
        setState("waveform_request", encoded.c_str());
        fEditorSnapshotRetryDeadline = now + kEditorSnapshotRetryInterval;
#else
        static_cast<void>(now);
#endif
    }

    void commitEditorSnapshotIfReady()
    {
        if (!fEditorSnapshot.readyFor(fSelectedPad))
            return;
        fWaveform = fEditorSnapshot.waveform();
        fWaveformDetail.reset(fWaveform.frames);
        fHasWaveform = fWaveform.frames != 0U;
        fEditorSettings = fEditorSnapshot.playback();
        fEditorSettingsPad = fSelectedPad;
        fMixerSettings = fEditorSnapshot.mixer();
        fMixerSettingsPad = fSelectedPad;
        fPlaybackIndicator.acceptSettings(fSelectedPad, fEditorSettings,
                                          std::chrono::steady_clock::now());
        fEditorSnapshot.complete();
    }

    void beginSampleEditor(const int targetPad)
    {
        const int pad = clampPad(static_cast<float>(targetPad));
        fEditorMode = true;
        fStatus[0] = '\0';
        if (midichopper::ui::editorPadSelectionChanged(fSelectedPad, pad))
            selectEditorPad(pad);
        else
            refreshSelectedWaveform();
        requestRepaint();
    }

    void sendChopState(const char* const key, const std::string& value) override
    {
#if DISTRHO_PLUGIN_WANT_STATE
        setState(key, value.c_str());
#else
        static_cast<void>(key);
        static_cast<void>(value);
#endif
    }

    void selectChopPad(const int pad) override
    {
        fEditorMode = false;
        fEditorSnapshot.complete();
        fSelectedPad = pad;
    }

    void leaveChopEditor() override
    {
        refreshSelectedWaveform();
    }

    void resetChopViewport(const std::uint64_t frames) override
    {
        fWaveformDetail.reset(frames);
    }

    void setChopStatus(const char* const message) override
    {
        copyString(fStatus, message);
    }

    void setChopStructureBusy(const bool busy) override
    {
        fPadStructureBusy = busy;
    }

    void repaintChopEditor() override
    {
        requestRepaint();
    }

    void refreshSelectedWaveform()
    {
        requestWaveform();
    }

    void selectEditorPad(const int pad)
    {
        const int selectedPad = clampPad(static_cast<float>(pad));
        if (!midichopper::ui::editorPadSelectionChanged(fSelectedPad, selectedPad))
            return;

        fSelectedPad = selectedPad;
        requestWaveform();
        requestRepaint();
    }

    void selectBank(const int bank)
    {
        const int selectedBank = std::clamp(bank, 0, static_cast<int>(midichopper::kBankCount - 1));
        if (selectedBank == fBank || (fArm && fCurrentPad >= 0))
            return;
        if (fChopEditor.active()) {
            setLocalStatus("Apply or Exit before changing bank");
            return;
        }
        const int localPad = hasSelectedPad()
            ? std::clamp(localPadForGlobalPad(fSelectedPad), 0, visiblePadCount() - 1)
            : 0;
        fBank = selectedBank;
        if (fEditorMode)
            selectEditorPad(globalPad(localPad));
        else {
            fSelectedPad = -1;
            fHasWaveform = false;
            fEditorSnapshot.complete();
            if (!fArm)
                fLastPlayedPad = -1;
        }
        setParameterValue(kParameterActiveBank, static_cast<float>(fBank + 1));
        requestRepaint();
    }

    void selectLayout(const int layout)
    {
        if (fArm && fCurrentPad >= 0)
            return;
        const int selectedLayout = std::clamp(layout,
            static_cast<int>(parameterRanges::padLayout.minimum),
            static_cast<int>(parameterRanges::padLayout.maximum));
        if (selectedLayout == fLayout)
            return;
        if (fChopEditor.active()) {
            setLocalStatus("Apply or Exit before changing layout");
            return;
        }
        fLayout = selectedLayout;
        if (fStartPad >= visiblePadCount()) {
            fStartPad = 0;
            setControlValue(kParameterStartPad, 1.0f);
        }
        normalizeSelectionForContext();
        setControlValue(kParameterPadLayout, static_cast<float>(fLayout));
        requestRepaint();
    }

    void selectMidiBankMode(const int mode)
    {
        if (fArm && fCurrentPad >= 0)
            return;
        const int selectedMode = mode == 0 ? 0 : 1;
        if (fChopEditor.active()) {
            setLocalStatus("Apply or Exit before changing MIDI mode");
            return;
        }
        if (selectedMode != fMidiBankMode)
            setControlValue(kParameterMidiBankMode, static_cast<float>(selectedMode));
        requestRepaint();
    }

    void commitEditorSettings()
    {
        fEditorSettingsPad = fSelectedPad;
#if DISTRHO_PLUGIN_WANT_STATE
        const std::string encoded = sms::state::encodeSamplePlaybackSettings(fEditorSettings);
        setState(kPadEditStateKeys[static_cast<std::size_t>(fSelectedPad)].c_str(), encoded.c_str());
#endif
        refreshPendingEditorSnapshot();
    }

    void commitMixerSettings()
    {
        fMixerSettings = sms::dsp::sanitize(fMixerSettings);
        fMixerSettingsPad = fSelectedPad;
#if DISTRHO_PLUGIN_WANT_STATE
        const std::string encoded = sms::state::encodeSampleMixerSettings(fMixerSettings);
        setState(kPadMixerStateKeys[static_cast<std::size_t>(fSelectedPad)].c_str(),
                 encoded.c_str());
#endif
        refreshPendingEditorSnapshot();
    }

    void refreshPendingEditorSnapshot() noexcept
    {
        // A queued snapshot predating this local edit must not overwrite it.
        if (fEditorSnapshot.pending()) {
            fEditorSnapshot.begin(fSelectedPad);
            fEditorSnapshotRetryDeadline = std::chrono::steady_clock::now();
        }
    }

    void beginMixerValueEntry(const sms::ui::InteractiveTarget target)
    {
        if (!midichopper::ui::isMixerValueLabel(target) ||
            target.index < 0 || target.index >= 5)
            return;

        float displayedValue = 0.0f;
        int entryPad = -1;
        if (midichopper::ui::isTarget(target, midichopper::ui::InteractiveType::levelValueLabel)) {
            displayedValue = target.index == 0 ? fMonitorGain : fGain;
        } else if (midichopper::ui::isTarget(
                target, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
            switch (target.index) {
            case 0: displayedValue = fGain; break;
            case 1: displayedValue = fGlobalPan * 100.0f; break;
            case 2: displayedValue = fGlobalTune; break;
            case 3: displayedValue = fGlobalLowpass * 100.0f; break;
            case 4: displayedValue = fGlobalHighpass * 100.0f; break;
            default: return;
            }
        } else {
            switch (target.index) {
            case 0: displayedValue = fMixerSettings.gainDecibels; break;
            case 1: displayedValue = fMixerSettings.pan * 100.0f; break;
            case 2: displayedValue = fMixerSettings.tuneSemitones; break;
            case 3: displayedValue = fMixerSettings.lowpass * 100.0f; break;
            case 4: displayedValue = fMixerSettings.highpass * 100.0f; break;
            default: return;
            }
            entryPad = fSelectedPad;
        }

        fMixerValueEntry.begin(target, entryPad, displayedValue);
        fStatus[0] = '\0';
        // Embedded hosts can leave keyboard focus on their own proxy window
        // after the double-click, so explicitly focus the DGL child view.
        getWindow().focus();
        requestRepaint();
    }

    void commitMixerValueEntry()
    {
        const auto target = fMixerValueEntry.target();
        if (!midichopper::ui::isMixerValueLabel(target) ||
            target.index < 0 || target.index >= 5)
            return;

        float minimum = 0.0f;
        float maximum = 1.0f;
        float displayScale = (target.index == 1 || target.index == 3 ||
                              target.index == 4) ? 0.01f : 1.0f;
        std::uint32_t parameter = kParameterOutputGainDb;
        const bool level = midichopper::ui::isTarget(
            target, midichopper::ui::InteractiveType::levelValueLabel);
        const bool global = level || midichopper::ui::isTarget(
            target, midichopper::ui::InteractiveType::globalMixerValueLabel);
        if (level) {
            minimum = parameterRanges::outputGainDb.minimum;
            maximum = parameterRanges::outputGainDb.maximum;
            displayScale = 1.0f;
            parameter = levelParameter(target.index);
        } else if (global) {
            switch (target.index) {
            case 0:
                minimum = parameterRanges::outputGainDb.minimum;
                maximum = parameterRanges::outputGainDb.maximum;
                parameter = kParameterOutputGainDb;
                break;
            case 1:
                minimum = parameterRanges::globalPan.minimum;
                maximum = parameterRanges::globalPan.maximum;
                parameter = kParameterGlobalPan;
                break;
            case 2:
                minimum = parameterRanges::globalTuneSemitones.minimum;
                maximum = parameterRanges::globalTuneSemitones.maximum;
                parameter = kParameterGlobalTuneSemitones;
                break;
            case 3:
                parameter = kParameterGlobalLowpass;
                break;
            case 4:
                parameter = kParameterGlobalHighpass;
                break;
            default: return;
            }
        } else {
            if (fMixerValueEntry.pad() != fSelectedPad) {
                fMixerValueEntry.cancel();
                requestRepaint();
                return;
            }
            switch (target.index) {
            case 0:
                minimum = sms::dsp::kMinimumSampleGainDecibels;
                maximum = sms::dsp::kMaximumSampleGainDecibels;
                break;
            case 1:
                minimum = sms::dsp::kMinimumSamplePan;
                maximum = sms::dsp::kMaximumSamplePan;
                break;
            case 2:
                minimum = sms::dsp::kMinimumTuneSemitones;
                maximum = sms::dsp::kMaximumTuneSemitones;
                break;
            case 3:
            case 4:
                break;
            default: return;
            }
        }

        const auto value = fMixerValueEntry.value(displayScale, minimum, maximum);
        if (!value) {
            setLocalStatus("Enter a numeric mixer value");
            return;
        }

        fMixerValueEntry.cancel();
        if (global) {
            editParameter(parameter, true);
            setControlValue(parameter, *value);
            editParameter(parameter, false);
        } else {
            switch (target.index) {
            case 0: fMixerSettings.gainDecibels = *value; break;
            case 1: fMixerSettings.pan = *value; break;
            case 2: fMixerSettings.tuneSemitones = *value; break;
            case 3: fMixerSettings.lowpass = *value; break;
            case 4: fMixerSettings.highpass = *value; break;
            default: return;
            }
            commitMixerSettings();
        }
        requestRepaint();
    }

    bool resetMixerControl(const sms::ui::InteractiveTarget clicked)
    {
        if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::levelFader) ||
            midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::levelValueLabel)) {
            setControlValueFromWheel(levelParameter(clicked.index), 0.0f);
            requestRepaint();
            return true;
        }
        if (midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::globalMixerKnob) ||
            midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
            switch (clicked.index) {
            case 0: setControlValue(kParameterOutputGainDb, 0.0f); break;
            case 1: setControlValue(kParameterGlobalPan, 0.0f); break;
            case 2: setControlValue(kParameterGlobalTuneSemitones, 0.0f); break;
            case 3: setControlValue(kParameterGlobalLowpass, 0.0f); break;
            case 4: setControlValue(kParameterGlobalHighpass, 0.0f); break;
            case 5: setControlValue(kParameterGlobalFilterSlope, 1.0f); break;
            case 6: setControlValue(kParameterGlobalDirty, 0.0f); break;
            default: return false;
            }
            requestRepaint();
            return true;
        }
        if (midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::mixerKnob) ||
            midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::mixerValueLabel)) {
            midichopper::ui::resetMixerKnob(fMixerSettings, clicked.index);
            commitMixerSettings();
            requestRepaint();
            return true;
        }
        if (midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::envelopeSlider)) {
            sms::ui::waveform::resetEnvelopeSlider(fEditorSettings, clicked.index);
            commitEditorSettings();
            requestRepaint();
            return true;
        }
        return false;
    }

    void updateGlobalMixerDrag(const float x, const float y)
    {
        if (fGlobalMixerDragIndex == 5) {
            setControlValue(kParameterGlobalFilterSlope,
                midichopper::ui::slopeValueAtX(x, uiLayout::globalMixerKnob(5)));
            return;
        }
        if (fGlobalMixerDragIndex < 0 || fGlobalMixerDragIndex >= 5)
            return;
        float startValue = 0.0f;
        float minimum = 0.0f;
        float maximum = 1.0f;
        float steppedIncrement = 1.0f;
        std::uint32_t parameter = kParameterOutputGainDb;
        switch (fGlobalMixerDragIndex) {
        case 0:
            minimum = parameterRanges::outputGainDb.minimum;
            maximum = parameterRanges::outputGainDb.maximum;
            startValue = fGlobalMixerDragStart.gainDecibels;
            parameter = kParameterOutputGainDb;
            break;
        case 1:
            minimum = parameterRanges::globalPan.minimum;
            maximum = parameterRanges::globalPan.maximum;
            startValue = fGlobalMixerDragStart.pan;
            steppedIncrement = 0.1f;
            parameter = kParameterGlobalPan;
            break;
        case 2:
            minimum = parameterRanges::globalTuneSemitones.minimum;
            maximum = parameterRanges::globalTuneSemitones.maximum;
            startValue = fGlobalMixerDragStart.tuneSemitones;
            parameter = kParameterGlobalTuneSemitones;
            break;
        case 3:
            minimum = 0.0f; maximum = 1.0f;
            startValue = fGlobalMixerDragStart.lowpass;
            steppedIncrement = 0.05f;
            parameter = kParameterGlobalLowpass;
            break;
        case 4:
            minimum = 0.0f; maximum = 1.0f;
            startValue = fGlobalMixerDragStart.highpass;
            steppedIncrement = 0.05f;
            parameter = kParameterGlobalHighpass;
            break;
        default:
            return;
        }
        setControlValue(parameter, midichopper::ui::knobDraggedValue(
            startValue, fGlobalMixerDragStartY, y, minimum, maximum,
            steppedIncrement, fGlobalMixerDragAdjustment));
    }

    bool parseEditorState(const char* const key, const char* const value)
    {
        for (std::size_t pad = 0; pad < kPadEditStateKeys.size(); ++pad)
        {
            if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) != 0)
                continue;
            sms::dsp::SamplePlaybackSettings decoded;
            if (sms::state::decodeSamplePlaybackSettings(value, decoded)) {
                const int decodedPad = static_cast<int>(pad);
                if (!fEditorSnapshot.pending() && decodedPad == fSelectedPad) {
                    fEditorSettings = decoded;
                    fEditorSettingsPad = decodedPad;
                }
                if (!fEditorSnapshot.pending())
                    fPlaybackIndicator.acceptSettings(decodedPad, decoded,
                                                      std::chrono::steady_clock::now());
            }
            return true;
        }
        return false;
    }

    bool parseMixerState(const char* const key, const char* const value)
    {
        for (std::size_t pad = 0; pad < kPadMixerStateKeys.size(); ++pad)
        {
            if (std::strcmp(key, kPadMixerStateKeys[pad].c_str()) != 0)
                continue;
            sms::dsp::SampleMixerSettings decoded;
            if (sms::state::decodeSampleMixerSettings(value, decoded)) {
                const int decodedPad = static_cast<int>(pad);
                if (!fEditorSnapshot.pending() && decodedPad == fSelectedPad) {
                    fMixerSettings = decoded;
                    fMixerSettingsPad = decodedPad;
                }
            }
            return true;
        }
        return false;
    }

    bool parsePadColorState(const char* const key, const char* const value)
    {
        for (std::size_t pad = 0; pad < kPadColorStateKeys.size(); ++pad) {
            if (std::strcmp(key, kPadColorStateKeys[pad].c_str()) != 0)
                continue;
            fPadColors[pad] = midichopper::plugin::decodePadColorIndex(
                value, static_cast<int>(sms::ui::dpf::theme().padColors.size()));
            return true;
        }
        return false;
    }

    void updateMixerDrag(const float x, const float y)
    {
        if (fMixerDragIndex == 5) {
            fMixerSettings.filterSlope = midichopper::ui::slopeValueAtX(
                x, uiLayout::mixerKnob(5));
            commitMixerSettings();
            requestRepaint();
            return;
        }
        if (fMixerDragIndex < 0 || fMixerDragIndex >= 5)
            return;
        float startValue = 0.0f;
        float minimum = 0.0f;
        float maximum = 1.0f;
        float steppedIncrement = 1.0f;
        switch (fMixerDragIndex) {
        case 0:
            startValue = fMixerDragStartSettings.gainDecibels;
            minimum = sms::dsp::kMinimumSampleGainDecibels;
            maximum = sms::dsp::kMaximumSampleGainDecibels;
            break;
        case 1:
            startValue = fMixerDragStartSettings.pan;
            minimum = sms::dsp::kMinimumSamplePan;
            maximum = sms::dsp::kMaximumSamplePan;
            steppedIncrement = 0.1f;
            break;
        case 2:
            startValue = fMixerDragStartSettings.tuneSemitones;
            minimum = sms::dsp::kMinimumTuneSemitones;
            maximum = sms::dsp::kMaximumTuneSemitones;
            break;
        case 3:
            startValue = fMixerDragStartSettings.lowpass;
            steppedIncrement = 0.05f;
            break;
        case 4:
            startValue = fMixerDragStartSettings.highpass;
            steppedIncrement = 0.05f;
            break;
        default:
            return;
        }
        const float adjusted = midichopper::ui::knobDraggedValue(
            startValue, fMixerDragStartY, y, minimum, maximum,
            steppedIncrement, fMixerDragAdjustment);
        switch (fMixerDragIndex) {
        case 0:
            fMixerSettings.gainDecibels = adjusted;
            break;
        case 1:
            fMixerSettings.pan = adjusted;
            break;
        case 2:
            fMixerSettings.tuneSemitones = adjusted;
            break;
        case 3: fMixerSettings.lowpass = adjusted; break;
        case 4: fMixerSettings.highpass = adjusted; break;
        default:
            return;
        }
        fMixerSettings = sms::dsp::sanitize(fMixerSettings);
        commitMixerSettings();
        requestRepaint();
    }

    void updateEditorDrag(const float x, const float y)
    {
        if (fDragTarget == WaveformEditTarget::regionStart ||
            fDragTarget == WaveformEditTarget::regionEnd) {
            sms::ui::waveform::updateRegion(
                fEditorSettings, fDragTarget, x, uiLayout::editorWaveform,
                fWaveformDetail.viewport());
        } else if (fDragTarget >= WaveformEditTarget::attackSlider &&
                   fDragTarget <= WaveformEditTarget::releaseSlider) {
            const int slider = static_cast<int>(fDragTarget) -
                               static_cast<int>(WaveformEditTarget::attackSlider);
            sms::ui::waveform::updateEnvelopeSlider(
                fEditorSettings, fDragTarget, x, uiLayout::editorSlider(slider));
        } else if (fDragTarget >= WaveformEditTarget::attackNode &&
                   fDragTarget <= WaveformEditTarget::releaseNode) {
            sms::ui::waveform::updateEnvelopeNode(
                fEditorSettings, fDragStartSettings, fDragTarget, {x, y},
                {fDragStartX, fDragStartY}, envelopeGraphGeometry());
        }
        commitEditorSettings();
        requestRepaint();
    }

    static void copyString(char (&destination)[160], const char* source)
    {
        std::strncpy(destination, source, sizeof(destination) - 1);
        destination[sizeof(destination) - 1] = '\0';
    }

    template <size_t N>
    static void copyChars(std::array<char, N>& destination, const char* source)
    {
        std::size_t index = 0;
        while (index < N && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
        std::fill(destination.begin() + index, destination.end(), '0');
    }

    static int parsePad(const char* value, int fallback)
    {
        if (value == nullptr || *value == '\0')
            return fallback;
        return clampPad(std::strtof(value, nullptr));
    }

    template <size_t N>
    static void parsePadMask(const char* value, std::array<char, N>& destination)
    {
        destination.fill('0');
        if (value == nullptr)
            return;
        if (std::strncmp(value, "0x", 2) == 0 || std::strncmp(value, "0X", 2) == 0)
        {
            const unsigned long mask = std::strtoul(value + 2, nullptr, 16);
            for (size_t i = 0; i < N && i < 16; ++i)
                destination[i] = (mask & (1UL << i)) != 0 ? '1' : '0';
            return;
        }
        copyChars(destination, value);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidichopperUI)
};

UI* createUI()
{
    return new MidichopperUI();
}

END_NAMESPACE_DISTRHO
