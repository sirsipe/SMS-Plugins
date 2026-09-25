/*
 * SMS-Midichopper - DPF/NanoVG user interface
 *
 * Parameter indices and ranges are shared with the DSP through Parameters.hpp.
 * Drawing is delegated to MidichopperView and reusable Common-UI components;
 * this class owns only host communication and interaction state.
 *
 * State contract used by the sample editor:
 *   pad_edit_01..64  compact non-destructive cut-point and ADSR settings
 *   pad_mix_01..64   independent gain, pan, and tune settings
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
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "MidichopperView.hpp"
#include "PadClipboardProtocol.hpp"
#include "PadFileActionProtocol.hpp"
#include "PadStructureProtocol.hpp"
#include "Parameters.hpp"
#include "PluginUiBridge.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

START_NAMESPACE_DISTRHO

namespace {

using namespace midichopper::plugin;
namespace uiLayout = midichopper::ui::layout;
using WaveformEditTarget = sms::ui::waveform::EditTarget;

inline constexpr auto kClearConfirmationTimeout = std::chrono::seconds(2);
inline constexpr auto kMeterFrameInterval = std::chrono::milliseconds(33);
inline constexpr auto kEditorSnapshotRetryInterval = std::chrono::milliseconds(250);

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

} // namespace

class MidichopperUI final : public sms::ui::dpf::NanoUI
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
          fPressedActionParameter(-1),
          fClearArmed(false),
          fMenuOpen(false),
          fPadContextMenuOpen(false),
          fPadContextTarget(-1),
          fPadContextClearArmed(false),
          fPadContextPointerCaptured(false),
          fEditorMode(false),
          fPlayOnSelect(false),
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
        fChopOffsets.fill(0);
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
        if (!fChopEditorMode)
            return;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fUiBridge != nullptr) {
            fUiBridge->stopUiPreview();
            return;
        }
#endif
        disableChopMidiPreview();
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
            if (std::isfinite(value) && value > 0.0f) {
                fChopPreviewPosition = value;
                fChopPlaying = true;
                fChopPreviewPad = -1;
                if (fChopEditorMode && chopReady()) {
                    const int encodedPad = static_cast<int>(std::floor(value)) - 1;
                    const int originalPad = encodedPad - fChopFirstPad;
                    const double sourceFrame = midichopper::ui::chop::sourceFrameForPlayhead(
                        fChopWaveforms, originalPad, value - std::floor(value));
                    const int displayedPads = fChopSplitMode ? 2 : 3;
                    for (int pad = 0; sourceFrame >= 0.0 && pad < displayedPads; ++pad) {
                        const auto start = midichopper::ui::chop::adjustedStartFrame(
                            fChopWaveforms, fChopOffsets, pad);
                        const auto frames = midichopper::ui::chop::adjustedFrames(
                            fChopWaveforms, fChopOffsets, pad);
                        if (frames != 0U && sourceFrame >= static_cast<double>(start) &&
                            sourceFrame < static_cast<double>(start + frames)) {
                            fChopPreviewPad = pad;
                            break;
                        }
                    }
                }
            } else {
                if (fChopPlaying)
                    fChopPreviewPosition = 0.0f;
                fChopPlaying = false;
                fChopPreviewPad = -1;
            }
            requestRepaint();
            return;
        }
        if (index == kParameterPlaybackPosition)
        {
            if (std::isfinite(value) && value > 0.0f) {
                fPlaybackPosition = value;
                fLocalPlayheadPad = static_cast<int>(std::floor(value)) - 1;
                fLocalPlayheadFraction = value - std::floor(value);
                fLocalPlayheadActive = true;
                fLocalPlayheadTick = std::chrono::steady_clock::now();
                fLocalPlayheadClearDeadline = {};
            } else {
                fPlaybackPosition = 0.0f;
                fLocalPlayheadActive = false;
                fLocalPlayheadPad = -1;
                fLocalPlayheadClearDeadline = {};
            }
            if (fEditorMode)
                requestRepaint();
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
            else if (!isActive && globalPad(localPad) == fLocalPlayheadPad)
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
            if (armed && fChopEditorMode)
                cancelChopEditor();
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
                stopChopPreview();
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
                if (fChopEditorMode)
                    cancelChopEditor();
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
        case kParameterGlobalPan:
            changed = fGlobalPan != value;
            fGlobalPan = value;
            break;
        case kParameterGlobalTuneSemitones:
            changed = fGlobalTune != value;
            fGlobalTune = value;
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
            if (fChopEditorMode)
                cancelChopEditor();
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
            if (!fArm && !fEditorMode && !fChopEditorMode)
                fLastPlayedPad = -1;
            if (fChopEditorMode)
                cancelChopEditor();
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
            changed = !fArm && !fChopEditorMode && pad < midichopper::kPadCount;
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
            sms::audio::WaveformSummary summary;
            if (sms::audio::decodeWaveformSummary(value, summary)) {
                if (fChopEditorMode && !fChopSplitMode && fChopFirstPad >= 0 &&
                    summary.pad >= static_cast<std::uint32_t>(fChopFirstPad) &&
                    summary.pad < static_cast<std::uint32_t>(fChopFirstPad + 3)) {
                    const auto localPad = static_cast<std::size_t>(
                        static_cast<int>(summary.pad) - fChopFirstPad);
                    fChopWaveforms[localPad] = summary;
                    if (static_cast<int>(localPad) == fChopNextWaveform) {
                        ++fChopNextWaveform;
                        fChopWaveformRequestPending = false;
                    }
                    if (fChopNextWaveform >= 3) {
                        if (chopReady()) {
                            fStatus[0] = '\0';
                            updateChopMidiPreview();
                        } else {
                            muteChopMidiPreview();
                            copyString(fStatus,
                                "Non-empty samples must use one sample rate");
                        }
                    }
                }
                if (fEditorSnapshot.accept(summary)) {
                    commitEditorSnapshotIfReady();
                } else if (!fEditorSnapshot.pending() &&
                           summary.pad == static_cast<std::uint32_t>(fSelectedPad)) {
                    fWaveform = summary;
                    fHasWaveform = summary.frames != 0U;
                }
            }
        }
        else if (std::strcmp(key, "chop_status") == 0)
        {
            fChopApplying = false;
            if (std::strcmp(value, "CH1;OK") == 0) {
                stopChopPreview();
                resetChopWaveforms();
                copyString(fStatus, "Cut points applied");
            } else if (std::strncmp(value, "CH1;ERROR;", 10U) == 0) {
                copyString(fStatus, value + 10U);
                updateChopMidiPreview();
            }
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
                    beginSplitEditor(plan);
                } else {
                    cancelSplitPlan(plan.planId);
                    copyString(fStatus, "Split cancelled because pad context changed");
                }
            } else if (std::strcmp(value, "PS1;O;0;C") == 0) {
                fPadStructureBusy = false;
                copyString(fStatus, "Pad gap collapsed");
                if (fEditorMode)
                    refreshSelectedWaveform();
            } else if (const std::string expected = "PS1;O;" +
                           std::to_string(fSplitPlanId) + ";S";
                       fSplitPlanId != 0U && expected == value) {
                fPadStructureBusy = false;
                const int cutTarget = midichopper::ui::chop::postSplitEditorTarget(
                    localPadForGlobalPad(fSelectedPad), visiblePadCount());
                if (cutTarget >= 0)
                    beginChopEditor(globalPad(cutTarget));
                else
                    cancelChopEditor();
                copyString(fStatus, "Sample split applied");
            } else if (std::strncmp(value, "PS1;E;", 6U) == 0) {
                char* idEnd = nullptr;
                const auto errorPlanId = std::strtoull(value + 6U, &idEnd, 10);
                if (idEnd == value + 6U || idEnd == nullptr || *idEnd != ';')
                    return;
                const bool activeSplitError = fChopSplitMode &&
                    errorPlanId == fSplitPlanId;
                const bool pendingError = errorPlanId == 0U && fPadStructureBusy;
                if (!activeSplitError && !pendingError)
                    return;
                fPadStructureBusy = false;
                fPendingSplitTarget = -1;
                fPendingSplitFirst = -1;
                fPendingSplitCount = 0;
                if (activeSplitError) {
                    fSplitPlanId = 0U;
                    fChopSplitMode = false;
                    cancelChopEditor();
                }
                fChopApplying = false;
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
                    if (fChopSplitMode || fChopApplying)
                        cancelChopEditor();
                    fChopApplying = false;
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
        if (fChopEditorMode && fChopNextWaveform >= 0 &&
            fChopNextWaveform < 3 && !fChopWaveformRequestPending) {
            fChopWaveformRequestPending = true;
            requestChopWaveform(fChopNextWaveform);
        }
        if (fEditorSnapshot.pending() && now >= fEditorSnapshotRetryDeadline)
            sendEditorSnapshotRequest(now);
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
                : padMenuActionView(entry.action, collapseLabel);
        }
        const midichopper::ui::ViewState view{
            fArm, fRecordMode, fFixedLength, fPlaybackMode, fMonitor,
            fStartPad, fPreRoll, fBaseNote, fMidiBankMode, fGain, fGlobalPan, fGlobalTune,
            fMaxVoices, fBank, fLayout,
            fSelectedPad, fCurrentPad, fPadPress.pad(), fClearArmed, fMenuOpen,
            fPadContextMenuOpen, fPadContextMenu,
            std::span<const sms::ui::ContextMenuItemView>{contextMenuItems},
            fHover.target(),
            fMixerValueEntryTarget, fMixerValueEntryText.data(),
            fEditorMode, fChopEditorMode, fChopSplitMode, fPlayOnSelect, fHasWaveform,
            fInputLevels, fOutputLevels, fPadState, fPadStatus,
            fEditorSettings, fMixerSettings, fWaveform, fPlaybackPosition,
            std::span<const sms::audio::WaveformSummary>{
                fChopWaveforms.data(), fChopWaveforms.size()},
            std::span<const std::int64_t>{
                fChopOffsets.data(), fChopOffsets.size()},
            fChopFirstPad, fSelectedPad, fChopPreviewPosition, fChopPreviewPad,
            fChopActiveBoundary, chopReady(),
            chopDirty(), fChopApplying, canNavigateChop(-1), canNavigateChop(1), fStatus,
        };
        midichopper::ui::draw(*this, view);
        endLogicalDisplay();

    }

    bool onMouse(const MouseEvent& ev) override
    {
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
            cancelMixerValueEntry();
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
            cancelMixerValueEntry();
            if (fArm || fChopEditorMode)
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
            if (fMixerValueEntryTarget.valid() && clicked != fMixerValueEntryTarget)
                cancelMixerValueEntry();
            if (fHover.update(clicked))
                requestRepaint();
            if (fPadContextMenuOpen)
            {
                fPadContextPointerCaptured = true;
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::padContextItem)) {
                    const auto row = static_cast<std::size_t>(clicked.index);
                    if (row < kPadMenuEntries.size())
                        invokePadMenuAction(kPadMenuEntries[row].action);
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
                fMenuOpen = false;
                requestRepaint();
                return true;
            }
            if (fChopEditorMode)
            {
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopBoundary)) {
                    fChopActiveBoundary = clicked.index;
                    fChopDragStartX = x;
                    fChopDragStartOffset =
                        fChopOffsets[static_cast<std::size_t>(clicked.index)];
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopPadPreview)) {
                    previewChopPad(clicked.index);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopApply)) {
                    if (chopDirty() && !fChopApplying)
                        applyChops();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopPrevious)) {
                    navigateChopEditor(-1);
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopExit)) {
                    if (!fChopApplying)
                        cancelChopEditor();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopNext)) {
                    navigateChopEditor(1);
                    return true;
                }
                return false;
            }
            if (fEditorMode)
            {
                const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::mixerValueLabel)) {
                    if (clicked == fMixerValueEntryTarget)
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
                    clicked, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
                if (clicked == fMixerValueEntryTarget)
                    return true;
                if (fDoubleClick.press(clicked, {x, y}, static_cast<std::uint64_t>(now))) {
                    fGlobalMixerDragIndex = -1;
                    beginMixerValueEntry(clicked);
                } else {
                    fGlobalMixerDragIndex = clicked.index;
                    fGlobalMixerDragStartY = y;
                    fGlobalMixerDragStart = {fGain, fGlobalPan, fGlobalTune};
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
                    setLocalStatus("Press any pad to start");
                }
                else
                {
                    pressPlaybackPad(pad);
                }
                requestRepaint();
                return true;
            }

            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::playMode))
            {
                setControlValue(kParameterMode, 0.0f);
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::armMode))
            {
                setControlValue(kParameterMode, 1.0f);
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
                const float t = normalizedX(x, uiLayout::fixedLength);
                setControlValue(kParameterFixedLengthSeconds,
                    parameterRanges::fixedLengthSeconds.minimum + t *
                    (parameterRanges::fixedLengthSeconds.maximum -
                     parameterRanges::fixedLengthSeconds.minimum));
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
                const float t = normalizedX(x, uiLayout::voiceLimit);
                setControlValue(kParameterMaxVoices,
                    parameterRanges::maxVoices.minimum + static_cast<float>(std::lround(
                        t * (parameterRanges::maxVoices.maximum -
                             parameterRanges::maxVoices.minimum))));
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::preRoll))
            {
                const float t = normalizedX(x, uiLayout::preRoll(fRecordMode >= 0.5f));
                setControlValue(kParameterPreRollMs,
                                t * parameterRanges::preRollMs.maximum);
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
                fGlobalMixerDragStart = {fGain, fGlobalPan, fGlobalTune};
                fGlobalMixerDragAdjustment = knobAdjustment(ev.mod);
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::finalizeAction))
            {
                setParameterValue(kParameterFinalize, 1.0f);
                fPressedActionParameter = kParameterFinalize;
                setLocalStatus("Chop finalized");
                return true;
            }
            if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::undoAction))
            {
                setParameterValue(kParameterUndo, 1.0f);
                fPressedActionParameter = kParameterUndo;
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
                    setParameterValue(kParameterClearAll, 1.0f);
                    fPressedActionParameter = kParameterClearAll;
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
        else if (fChopEditorMode && fChopActiveBoundary >= 0)
        {
            fChopActiveBoundary = -1;
            requestRepaint();
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
            fGlobalMixerDragIndex = -1;
            requestRepaint();
            return true;
        }
        else if (fPadPress.pad() >= 0)
        {
            releasePressedPad();
            requestRepaint();
            return true;
        }
        else if (fPressedActionParameter >= 0)
        {
            setParameterValue(static_cast<uint32_t>(fPressedActionParameter), 0.0f);
            fPressedActionParameter = -1;
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX() - uiLayout::contentOffsetX;
        const float y = position.getY();
        if (fChopEditorMode && fChopActiveBoundary >= 0) {
            const auto boundary = static_cast<std::size_t>(fChopActiveBoundary);
            const auto total = midichopper::ui::chop::totalFrames(fChopWaveforms);
            const auto delta = static_cast<std::int64_t>(std::llround(
                (x - fChopDragStartX) * static_cast<double>(total) /
                uiLayout::chopWaveform.width));
            const auto requested = fChopDragStartOffset + delta;
            fChopOffsets[boundary] = clampChopBoundaryOffset(
                fChopActiveBoundary, requested);
            updateChopMidiPreview();
            fStatus[0] = '\0';
            requestRepaint();
            return true;
        }
        if (fEditorMode && fDragTarget != WaveformEditTarget::none) {
            updateEditorDrag(x, y);
            return true;
        }
        if (fEditorMode && fMixerDragIndex >= 0) {
            updateMixerDrag(y);
            return true;
        }
        if (fGlobalMixerDragIndex >= 0) {
            updateGlobalMixerDrag(y);
            return true;
        }
        const auto hovered = resolveInteractiveTarget(x, y);
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
        }
        if (delta == 0.0f || !std::isfinite(delta))
            return false;

        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::chopBoundary)) {
            const auto boundary = static_cast<std::size_t>(hovered.index);
            const auto requested = midichopper::ui::chop::wheelAdjustedBoundaryOffset(
                fChopWaveforms, fChopOffsets, hovered.index, delta,
                uiLayout::chopWaveform);
            fChopOffsets[boundary] = clampChopBoundaryOffset(hovered.index, requested);
            updateChopMidiPreview();
            fStatus[0] = '\0';
            requestRepaint();
            return true;
        }
        if (midichopper::ui::isTarget(
                hovered, midichopper::ui::InteractiveType::regionHandle)) {
            sms::ui::waveform::adjustRegionByWheel(
                fEditorSettings, static_cast<WaveformEditTarget>(hovered.index), delta,
                fWaveform.frames, uiLayout::editorWaveform);
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
        if (fMixerValueEntryTarget.valid()) {
            if (!ev.press)
                return true;
            if (ev.key == DGL_NAMESPACE::kKeyEscape) {
                cancelMixerValueEntry();
                requestRepaint();
            } else if (ev.key == DGL_NAMESPACE::kKeyEnter) {
                commitMixerValueEntry();
            } else if (ev.key == DGL_NAMESPACE::kKeyBackspace) {
                if (fMixerValueEntryReplaceOnType) {
                    fMixerValueEntryLength = 0U;
                    fMixerValueEntryReplaceOnType = false;
                } else if (fMixerValueEntryLength > 0U) {
                    --fMixerValueEntryLength;
                }
                fMixerValueEntryText[fMixerValueEntryLength] = '\0';
                requestRepaint();
            } else if (ev.key == DGL_NAMESPACE::kKeyDelete) {
                fMixerValueEntryLength = 0U;
                fMixerValueEntryText[0] = '\0';
                fMixerValueEntryReplaceOnType = false;
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
        if (fChopEditorMode && !fChopApplying) {
            cancelChopEditor();
            return true;
        }
        return false;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!fMixerValueEntryTarget.valid())
            return false;
        if (ev.character > 0x7fU)
            return true;
        const char character = static_cast<char>(ev.character);
        const bool digit = character >= '0' && character <= '9';
        const bool decimalPoint = character == '.';
        const bool sign = character == '+' || character == '-';
        if (!digit && !decimalPoint && !sign)
            return true;

        if (fMixerValueEntryReplaceOnType) {
            fMixerValueEntryLength = 0U;
            fMixerValueEntryText[0] = '\0';
            fMixerValueEntryReplaceOnType = false;
        }
        const std::string_view current{
            fMixerValueEntryText.data(), fMixerValueEntryLength};
        if ((decimalPoint && current.find('.') != std::string_view::npos) ||
            (sign && fMixerValueEntryLength != 0U) ||
            fMixerValueEntryLength + 1U >= fMixerValueEntryText.size())
            return true;

        fMixerValueEntryText[fMixerValueEntryLength++] = character;
        fMixerValueEntryText[fMixerValueEntryLength] = '\0';
        requestRepaint();
        return true;
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
    float fGlobalPan;
    float fGlobalTune;
    int fMaxVoices;
    int fBank;
    int fLayout;
    int fSelectedPad;
    int fLastPlayedPad;
    int fCurrentPad;
    midichopper::ui::PadPressTracker fPadPress;
    int fPressedActionParameter;
    bool fClearArmed;
    std::chrono::steady_clock::time_point fClearDeadline{};
    bool fMenuOpen;
    bool fPadContextMenuOpen;
    int fPadContextTarget;
    sms::ui::ContextMenuGeometry fPadContextMenu;
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
    bool fChopEditorMode = false;
    bool fChopSplitMode = false;
    std::uint64_t fSplitPlanId = 0U;
    bool fPlayOnSelect;
    WaveformEditTarget fDragTarget;
    int fMixerDragIndex;
    float fMixerDragStartY = 0.0f;
    sms::dsp::SampleMixerSettings fMixerDragStartSettings{};
    midichopper::ui::KnobAdjustment fMixerDragAdjustment =
        midichopper::ui::KnobAdjustment::normal;
    int fGlobalMixerDragIndex;
    float fGlobalMixerDragStartY = 0.0f;
    sms::dsp::SampleMixerSettings fGlobalMixerDragStart{};
    midichopper::ui::KnobAdjustment fGlobalMixerDragAdjustment =
        midichopper::ui::KnobAdjustment::normal;
    midichopper::ui::DoubleClickTracker fDoubleClick;
    sms::ui::InteractiveTarget fMixerValueEntryTarget{};
    std::array<char, 24> fMixerValueEntryText{};
    std::size_t fMixerValueEntryLength = 0U;
    int fMixerValueEntryPad = -1;
    bool fMixerValueEntryReplaceOnType = false;
    float fDragStartX;
    float fDragStartY;
    bool fHasWaveform;
    midichopper::ui::EditorSnapshotCollector fEditorSnapshot;
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
    sms::dsp::SamplePlaybackSettings fEditorSettings{};
    int fEditorSettingsPad = -1;
    sms::dsp::SamplePlaybackSettings fDragStartSettings{};
    sms::dsp::SampleMixerSettings fMixerSettings{};
    int fMixerSettingsPad = -1;
    sms::audio::WaveformSummary fWaveform{};
    std::array<sms::audio::WaveformSummary, midichopper::ui::chop::kPadCount> fChopWaveforms{};
    std::array<std::int64_t, midichopper::ui::chop::kBoundaryCount> fChopOffsets{};
    int fChopFirstPad = -1;
    int fChopNextWaveform = -1;
    bool fChopWaveformRequestPending = false;
    int fChopActiveBoundary = -1;
    float fChopDragStartX = 0.0f;
    std::int64_t fChopDragStartOffset = 0;
    bool fChopPlaying = false;
    int fChopPreviewPad = -1;
    bool fChopApplying = false;
    float fChopPreviewPosition = 0.0f;
    float fPlaybackPosition = 0.0f;
    bool fLocalPlayheadActive = false;
    bool fLocalPlayheadAwaitingSettings = false;
    int fLocalPlayheadPad = -1;
    float fLocalPlayheadFraction = 0.0f;
    std::chrono::steady_clock::time_point fLocalPlayheadTick{};
    std::chrono::steady_clock::time_point fLocalPlayheadStarted{};
    std::chrono::steady_clock::time_point fLocalPlayheadClearDeadline{};
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
        context.chopEditorMode = fChopEditorMode;
        context.chopSplitMode = fChopSplitMode;
        context.menuOpen = fMenuOpen;
        context.padContextMenuOpen = fPadContextMenuOpen;
        context.armed = fArm;
        context.fixedCapture = fRecordMode >= 0.5f;
        context.captureActive = fArm && fCurrentPad >= 0;
        context.chopReady = chopReady();
        context.chopApplying = fChopApplying;
        context.chopApplyEnabled = chopReady() && chopDirty() && !fChopApplying;
        context.chopPreviousEnabled = canNavigateChop(-1) && !fChopApplying;
        context.chopExitEnabled = !fChopApplying;
        context.chopNextEnabled = canNavigateChop(1) && !fChopApplying;
        context.padLayout = fLayout;
        context.padContextMenu = fPadContextMenu;
        context.padContextMenuEnabled = menuEnabled;
        context.editorSettings = &fEditorSettings;
        context.envelope = &envelope;
        context.chopWaveforms = fChopWaveforms;
        context.chopOffsets = fChopOffsets;
        return midichopper::ui::interactiveTargetAt({x, y}, context);
    }

    static float normalizedX(const float x, const sms::ui::Rect bounds) noexcept
    {
        return std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
    }

    [[nodiscard]] std::int64_t clampChopBoundaryOffset(
        const int boundary, const std::int64_t requested) const noexcept
    {
        const auto clamped = midichopper::ui::chop::clampBoundaryOffset(
            fChopWaveforms, fChopOffsets, boundary, requested);
        if (!fChopSplitMode || boundary != 0 || fChopWaveforms[0].frames < 2U)
            return clamped;
        const auto original = static_cast<std::int64_t>(fChopWaveforms[0].frames);
        return std::clamp(clamped, 1 - original, std::int64_t{-1});
    }

    void openPadContextMenu(const int pad, const sms::ui::Point anchor)
    {
        fPadContextTarget = pad;
        fPadContextMenu = sms::ui::ContextMenuGeometry(
            anchor, std::span<const sms::ui::ContextMenuItemKind>{kPadMenuItemKinds},
            uiLayout::contentBounds);
        static_cast<void>(fHover.clear());
        fPadContextClearArmed = false;
        fPadContextCollapseArmed = false;
        fPadContextMenuOpen = true;
    }

    void closePadContextMenu() noexcept
    {
        fPadContextMenuOpen = false;
        fPadContextTarget = -1;
        static_cast<void>(fHover.clear());
        fPadContextClearArmed = false;
        fPadContextCollapseArmed = false;
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
        const PadMenuAction action, const char* const collapseLabel) const noexcept
    {
        const bool enabled = padMenuActionEnabled(action);
        switch (action) {
        case PadMenuAction::editSample:
            return {"EDIT SAMPLE", enabled, false};
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

    void invokePadMenuAction(const PadMenuAction action)
    {
        if (!padMenuActionEnabled(action))
            return;
        switch (action) {
        case PadMenuAction::editSample: {
            const int pad = fPadContextTarget;
            closePadContextMenu();
            beginSampleEditor(pad);
            return;
        }
        case PadMenuAction::copy:
            requestPadClipboard(PadClipboardAction::copy);
            return;
        case PadMenuAction::paste:
            requestPadClipboard(PadClipboardAction::paste);
            return;
        case PadMenuAction::adjustCutPoints: {
            const int pad = fPadContextTarget;
            closePadContextMenu();
            beginChopEditor(pad);
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
        char defaultName[32];
        std::snprintf(defaultName, sizeof(defaultName), "midichopper-pad-%02d.wav",
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

    void startLocalPlayhead(const int pad)
    {
        if (pad < 0 || pad >= static_cast<int>(midichopper::kPadCount))
            return;
        const auto now = std::chrono::steady_clock::now();
        if (fLocalPlayheadActive && fLocalPlayheadPad == pad &&
            now - fLocalPlayheadStarted < std::chrono::milliseconds(150))
            return;
        fLocalPlayheadPad = pad;
        fLocalPlayheadAwaitingSettings = fEditorSettingsPad != pad;
        fLocalPlayheadFraction = fLocalPlayheadAwaitingSettings
            ? 0.0f : sms::dsp::sanitize(fEditorSettings).start;
        fLocalPlayheadActive = true;
        fLocalPlayheadTick = now;
        fLocalPlayheadStarted = now;
        fLocalPlayheadClearDeadline = {};
        fPlaybackPosition = static_cast<float>(pad + 1) + fLocalPlayheadFraction;
        if (fEditorMode)
            requestRepaint();
    }

    void stopLocalPlayhead(const bool holdFinal)
    {
        fLocalPlayheadActive = false;
        fLocalPlayheadAwaitingSettings = false;
        if (holdFinal && fLocalPlayheadPad >= 0) {
            fLocalPlayheadFraction = std::min(fLocalPlayheadFraction, 0.985f);
            fPlaybackPosition = static_cast<float>(fLocalPlayheadPad + 1) +
                                fLocalPlayheadFraction;
            fLocalPlayheadClearDeadline = std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds(100);
        } else {
            fLocalPlayheadPad = -1;
            fPlaybackPosition = 0.0f;
            fLocalPlayheadClearDeadline = {};
        }
        if (fEditorMode)
            requestRepaint();
    }

    void updateLocalPlayhead(const std::chrono::steady_clock::time_point now)
    {
        if (!fLocalPlayheadActive) {
            if (fLocalPlayheadClearDeadline.time_since_epoch().count() != 0 &&
                now >= fLocalPlayheadClearDeadline)
                stopLocalPlayhead(false);
            return;
        }
        if (fLocalPlayheadAwaitingSettings || fLocalPlayheadPad != fSelectedPad ||
            !fHasWaveform || fWaveform.frames == 0U || fWaveform.sampleRate <= 1.0) {
            fLocalPlayheadTick = now;
            return;
        }
        const double elapsed = std::chrono::duration<double>(now - fLocalPlayheadTick).count();
        fLocalPlayheadTick = now;
        const auto playback = sms::dsp::sanitize(fEditorSettings);
        const auto mixer = sms::dsp::sanitize(fMixerSettings);
        const double tuneRatio = std::exp2(
            static_cast<double>(mixer.tuneSemitones + fGlobalTune) / 12.0);
        fLocalPlayheadFraction = sms::ui::waveform::advancePlaybackFraction(
            fLocalPlayheadFraction, elapsed, fWaveform.frames, fWaveform.sampleRate,
            tuneRatio, playback.end);
        fPlaybackPosition = static_cast<float>(fLocalPlayheadPad + 1) +
                            fLocalPlayheadFraction;
        if (fLocalPlayheadFraction >= playback.end - 1.0e-6f) {
            stopLocalPlayhead(true);
            return;
        }
        if (fEditorMode)
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
        if (!hasSelectedPad())
            return;
        fEditorSnapshot.begin(fSelectedPad);
        sendEditorSnapshotRequest(std::chrono::steady_clock::now());
#endif
    }

    void sendEditorSnapshotRequest(const std::chrono::steady_clock::time_point now)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (!fEditorSnapshot.pending())
            return;
        char pad[12];
        std::snprintf(pad, sizeof(pad), "%d", fEditorSnapshot.pad());
        setState("waveform_request", pad);
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
        fHasWaveform = fWaveform.frames != 0U;
        fEditorSettings = fEditorSnapshot.playback();
        fEditorSettingsPad = fSelectedPad;
        fMixerSettings = fEditorSnapshot.mixer();
        fMixerSettingsPad = fSelectedPad;
        if (fLocalPlayheadAwaitingSettings && fLocalPlayheadPad == fSelectedPad) {
            fLocalPlayheadFraction = fEditorSettings.start;
            fPlaybackPosition = static_cast<float>(fSelectedPad + 1) +
                                fEditorSettings.start;
            fLocalPlayheadAwaitingSettings = false;
            fLocalPlayheadTick = std::chrono::steady_clock::now();
        }
        fEditorSnapshot.complete();
    }

    [[nodiscard]] bool chopDirty() const noexcept
    {
        if (fChopSplitMode)
            return chopReady();
        return std::any_of(fChopOffsets.begin(), fChopOffsets.end(),
            [](const std::int64_t offset) { return offset != 0; });
    }

    [[nodiscard]] bool chopReady() const noexcept
    {
        return midichopper::ui::chop::ready(fChopWaveforms);
    }

    [[nodiscard]] bool canNavigateChop(const int direction) const noexcept
    {
        if (!fChopEditorMode || direction == 0)
            return false;
        return midichopper::ui::chop::navigationTarget(
            localPadForGlobalPad(fSelectedPad), direction, visiblePadCount()) >= 0;
    }

    void beginSampleEditor(const int targetPad)
    {
        const int pad = clampPad(static_cast<float>(targetPad));
        fEditorMode = true;
        fChopEditorMode = false;
        fStatus[0] = '\0';
        if (midichopper::ui::editorPadSelectionChanged(fSelectedPad, pad))
            selectEditorPad(pad);
        else
            refreshSelectedWaveform();
        requestRepaint();
    }

    void resetChopWaveforms()
    {
        fChopWaveforms.fill({});
        fChopOffsets.fill(0);
        fChopNextWaveform = 0;
        fChopWaveformRequestPending = false;
        fChopActiveBoundary = -1;
        fChopPreviewPosition = 0.0f;
        fChopPreviewPad = -1;
    }

    void beginChopEditor(const int targetPad)
    {
        const int localPad = localPadForGlobalPad(targetPad);
        if (targetPad < 0 || localPad <= 0 || localPad + 1 >= visiblePadCount())
            return;
        stopChopPreview();
        fEditorMode = false;
        fChopEditorMode = true;
        fChopSplitMode = false;
        fSplitPlanId = 0U;
        fEditorSnapshot.complete();
        fSelectedPad = targetPad;
        fChopFirstPad = targetPad - 1;
        resetChopWaveforms();
        fChopApplying = false;
        fStatus[0] = '\0';
        muteChopMidiPreview();
        requestRepaint();
    }

    void beginSplitEditor(const midichopper::plugin::SplitPlanReady& plan)
    {
        if (plan.targetPad >= midichopper::kPadCount ||
            plan.emptyPad <= plan.targetPad || plan.emptyPad >= midichopper::kPadCount ||
            plan.waveform.pad != plan.targetPad || plan.waveform.frames < 2U)
            return;
        stopChopPreview();
        fEditorMode = false;
        fChopEditorMode = true;
        fChopSplitMode = true;
        fEditorSnapshot.complete();
        fSelectedPad = static_cast<int>(plan.targetPad);
        fChopFirstPad = static_cast<int>(plan.targetPad);
        fSplitPlanId = plan.planId;
        resetChopWaveforms();
        fChopWaveforms[0] = plan.waveform;
        fChopWaveforms[1] = {plan.targetPad + 1U, 0U,
                             plan.waveform.sampleRate, {}, {}};
        fChopWaveforms[2] = {plan.targetPad + 1U, 0U,
                             plan.waveform.sampleRate, {}, {}};
        const auto midpoint = static_cast<std::int64_t>(plan.waveform.frames / 2U);
        fChopOffsets[0] = midpoint - static_cast<std::int64_t>(plan.waveform.frames);
        fChopNextWaveform = 3;
        fChopWaveformRequestPending = false;
        fChopApplying = false;
        fStatus[0] = '\0';
        updateChopMidiPreview();
        requestRepaint();
    }

    void navigateChopEditor(const int direction)
    {
        if (fChopApplying || !canNavigateChop(direction))
            return;
        const int nextLocalPad = midichopper::ui::chop::navigationTarget(
            localPadForGlobalPad(fSelectedPad), direction, visiblePadCount());
        if (fChopSplitMode)
            cancelSplitPlan(fSplitPlanId);
        beginChopEditor(globalPad(nextLocalPad));
    }

    void cancelSplitPlan(const std::uint64_t planId)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (planId != 0U) {
            midichopper::plugin::PadStructureRequest request;
            request.action = PadStructureAction::cancelSplit;
            request.planId = planId;
            const auto encoded = encodePadStructureRequest(request);
            setState("pad_structure_request", encoded.c_str());
        }
#else
        static_cast<void>(planId);
#endif
    }

    void cancelChopEditor()
    {
        if (fChopSplitMode)
            cancelSplitPlan(fSplitPlanId);
        stopChopPreview();
        disableChopMidiPreview();
        fChopEditorMode = false;
        fChopSplitMode = false;
        fSplitPlanId = 0U;
        fChopFirstPad = -1;
        fChopNextWaveform = -1;
        fChopWaveformRequestPending = false;
        fChopActiveBoundary = -1;
        fChopApplying = false;
        fStatus[0] = '\0';
        refreshSelectedWaveform();
        requestRepaint();
    }

    void requestChopWaveform(const int padInEditor)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (fChopFirstPad < 0 || padInEditor < 0 || padInEditor >= 3)
            return;
        char pad[12];
        std::snprintf(pad, sizeof(pad), "%d", fChopFirstPad + padInEditor);
        setState("waveform_request", pad);
#else
        static_cast<void>(padInEditor);
#endif
    }

    void previewChopPad(const int padInEditor)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (!chopReady() || padInEditor < 0 || padInEditor >= 3) {
            setLocalStatus("Non-empty samples must use one sample rate");
            return;
        }
        const auto start = midichopper::ui::chop::adjustedStartFrame(
            fChopWaveforms, fChopOffsets, padInEditor);
        const auto frames = midichopper::ui::chop::adjustedFrames(
            fChopWaveforms, fChopOffsets, padInEditor);
        if (frames == 0U)
            return;
        const auto end = start + frames;
        const auto request = midichopper::plugin::encodeChopPreviewRequest(
            {true, static_cast<std::uint32_t>(fChopFirstPad),
             fChopSplitMode ? 1U : 3U, start, end});
        setState("chop_preview_request", request.c_str());
        fChopPlaying = true;
        fChopPreviewPad = padInEditor;
        requestRepaint();
#else
        static_cast<void>(padInEditor);
#endif
    }

    void stopChopPreview()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        const auto request = midichopper::plugin::encodeChopPreviewRequest(
            {false, static_cast<std::uint32_t>(std::max(fChopFirstPad, 0)),
             fChopSplitMode ? 1U : 3U, 0U, 0U});
        setState("chop_preview_request", request.c_str());
#endif
        fChopPlaying = false;
        fChopPreviewPad = -1;
        requestRepaint();
    }

    void updateChopMidiPreview()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (!fChopEditorMode || fChopApplying || fChopFirstPad < 0 || !chopReady()) {
            if (fChopEditorMode && fChopFirstPad >= 0)
                muteChopMidiPreview();
            else
                disableChopMidiPreview();
            return;
        }
        midichopper::plugin::ChopMidiPreviewRequest request;
        request.active = true;
        request.firstPad = static_cast<std::uint32_t>(fChopFirstPad);
        request.sourcePadCount = fChopSplitMode ? 1U : 3U;
        request.previewPadCount = fChopSplitMode ? 2U : 3U;
        for (std::uint32_t pad = 0; pad < request.previewPadCount; ++pad) {
            const auto localPad = static_cast<int>(pad);
            request.sourceFrames[pad] = midichopper::ui::chop::adjustedStartFrame(
                fChopWaveforms, fChopOffsets, localPad);
            request.sourceEndFrames[pad] = request.sourceFrames[pad] +
                midichopper::ui::chop::adjustedFrames(
                    fChopWaveforms, fChopOffsets, localPad);
        }
        const auto encoded = midichopper::plugin::encodeChopMidiPreviewRequest(request);
        setState("chop_midi_preview", encoded.c_str());
#endif
    }

    void muteChopMidiPreview()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (fChopFirstPad < 0)
            return;
        midichopper::plugin::ChopMidiPreviewRequest request;
        request.active = true;
        request.firstPad = static_cast<std::uint32_t>(fChopFirstPad);
        request.sourcePadCount = fChopSplitMode ? 1U : 3U;
        const auto encoded = midichopper::plugin::encodeChopMidiPreviewRequest(request);
        setState("chop_midi_preview", encoded.c_str());
#endif
    }

    void disableChopMidiPreview()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        const auto encoded = midichopper::plugin::encodeChopMidiPreviewRequest({});
        setState("chop_midi_preview", encoded.c_str());
#endif
    }

    void applyChops()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (!chopReady()) {
            setLocalStatus("Non-empty samples must use one sample rate");
            return;
        }
        if (fChopSplitMode) {
            const auto splitFrame = midichopper::ui::chop::adjustedBoundary(
                fChopWaveforms, fChopOffsets, 0);
            if (fSplitPlanId == 0U || splitFrame <= 0 ||
                splitFrame >= static_cast<std::int64_t>(fChopWaveforms[0].frames)) {
                setLocalStatus("Both split halves must contain audio");
                return;
            }
            midichopper::plugin::PadStructureRequest request;
            request.action = PadStructureAction::applySplit;
            request.planId = fSplitPlanId;
            request.splitFrame = static_cast<std::uint32_t>(splitFrame);
            stopChopPreview();
            muteChopMidiPreview();
            fChopApplying = true;
            fPadStructureBusy = true;
            setLocalStatus("Applying sample split...");
            const auto encoded = encodePadStructureRequest(request);
            setState("pad_structure_request", encoded.c_str());
            return;
        }
        midichopper::plugin::ChopApplyRequest request;
        request.firstPad = static_cast<std::uint32_t>(fChopFirstPad);
        request.padCount = 3U;
        for (int boundary = 0; boundary < 2; ++boundary)
            request.boundaryOffsets[static_cast<std::size_t>(boundary)] =
                fChopOffsets[static_cast<std::size_t>(boundary)];
        stopChopPreview();
        muteChopMidiPreview();
        fChopApplying = true;
        setLocalStatus("Applying chop boundaries...");
        const auto encoded = midichopper::plugin::encodeChopApplyRequest(request);
        setState("chop_apply_request", encoded.c_str());
#endif
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
        if (fChopEditorMode) {
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
        if (fChopEditorMode) {
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
        if (fChopEditorMode) {
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
    }

    void beginMixerValueEntry(const sms::ui::InteractiveTarget target)
    {
        if (!midichopper::ui::isMixerValueLabel(target) ||
            target.index < 0 || target.index >= 3)
            return;

        float displayedValue = 0.0f;
        if (midichopper::ui::isTarget(
                target, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
            switch (target.index) {
            case 0: displayedValue = fGain; break;
            case 1: displayedValue = fGlobalPan * 100.0f; break;
            case 2: displayedValue = fGlobalTune; break;
            default: return;
            }
            fMixerValueEntryPad = -1;
        } else {
            switch (target.index) {
            case 0: displayedValue = fMixerSettings.gainDecibels; break;
            case 1: displayedValue = fMixerSettings.pan * 100.0f; break;
            case 2: displayedValue = fMixerSettings.tuneSemitones; break;
            default: return;
            }
            fMixerValueEntryPad = fSelectedPad;
        }

        std::snprintf(fMixerValueEntryText.data(), fMixerValueEntryText.size(),
                      "%.6g", displayedValue);
        fMixerValueEntryLength = std::strlen(fMixerValueEntryText.data());
        fMixerValueEntryTarget = target;
        fMixerValueEntryReplaceOnType = true;
        fStatus[0] = '\0';
        requestRepaint();
    }

    void cancelMixerValueEntry() noexcept
    {
        fMixerValueEntryTarget = sms::ui::kNoInteractiveTarget;
        fMixerValueEntryText[0] = '\0';
        fMixerValueEntryLength = 0U;
        fMixerValueEntryPad = -1;
        fMixerValueEntryReplaceOnType = false;
    }

    void commitMixerValueEntry()
    {
        const auto target = fMixerValueEntryTarget;
        if (!midichopper::ui::isMixerValueLabel(target) ||
            target.index < 0 || target.index >= 3)
            return;

        float minimum = 0.0f;
        float maximum = 1.0f;
        float displayScale = target.index == 1 ? 0.01f : 1.0f;
        std::uint32_t parameter = kParameterOutputGainDb;
        const bool global = midichopper::ui::isTarget(
            target, midichopper::ui::InteractiveType::globalMixerValueLabel);
        if (global) {
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
            default: return;
            }
        } else {
            if (fMixerValueEntryPad != fSelectedPad) {
                cancelMixerValueEntry();
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
            default: return;
            }
        }

        const auto value = midichopper::ui::mixerValueFromText(
            {fMixerValueEntryText.data(), fMixerValueEntryLength},
            displayScale, minimum, maximum);
        if (!value) {
            setLocalStatus("Enter a numeric mixer value");
            return;
        }

        cancelMixerValueEntry();
        if (global) {
            editParameter(parameter, true);
            setControlValue(parameter, *value);
            editParameter(parameter, false);
        } else {
            switch (target.index) {
            case 0: fMixerSettings.gainDecibels = *value; break;
            case 1: fMixerSettings.pan = *value; break;
            case 2: fMixerSettings.tuneSemitones = *value; break;
            default: return;
            }
            commitMixerSettings();
        }
        requestRepaint();
    }

    bool resetMixerControl(const sms::ui::InteractiveTarget clicked)
    {
        if (midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::globalMixerKnob) ||
            midichopper::ui::isTarget(
                clicked, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
            switch (clicked.index) {
            case 0: setControlValue(kParameterOutputGainDb, 0.0f); break;
            case 1: setControlValue(kParameterGlobalPan, 0.0f); break;
            case 2: setControlValue(kParameterGlobalTuneSemitones, 0.0f); break;
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

    void updateGlobalMixerDrag(const float y)
    {
        if (fGlobalMixerDragIndex < 0 || fGlobalMixerDragIndex >= 3)
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
                if (fEditorSnapshot.accept(decodedPad, decoded)) {
                    commitEditorSnapshotIfReady();
                } else if (!fEditorSnapshot.pending() && decodedPad == fSelectedPad) {
                    fEditorSettings = decoded;
                    fEditorSettingsPad = decodedPad;
                }
                if (!fEditorSnapshot.pending() && fLocalPlayheadAwaitingSettings &&
                    fLocalPlayheadPad == decodedPad) {
                    fLocalPlayheadFraction = decoded.start;
                    fPlaybackPosition = static_cast<float>(pad + 1U) + decoded.start;
                    fLocalPlayheadAwaitingSettings = false;
                    fLocalPlayheadTick = std::chrono::steady_clock::now();
                }
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
                if (fEditorSnapshot.accept(decodedPad, decoded)) {
                    commitEditorSnapshotIfReady();
                } else if (!fEditorSnapshot.pending() && decodedPad == fSelectedPad) {
                    fMixerSettings = decoded;
                    fMixerSettingsPad = decodedPad;
                }
            }
            return true;
        }
        return false;
    }

    void updateMixerDrag(const float y)
    {
        if (fMixerDragIndex < 0 || fMixerDragIndex >= 3)
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
                fEditorSettings, fDragTarget, x, uiLayout::editorWaveform);
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
