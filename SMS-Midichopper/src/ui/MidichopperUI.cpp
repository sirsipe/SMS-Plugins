/*
 * SMS-Midichopper - DPF/NanoVG user interface
 *
 * Parameter indices and ranges are shared with the DSP through Parameters.hpp.
 * Drawing is delegated to MidichopperView and reusable Common-UI components;
 * this class owns only host communication and interaction state.
 *
 * State contract used by the sample editor:
 *   pad_edit_01..64  compact non-destructive cut-point and ADSR settings
 *   waveform_request selected pad index sent from UI to DSP
 *   waveform_data    compact 128-bin min/max summary returned by DSP
 *   pad_clear_request one-based pad command consumed at an audio block boundary
 *   pad_file_request action, pad, and UTF-8 path sent from UI to DSP
 *   pad_file_busy/status small progress and result messages returned to the UI
 *   pad_clipboard_request internal per-instance Copy or Paste command
 *   chop_apply_request/status transactional rolling-boundary edit and result
 *   chop_preview_request raw allocation-free play/pause/seek command
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
#include "DSP/SamplePlaybackSettings.hpp"
#include "LevelMeter.hpp"
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
#include "Parameters.hpp"

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

enum class PadMenuAction : int {
    copy = 0,
    paste,
    exportRaw,
    exportProcessed,
    import,
    clear,
    count,
};

enum class PendingFileDialog : std::uint8_t {
    none,
    exportRaw,
    exportProcessed,
    import,
};

std::array<std::string, midichopper::kPadCount> makePadEditStateKeys()
{
    std::array<std::string, midichopper::kPadCount> keys;
    for (uint pad = 0; pad < midichopper::kPadCount; ++pad) {
        char key[24];
        std::snprintf(key, sizeof(key), "pad_edit_%02u", pad + 1U);
        keys[pad] = key;
    }
    return keys;
}

const auto kPadEditStateKeys = makePadEditStateKeys();

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
          fDragStartX(0.0f),
          fDragStartY(0.0f),
          fHasWaveform(false),
          fCaptureTargetRequestAlternateHalf(false)
    {
        fPadState.fill('0');
        fPadStatus.fill('0');
        fChopOffsets.fill(0);
        fStatus[0] = '\0';

#ifndef DGL_NO_SHARED_RESOURCES
        loadSharedResources();
#endif
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
                fEditorSettings = {};
                fEditorSettingsPad = completedPad;
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
                fEditorSettings = {};
                fEditorSettingsPad = -1;
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
            } else {
                if (fChopPlaying)
                    fChopPreviewPosition = 0.0f;
                fChopPlaying = false;
            }
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
            fArm = armed;
            fStatus[0] = '\0';
            fSelectedPad = -1;
            fLastPlayedPad = -1;
            if (fArm) {
                fEditorMode = false;
                fChopEditorMode = false;
                stopChopPreview();
                fDragTarget = WaveformEditTarget::none;
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
                    initializeChopEditor();
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
                initializeChopEditor();
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
                initializeChopEditor();
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
            // MIDI performance remains audible in Chop Editor, but it must not
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
                }
                fSelectedPad = fLastPlayedPad;
                refreshSelectedWaveform();
            }
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
            if (selectedPad != fSelectedPad) {
                fEditorSettings = {};
                fEditorSettingsPad = -1;
            }
            fSelectedPad = selectedPad;
            fBank = std::clamp(bankForGlobalPad(fSelectedPad), 0,
                               static_cast<int>(midichopper::kBankCount - 1));
        }
        else if (std::strcmp(key, "status") == 0)
            copyString(fStatus, value);
        else if (std::strcmp(key, "waveform_data") == 0)
        {
            sms::audio::WaveformSummary summary;
            if (sms::audio::decodeWaveformSummary(value, summary)) {
                if (fChopEditorMode && bankForGlobalPad(static_cast<int>(summary.pad)) == fBank) {
                    const int localPad = localPadForGlobalPad(static_cast<int>(summary.pad));
                    if (localPad >= 0 && localPad < visiblePadCount())
                        fChopWaveforms[static_cast<std::size_t>(localPad)] = summary;
                }
                if (summary.pad == static_cast<std::uint32_t>(fSelectedPad)) {
                    fWaveform = summary;
                    fHasWaveform = summary.frames != 0U;
                }
            }
        }
        else if (std::strcmp(key, "chop_status") == 0)
        {
            fChopApplying = false;
            if (std::strcmp(value, "CH1;OK") == 0) {
                copyString(fStatus, "Chops applied — Start, End and ADSR reset");
                initializeChopEditor();
            } else if (std::strncmp(value, "CH1;ERROR;", 10U) == 0) {
                copyString(fStatus, value + 10U);
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
                fEditorSettings = {};
                fEditorSettingsPad = fSelectedPad;
                refreshSelectedWaveform();
            }
            fActiveFileAction = PendingFileDialog::none;
            fActiveFilePad = -1;
        }
        else if (parseEditorState(key, value))
        {
        }
        else
            return;
        requestRepaint();
    }
#endif

    void onUiIdle() override
    {
        const auto now = std::chrono::steady_clock::now();
        if (fClearArmed && now >= fClearDeadline) {
            fClearArmed = false;
            requestRepaint();
        }
        if (fPadContextClearArmed && now >= fPadContextClearDeadline) {
            fPadContextClearArmed = false;
            requestRepaint();
        }
        if (fMeterRepaintPending && now >= fNextMeterRepaint) {
            fMeterRepaintPending = false;
            fNextMeterRepaint = now + kMeterFrameInterval;
            requestRepaint();
        }
        if (fChopEditorMode && fChopNextWaveform >= 0 &&
            fChopNextWaveform < visiblePadCount()) {
            requestChopWaveform(fChopNextWaveform++);
        }
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
        const std::array contextMenuItems{
            sms::ui::ContextMenuItemView{
                "COPY PAD", padCopyEnabled(), false},
            sms::ui::ContextMenuItemView{
                "PASTE PAD", padPasteEnabled(), false},
            sms::ui::ContextMenuItemView{
                "EXPORT WAV...", padExportEnabled(), false},
            sms::ui::ContextMenuItemView{
                "EXPORT PROCESSED...", padProcessedExportEnabled(), false},
            sms::ui::ContextMenuItemView{
                "IMPORT WAV...", !padActionBusy(), false},
            sms::ui::ContextMenuItemView{
                fPadContextClearArmed ? "CONFIRM CLEAR" : "CLEAR PAD",
                !padActionBusy() && padContextTargetOccupied(), true},
        };
        const midichopper::ui::ViewState view{
            fArm, fRecordMode, fFixedLength, fPlaybackMode, fMonitor,
            fStartPad, fPreRoll, fBaseNote, fMidiBankMode, fGain, fMaxVoices, fBank, fLayout,
            fSelectedPad, fCurrentPad, fPadPress.pad(), fClearArmed, fMenuOpen,
            fPadContextMenuOpen, fPadContextMenu,
            std::span<const sms::ui::ContextMenuItemView>{contextMenuItems},
            fHover.target(),
            fEditorMode, fChopEditorMode, fPlayOnSelect, fHasWaveform,
            fInputLevels, fOutputLevels, fPadState, fPadStatus,
            fEditorSettings, fWaveform,
            std::span<const sms::audio::WaveformSummary>{
                fChopWaveforms.data(), static_cast<std::size_t>(visiblePadCount())},
            std::span<const std::int64_t>{
                fChopOffsets.data(), static_cast<std::size_t>(visiblePadCount() - 1)},
            fChopPreviewPosition, fChopActiveBoundary, fChopLeverPull,
            chopDirty(), fChopApplying, fStatus,
        };
        midichopper::ui::draw(*this, view);
        endLogicalDisplay();

    }

    bool onMouse(const MouseEvent& ev) override
    {
        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX() - uiLayout::contentOffsetX;
        const float y = position.getY();

        if (ev.button == 2)
        {
            if (!ev.press)
                return fPadContextMenuOpen;
            if (fArm || fChopEditorMode)
                return false;

            const int visualIndex = fEditorMode
                ? editorPadGrid().hit({x, y}) : mainPadGrid().hit({x, y});
            const int localPad = localPadFromVisualIndex(visualIndex);
            if (localPad >= 0)
            {
                fMenuOpen = false;
                const int pad = globalPad(localPad);
                if (fEditorMode) {
                    if (fSelectedPad != pad)
                        selectEditorPad(pad);
                    else
                        refreshSelectedWaveform();
                }
                else {
                    if (fSelectedPad != pad) {
                        fEditorSettings = {};
                        fEditorSettingsPad = -1;
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

        if (ev.button != 1)
            return false;

        if (ev.press)
        {
            const auto clicked = resolveInteractiveTarget(x, y);
            if (fHover.update(clicked))
                requestRepaint();
            if (fPadContextMenuOpen)
            {
                fPadContextPointerCaptured = true;
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::padContextItem)) {
                    invokePadMenuAction(static_cast<PadMenuAction>(clicked.index));
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
                        clicked, midichopper::ui::InteractiveType::closeEditor)) {
                    if (chopDirty()) {
                        setLocalStatus("Apply or Revert chop changes before leaving");
                        return true;
                    }
                    stopChopPreview();
                    fChopEditorMode = false;
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
                        clicked, midichopper::ui::InteractiveType::chopBoundary)) {
                    fChopActiveBoundary = clicked.index;
                    fChopDragStartX = x;
                    fChopDragStartOffset =
                        fChopOffsets[static_cast<std::size_t>(clicked.index)];
                    fChopLeverPull = 0.0f;
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopPlay)) {
                    startChopPreview(currentChopSourceFrame());
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopPause)) {
                    stopChopPreview();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopApply)) {
                    if (chopDirty() && !fChopApplying)
                        applyChops();
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::chopCancel)) {
                    if (!fChopApplying) {
                        fChopOffsets.fill(0);
                        fStatus[0] = '\0';
                        requestRepaint();
                    }
                    return true;
                }
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::pad)) {
                    fChopSeeking = true;
                    seekChopPad(clicked.index, x);
                    return true;
                }
                return false;
            }
            if (fEditorMode)
            {
                if (midichopper::ui::isTarget(
                        clicked, midichopper::ui::InteractiveType::closeEditor))
                {
                    releasePressedPad();
                    fEditorMode = false;
                    fDragTarget = WaveformEditTarget::none;
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
                return false;
            }

            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::openEditor))
            {
                fEditorMode = true;
                fChopEditorMode = false;
                fStatus[0] = '\0';
                if (!hasSelectedPad() || bankForGlobalPad(fSelectedPad) != fBank)
                    selectEditorPad(globalPad(0));
                else
                    refreshSelectedWaveform();
                requestRepaint();
                return true;
            }
            if (midichopper::ui::isTarget(
                    clicked, midichopper::ui::InteractiveType::openChopEditor))
            {
                fEditorMode = false;
                fChopEditorMode = true;
                fStatus[0] = '\0';
                initializeChopEditor();
                requestRepaint();
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
                setControlValue(kParameterInputMonitor, fMonitor >= 0.5f ? 0.0f : 1.0f);
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
        else if (fPadContextPointerCaptured)
        {
            fPadContextPointerCaptured = false;
            return true;
        }
        else if (fChopEditorMode && fChopActiveBoundary >= 0)
        {
            fChopActiveBoundary = -1;
            fChopLeverPull = 0.0f;
            requestRepaint();
            return true;
        }
        else if (fChopEditorMode && fChopSeeking)
        {
            fChopSeeking = false;
            return true;
        }
        else if (fEditorMode && fDragTarget != WaveformEditTarget::none)
        {
            commitEditorSettings();
            fDragTarget = WaveformEditTarget::none;
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
            const double rate = fChopWaveforms[boundary].sampleRate;
            const auto delta = static_cast<std::int64_t>(std::llround(
                (x - fChopDragStartX) * std::max(rate, 1.0) * 0.002));
            const auto requested = fChopDragStartOffset + delta;
            fChopOffsets[boundary] = midichopper::ui::chop::clampBoundaryOffset(
                std::span<const sms::audio::WaveformSummary>{
                    fChopWaveforms.data(), static_cast<std::size_t>(visiblePadCount())},
                std::span<const std::int64_t>{
                    fChopOffsets.data(), static_cast<std::size_t>(visiblePadCount() - 1)},
                fChopActiveBoundary, requested);
            fChopLeverPull = std::clamp((x - fChopDragStartX) * 0.25f, -10.0f, 10.0f);
            fStatus[0] = '\0';
            requestRepaint();
            return true;
        }
        if (fChopEditorMode && fChopSeeking) {
            const int visualPad = mainPadGrid().hit({x, y});
            const int localPad = localPadFromVisualIndex(visualPad);
            if (localPad >= 0)
                seekChopPad(localPad, x);
            return true;
        }
        if (fEditorMode && fDragTarget != WaveformEditTarget::none) {
            updateEditorDrag(x, y);
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
        if (!ev.press || ev.key != DGL_NAMESPACE::kKeyEscape ||
            !fPadContextMenuOpen)
            return false;
        closePadContextMenu();
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
    bool fPadContextPointerCaptured;
    bool fPadFileBusy = false;
    bool fPadClipboardAvailable = false;
    bool fPadClipboardBusy = false;
    PadClipboardAction fActivePadClipboardAction = PadClipboardAction::copy;
    int fActivePadClipboardPad = -1;
    bool fSaveDialogAvailable = true;
    PendingFileDialog fPendingFileDialog = PendingFileDialog::none;
    int fPendingFilePad = -1;
    PendingFileDialog fActiveFileAction = PendingFileDialog::none;
    int fActiveFilePad = -1;
    bool fEditorMode;
    bool fChopEditorMode = false;
    bool fPlayOnSelect;
    WaveformEditTarget fDragTarget;
    float fDragStartX;
    float fDragStartY;
    bool fHasWaveform;
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
    sms::audio::WaveformSummary fWaveform{};
    std::array<sms::audio::WaveformSummary, midichopper::kPadsPerBank> fChopWaveforms{};
    std::array<std::int64_t, midichopper::kPadsPerBank - 1U> fChopOffsets{};
    int fChopNextWaveform = -1;
    int fChopActiveBoundary = -1;
    float fChopDragStartX = 0.0f;
    std::int64_t fChopDragStartOffset = 0;
    float fChopLeverPull = 0.0f;
    bool fChopSeeking = false;
    bool fChopPlaying = false;
    bool fChopApplying = false;
    float fChopPreviewPosition = 0.0f;
    char fStatus[160];

    [[nodiscard]] sms::ui::InteractiveTarget
    resolveInteractiveTarget(const float x, const float y) const noexcept
    {
        std::array<bool, static_cast<std::size_t>(PadMenuAction::count)> menuEnabled{};
        for (std::size_t index = 0; index < menuEnabled.size(); ++index)
            menuEnabled[index] = padMenuActionEnabled(static_cast<PadMenuAction>(index));
        const auto envelope = envelopeGraphGeometry();
        midichopper::ui::InteractionContext context;
        context.editorMode = fEditorMode;
        context.chopEditorMode = fChopEditorMode;
        context.menuOpen = fMenuOpen;
        context.padContextMenuOpen = fPadContextMenuOpen;
        context.armed = fArm;
        context.fixedCapture = fRecordMode >= 0.5f;
        context.captureActive = fArm && fCurrentPad >= 0;
        context.padLayout = fLayout;
        context.padContextMenu = fPadContextMenu;
        context.padContextMenuEnabled = menuEnabled;
        context.editorSettings = &fEditorSettings;
        context.envelope = &envelope;
        context.chopWaveforms = std::span<const sms::audio::WaveformSummary>{
            fChopWaveforms.data(), static_cast<std::size_t>(visiblePadCount())};
        return midichopper::ui::interactiveTargetAt({x, y}, context);
    }

    static float normalizedX(const float x, const sms::ui::Rect bounds) noexcept
    {
        return std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
    }

    void openPadContextMenu(const int pad, const sms::ui::Point anchor)
    {
        fPadContextTarget = pad;
        fPadContextMenu = sms::ui::ContextMenuGeometry(
            anchor, static_cast<int>(PadMenuAction::count), uiLayout::contentBounds);
        static_cast<void>(fHover.clear());
        fPadContextClearArmed = false;
        fPadContextMenuOpen = true;
    }

    void closePadContextMenu() noexcept
    {
        fPadContextMenuOpen = false;
        fPadContextTarget = -1;
        static_cast<void>(fHover.clear());
        fPadContextClearArmed = false;
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
        return fPadFileBusy || fPadClipboardBusy;
    }

    [[nodiscard]] bool padCopyEnabled() const noexcept
    {
        return !padActionBusy() && padContextTargetOccupied();
    }

    [[nodiscard]] bool padPasteEnabled() const noexcept
    {
        return !padActionBusy() && fPadClipboardAvailable &&
               fPadContextMenuOpen && fPadContextTarget >= 0;
    }

    [[nodiscard]] bool padProcessedExportEnabled() const noexcept
    {
        if (!padExportEnabled())
            return false;
        if (fEditorSettingsPad != fPadContextTarget)
            return false;
        const auto settings = sms::dsp::sanitize(fEditorSettings);
        return settings.start != 0.0f || settings.end != 1.0f ||
               settings.attackSeconds != 0.0f || settings.decaySeconds != 0.0f ||
               settings.sustainLevel != 1.0f || settings.releaseSeconds != 0.0f;
    }

    [[nodiscard]] bool padMenuActionEnabled(const PadMenuAction action) const noexcept
    {
        switch (action) {
        case PadMenuAction::copy: return padCopyEnabled();
        case PadMenuAction::paste: return padPasteEnabled();
        case PadMenuAction::exportRaw: return padExportEnabled();
        case PadMenuAction::exportProcessed: return padProcessedExportEnabled();
        case PadMenuAction::import: return !padActionBusy();
        case PadMenuAction::clear:
            return !padActionBusy() && padContextTargetOccupied();
        case PadMenuAction::count: return false;
        }
        return false;
    }

    void invokePadMenuAction(const PadMenuAction action)
    {
        if (!padMenuActionEnabled(action))
            return;
        switch (action) {
        case PadMenuAction::copy:
            requestPadClipboard(PadClipboardAction::copy);
            return;
        case PadMenuAction::paste:
            requestPadClipboard(PadClipboardAction::paste);
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
        const int midiNote = mappedMidiNote(globalPad(localPad));
        const int previousMidiNote = fPadPress.press(localPad, midiNote);
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        if (previousMidiNote >= 0)
            sendNote(0, static_cast<uint8_t>(previousMidiNote), 0);
        sendNote(0, static_cast<uint8_t>(midiNote), 127);
#else
        static_cast<void>(previousMidiNote);
#endif
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
        char pad[8];
        std::snprintf(pad, sizeof(pad), "%d", fSelectedPad);
        setState("waveform_request", pad);
#endif
    }

    [[nodiscard]] bool chopDirty() const noexcept
    {
        return std::any_of(fChopOffsets.begin(),
            fChopOffsets.begin() + std::max(0, visiblePadCount() - 1),
            [](const std::int64_t offset) { return offset != 0; });
    }

    void initializeChopEditor()
    {
        stopChopPreview();
        fChopWaveforms.fill({});
        fChopOffsets.fill(0);
        fChopNextWaveform = 0;
        fChopActiveBoundary = -1;
        fChopLeverPull = 0.0f;
        fChopSeeking = false;
        fChopApplying = false;
        fChopPreviewPosition = 0.0f;
    }

    void requestChopWaveform(const int localPad)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (localPad < 0 || localPad >= visiblePadCount())
            return;
        char pad[8];
        std::snprintf(pad, sizeof(pad), "%d", globalPad(localPad));
        setState("waveform_request", pad);
#else
        static_cast<void>(localPad);
#endif
    }

    [[nodiscard]] bool chopRunForPad(const int localPad, int& first, int& count) const noexcept
    {
        if (localPad < 0 || localPad >= visiblePadCount() ||
            fChopWaveforms[static_cast<std::size_t>(localPad)].frames == 0U)
            return false;
        const double rate = fChopWaveforms[static_cast<std::size_t>(localPad)].sampleRate;
        first = localPad;
        while (first > 0) {
            const auto& previous = fChopWaveforms[static_cast<std::size_t>(first - 1)];
            if (previous.frames == 0U || std::abs(previous.sampleRate - rate) > 0.5)
                break;
            --first;
        }
        int last = localPad;
        while (last + 1 < visiblePadCount()) {
            const auto& next = fChopWaveforms[static_cast<std::size_t>(last + 1)];
            if (next.frames == 0U || std::abs(next.sampleRate - rate) > 0.5)
                break;
            ++last;
        }
        count = last - first + 1;
        return count > 0;
    }

    [[nodiscard]] std::uint64_t currentChopSourceFrame() const noexcept
    {
        if (fChopPreviewPosition <= 0.0f)
            return 0U;
        const int pad = static_cast<int>(std::floor(fChopPreviewPosition)) - 1;
        const int localPad = localPadForGlobalPad(pad);
        if (bankForGlobalPad(pad) != fBank || localPad < 0 || localPad >= visiblePadCount())
            return 0U;
        std::uint64_t frame = 0U;
        for (int index = 0; index < localPad; ++index)
            frame += fChopWaveforms[static_cast<std::size_t>(index)].frames;
        const float fraction = fChopPreviewPosition - std::floor(fChopPreviewPosition);
        frame += static_cast<std::uint64_t>(fraction *
            fChopWaveforms[static_cast<std::size_t>(localPad)].frames);
        return frame;
    }

    void startChopPreview(const std::uint64_t bankSourceFrame)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        int localPad = 0;
        std::uint64_t cursor = 0U;
        for (; localPad < visiblePadCount(); ++localPad) {
            const auto frames = fChopWaveforms[static_cast<std::size_t>(localPad)].frames;
            if (frames != 0U && bankSourceFrame < cursor + frames)
                break;
            cursor += frames;
        }
        if (localPad >= visiblePadCount()) {
            localPad = 0;
            while (localPad < visiblePadCount() &&
                   fChopWaveforms[static_cast<std::size_t>(localPad)].frames == 0U)
                ++localPad;
            cursor = 0U;
            for (int index = 0; index < localPad; ++index)
                cursor += fChopWaveforms[static_cast<std::size_t>(index)].frames;
        }
        int runFirst = 0;
        int runCount = 0;
        if (!chopRunForPad(localPad, runFirst, runCount)) {
            setLocalStatus("No contiguous raw pads to preview");
            return;
        }
        std::uint64_t runStart = 0U;
        for (int index = 0; index < runFirst; ++index)
            runStart += fChopWaveforms[static_cast<std::size_t>(index)].frames;
        const auto request = midichopper::plugin::encodeChopPreviewRequest(
            {true, static_cast<std::uint32_t>(globalPad(runFirst)),
             static_cast<std::uint32_t>(runCount),
             bankSourceFrame > runStart ? bankSourceFrame - runStart : 0U});
        setState("chop_preview_request", request.c_str());
        fChopPlaying = true;
        requestRepaint();
#else
        static_cast<void>(bankSourceFrame);
#endif
    }

    void stopChopPreview()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        const auto request = midichopper::plugin::encodeChopPreviewRequest(
            {false, static_cast<std::uint32_t>(globalPad(0)), 1U, 0U});
        setState("chop_preview_request", request.c_str());
#endif
        fChopPlaying = false;
        requestRepaint();
    }

    void seekChopPad(const int localPad, const float x)
    {
        if (localPad < 0 || localPad >= visiblePadCount() ||
            fChopWaveforms[static_cast<std::size_t>(localPad)].frames == 0U)
            return;
        const auto cell = mainPadGrid().cell(padLayout().visualIndex(localPad));
        const float normalized = std::clamp((x - cell.x) / cell.width, 0.0f, 0.999999f);
        const auto waveforms = std::span<const sms::audio::WaveformSummary>{
            fChopWaveforms.data(), static_cast<std::size_t>(visiblePadCount())};
        const auto offsets = std::span<const std::int64_t>{
            fChopOffsets.data(), static_cast<std::size_t>(visiblePadCount() - 1)};
        const auto frame = midichopper::ui::chop::adjustedStartFrame(
            waveforms, offsets, localPad) + static_cast<std::uint64_t>(normalized *
                midichopper::ui::chop::adjustedFrames(waveforms, offsets, localPad));
        startChopPreview(frame);
    }

    void applyChops()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        int firstBoundary = -1;
        for (int boundary = 0; boundary + 1 < visiblePadCount(); ++boundary) {
            if (fChopOffsets[static_cast<std::size_t>(boundary)] != 0) {
                firstBoundary = boundary;
                break;
            }
        }
        if (firstBoundary < 0)
            return;
        int runFirst = 0;
        int runCount = 0;
        if (!chopRunForPad(firstBoundary, runFirst, runCount) || runCount < 2) {
            setLocalStatus("Changed boundary is not part of a contiguous pad run");
            return;
        }
        for (int boundary = 0; boundary + 1 < visiblePadCount(); ++boundary) {
            if (fChopOffsets[static_cast<std::size_t>(boundary)] != 0 &&
                (boundary < runFirst || boundary >= runFirst + runCount - 1)) {
                setLocalStatus("Apply or Revert one contiguous pad run at a time");
                return;
            }
        }
        midichopper::plugin::ChopApplyRequest request;
        request.firstPad = static_cast<std::uint32_t>(globalPad(runFirst));
        request.padCount = static_cast<std::uint32_t>(runCount);
        for (int boundary = 0; boundary + 1 < runCount; ++boundary)
            request.boundaryOffsets[static_cast<std::size_t>(boundary)] =
                fChopOffsets[static_cast<std::size_t>(runFirst + boundary)];
        stopChopPreview();
        fChopApplying = true;
        setLocalStatus("Applying chop boundaries...");
        const auto encoded = midichopper::plugin::encodeChopApplyRequest(request);
        setState("chop_apply_request", encoded.c_str());
#endif
    }

    void refreshSelectedWaveform()
    {
        fHasWaveform = false;
        requestWaveform();
    }

    void selectEditorPad(const int pad)
    {
        fSelectedPad = clampPad(static_cast<float>(pad));
        fEditorSettings = {};
        fEditorSettingsPad = -1;
        fHasWaveform = false;
        requestWaveform();
        requestRepaint();
    }

    void selectBank(const int bank)
    {
        const int selectedBank = std::clamp(bank, 0, static_cast<int>(midichopper::kBankCount - 1));
        if (selectedBank == fBank || (fArm && fCurrentPad >= 0))
            return;
        if (fChopEditorMode && chopDirty()) {
            setLocalStatus("Apply or Revert chop changes before changing bank");
            return;
        }
        const int localPad = hasSelectedPad()
            ? std::clamp(localPadForGlobalPad(fSelectedPad), 0, visiblePadCount() - 1)
            : 0;
        fBank = selectedBank;
        if (fChopEditorMode)
            initializeChopEditor();
        else if (fEditorMode)
            selectEditorPad(globalPad(localPad));
        else {
            fSelectedPad = -1;
            fHasWaveform = false;
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
        if (fChopEditorMode && chopDirty()) {
            setLocalStatus("Apply or Revert chop changes before changing layout");
            return;
        }
        fLayout = selectedLayout;
        if (fStartPad >= visiblePadCount()) {
            fStartPad = 0;
            setControlValue(kParameterStartPad, 1.0f);
        }
        normalizeSelectionForContext();
        if (fChopEditorMode)
            initializeChopEditor();
        setControlValue(kParameterPadLayout, static_cast<float>(fLayout));
        requestRepaint();
    }

    void selectMidiBankMode(const int mode)
    {
        if (fArm && fCurrentPad >= 0)
            return;
        const int selectedMode = mode == 0 ? 0 : 1;
        if (fChopEditorMode && chopDirty()) {
            setLocalStatus("Apply or Revert chop changes before changing MIDI mode");
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

    bool parseEditorState(const char* const key, const char* const value)
    {
        for (std::size_t pad = 0; pad < kPadEditStateKeys.size(); ++pad)
        {
            if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) != 0)
                continue;
            sms::dsp::SamplePlaybackSettings decoded;
            if (sms::state::decodeSamplePlaybackSettings(value, decoded) &&
                pad == static_cast<std::size_t>(fSelectedPad)) {
                fEditorSettings = decoded;
                fEditorSettingsPad = static_cast<int>(pad);
            }
            return true;
        }
        return false;
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
