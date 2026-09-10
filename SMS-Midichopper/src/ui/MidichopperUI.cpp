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
#include "Configuration.hpp"
#include "DPF/NanoUI.hpp"
#include "DPF/Theme.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"
#include "UI/Geometry.hpp"
#include "PadLayout.hpp"
#include "WaveformEditor.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperView.hpp"
#include "Parameters.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using namespace midichopper::plugin;
namespace uiLayout = midichopper::ui::layout;
using WaveformEditTarget = sms::ui::waveform::EditTarget;

inline constexpr auto kClearConfirmationTimeout = std::chrono::seconds(2);

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
          fPressedPad(-1),
          fPressedMidiNote(-1),
          fPressedActionParameter(-1),
          fClearArmed(false),
          fMenuOpen(false),
          fEditorMode(false),
          fDragTarget(WaveformEditTarget::none),
          fDragStartX(0.0f),
          fDragStartY(0.0f),
          fHasWaveform(false),
          fCaptureTargetRequestAlternateHalf(false)
    {
        fPadState.fill('0');
        fPadStatus.fill('0');
        fStatus[0] = '\0';

#ifndef DGL_NO_SHARED_RESOURCES
        loadSharedResources();
#endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kFirstPadStatusParameter && index < kFirstPadActivityParameter)
        {
            const auto localPad = static_cast<std::size_t>(index - kFirstPadStatusParameter);
            const int pad = globalPad(static_cast<int>(localPad));
            const bool wasOccupied = fPadState[localPad] != '0';
            const bool isOccupied = value >= 0.5f;
            if (wasOccupied == isOccupied)
                return;
            fPadState[localPad] = isOccupied ? '1' : '0';
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
            fArm = armed;
            fStatus[0] = '\0';
            fSelectedPad = -1;
            fLastPlayedPad = -1;
            if (fArm) {
                fEditorMode = false;
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
                if (fEditorMode && localPad >= 0)
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
            const int localPad = hasSelectedPad()
                ? std::clamp(localPadForGlobalPad(fSelectedPad), 0, visiblePadCount() - 1)
                : 0;
            fBank = bank;
            if (fEditorMode)
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
            fLayout = layout;
            if (fStartPad >= visiblePadCount())
                fStartPad = 0;
            if (!fArm && !fEditorMode)
                fLastPlayedPad = -1;
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
            changed = !fArm && pad < midichopper::kPadCount;
            if (!changed)
                break;
            fLastPlayedPad = static_cast<int>(pad);
            fBank = bankForGlobalPad(fLastPlayedPad);
            if (fEditorMode)
                selectEditorPad(fLastPlayedPad);
            else {
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

        if (std::strcmp(key, "pad_mask") == 0)
            parsePadMask(value, fPadState);
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
            fSelectedPad = clampPad(std::strtof(value, nullptr));
            fBank = std::clamp(bankForGlobalPad(fSelectedPad), 0,
                               static_cast<int>(midichopper::kBankCount - 1));
        }
        else if (std::strcmp(key, "status") == 0)
            copyString(fStatus, value);
        else if (std::strcmp(key, "waveform_data") == 0)
        {
            sms::audio::WaveformSummary summary;
            if (sms::audio::decodeWaveformSummary(value, summary) &&
                summary.pad == static_cast<std::uint32_t>(fSelectedPad))
            {
                fWaveform = summary;
                fHasWaveform = summary.frames != 0U;
            }
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
        if (fClearArmed && std::chrono::steady_clock::now() >= fClearDeadline) {
            fClearArmed = false;
            requestRepaint();
        }
    }

    void onNanoDisplay() override
    {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());

        beginPath();
        rect(0.0f, 0.0f, w, h);
        fillColor(sms::ui::dpf::theme().canvas);
        fill();

        beginLogicalDisplay();
        const midichopper::ui::ViewState view{
            fArm, fRecordMode, fFixedLength, fPlaybackMode, fMonitor,
            fStartPad, fPreRoll, fBaseNote, fMidiBankMode, fGain, fMaxVoices, fBank, fLayout,
            fSelectedPad, fCurrentPad, fPressedPad, fClearArmed, fMenuOpen,
            fEditorMode, fHasWaveform, fPadState, fPadStatus,
            fEditorSettings, fWaveform, fStatus,
        };
        midichopper::ui::draw(*this, view);
        endLogicalDisplay();

    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX();
        const float y = position.getY();

        if (ev.press)
        {
            if (hit(x, y, uiLayout::menuButton))
            {
                fMenuOpen = !fMenuOpen;
                requestRepaint();
                return true;
            }
            if (fMenuOpen)
            {
                for (int layoutIndex = 0;
                     layoutIndex < static_cast<int>(midichopper::kPadLayoutCount);
                     ++layoutIndex)
                {
                    if (hit(x, y, uiLayout::menuOption(layoutIndex)))
                    {
                        fMenuOpen = false;
                        selectLayout(layoutIndex);
                        return true;
                    }
                }
                for (int mode = 0; mode < static_cast<int>(midichopper::kMidiBankModeCount); ++mode)
                {
                    if (hit(x, y, uiLayout::midiBankModeOption(mode)))
                    {
                        fMenuOpen = false;
                        selectMidiBankMode(mode);
                        return true;
                    }
                }
                fMenuOpen = false;
                requestRepaint();
                return true;
            }
            if (fEditorMode)
            {
                if (hit(x, y, uiLayout::closeEditor))
                {
                    fEditorMode = false;
                    fDragTarget = WaveformEditTarget::none;
                    restoreLastPlayedSelection();
                    requestRepaint();
                    return true;
                }
                for (int bank = 0; bank < static_cast<int>(midichopper::kBankCount); ++bank)
                {
                    if (hit(x, y, uiLayout::editorBank(bank)))
                    {
                        selectBank(bank);
                        return true;
                    }
                }
                const int editorPad = localPadFromVisualIndex(editorPadGrid().hit({x, y}));
                if (editorPad >= 0)
                {
                    selectEditorPad(globalPad(editorPad));
                    return true;
                }
                if (hit(x, y, uiLayout::editorWaveform))
                {
                    fDragTarget = sms::ui::waveform::nearestRegionHandle(
                        x, uiLayout::editorWaveform, fEditorSettings);
                    updateEditorDrag(x, y);
                    return true;
                }
                if (hit(x, y, uiLayout::envelopeGraph))
                {
                    fDragTarget = hitEnvelopeHandle(x, y);
                    if (fDragTarget != WaveformEditTarget::none)
                    {
                        fDragStartX = x;
                        fDragStartY = y;
                        fDragStartSettings = fEditorSettings;
                        return true;
                    }
                }
                constexpr std::array sliderTargets{
                    WaveformEditTarget::attackSlider,
                    WaveformEditTarget::decaySlider,
                    WaveformEditTarget::sustainSlider,
                    WaveformEditTarget::releaseSlider,
                };
                for (std::size_t index = 0; index < sliderTargets.size(); ++index)
                {
                    if (hit(x, y, uiLayout::editorSlider(static_cast<int>(index))))
                    {
                        fDragTarget = sliderTargets[index];
                        updateEditorDrag(x, y);
                        return true;
                    }
                }
                return false;
            }

            if (hit(x, y, uiLayout::openEditor))
            {
                if (fArm)
                    return true;
                fEditorMode = true;
                if (!hasSelectedPad() || bankForGlobalPad(fSelectedPad) != fBank)
                    selectEditorPad(globalPad(0));
                else
                    refreshSelectedWaveform();
                requestRepaint();
                return true;
            }
            for (int bank = 0; bank < static_cast<int>(midichopper::kBankCount); ++bank)
            {
                if (hit(x, y, uiLayout::mainBank(bank)))
                {
                    selectBank(bank);
                    return true;
                }
            }
            const int pad = hitPad(x, y);
            if (pad >= 0)
            {
                if (fArm)
                {
                    if (fCurrentPad >= 0)
                        return true;
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
                    fPressedPad = pad;
                    fPressedMidiNote = mappedMidiNote(globalPad(pad));
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
                    sendNote(0, static_cast<uint8_t>(fPressedMidiNote), 127);
#endif
                }
                requestRepaint();
                return true;
            }

            if (hit(x, y, uiLayout::playMode))
            {
                setControlValue(kParameterMode, 0.0f);
                return true;
            }
            if (hit(x, y, uiLayout::armMode))
            {
                setControlValue(kParameterMode, 1.0f);
                return true;
            }
            if (hit(x, y, uiLayout::sequentialMode))
            {
                setControlValue(kParameterCaptureMode, 0.0f);
                return true;
            }
            if (hit(x, y, uiLayout::fixedMode))
            {
                setControlValue(kParameterCaptureMode, 1.0f);
                return true;
            }
            if (hit(x, y, uiLayout::fixedLength))
            {
                const float t = normalizedX(x, uiLayout::fixedLength);
                setControlValue(kParameterFixedLengthSeconds,
                    parameterRanges::fixedLengthSeconds.minimum + t *
                    (parameterRanges::fixedLengthSeconds.maximum -
                     parameterRanges::fixedLengthSeconds.minimum));
                return true;
            }
            if (hit(x, y, uiLayout::oneShotMode))
            {
                setControlValue(kParameterPlaybackMode, 0.0f);
                return true;
            }
            if (hit(x, y, uiLayout::gatedMode))
            {
                setControlValue(kParameterPlaybackMode, 1.0f);
                return true;
            }
            if (hit(x, y, uiLayout::voiceLimit))
            {
                const float t = normalizedX(x, uiLayout::voiceLimit);
                setControlValue(kParameterMaxVoices,
                    parameterRanges::maxVoices.minimum + static_cast<float>(std::lround(
                        t * (parameterRanges::maxVoices.maximum -
                             parameterRanges::maxVoices.minimum))));
                return true;
            }
            if (hit(x, y, uiLayout::preRoll))
            {
                const float t = normalizedX(x, uiLayout::preRoll);
                setControlValue(kParameterPreRollMs,
                                t * parameterRanges::preRollMs.maximum);
                return true;
            }
            if (hit(x, y, uiLayout::monitor))
            {
                setControlValue(kParameterInputMonitor, fMonitor >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
            if (hit(x, y, uiLayout::finalizeAction))
            {
                setParameterValue(kParameterFinalize, 1.0f);
                fPressedActionParameter = kParameterFinalize;
                setLocalStatus("Chop finalized");
                return true;
            }
            if (hit(x, y, uiLayout::undoAction))
            {
                setParameterValue(kParameterUndo, 1.0f);
                fPressedActionParameter = kParameterUndo;
                setLocalStatus("Last chop undone");
                return true;
            }
            if (hit(x, y, uiLayout::clearAction))
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
        else if (fEditorMode && fDragTarget != WaveformEditTarget::none)
        {
            commitEditorSettings();
            fDragTarget = WaveformEditTarget::none;
            requestRepaint();
            return true;
        }
        else if (fPressedPad >= 0)
        {
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
            if (fPressedMidiNote >= 0)
                sendNote(0, static_cast<uint8_t>(fPressedMidiNote), 0);
#endif
            fPressedPad = -1;
            fPressedMidiNote = -1;
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
        if (!fEditorMode || fDragTarget == WaveformEditTarget::none)
            return false;
        const auto position = toLogicalPosition(ev.pos);
        const float x = position.getX();
        const float y = position.getY();
        updateEditorDrag(x, y);
        return true;
    }

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
    int fPressedPad;
    int fPressedMidiNote;
    int fPressedActionParameter;
    bool fClearArmed;
    std::chrono::steady_clock::time_point fClearDeadline{};
    bool fMenuOpen;
    bool fEditorMode;
    WaveformEditTarget fDragTarget;
    float fDragStartX;
    float fDragStartY;
    bool fHasWaveform;
    bool fCaptureTargetRequestAlternateHalf;
    PlaybackPadEventTracker fPlaybackPadEvents;
    std::array<char, midichopper::kPadsPerBank> fPadState;
    std::array<char, midichopper::kPadsPerBank> fPadStatus;
    sms::dsp::SamplePlaybackSettings fEditorSettings{};
    sms::dsp::SamplePlaybackSettings fDragStartSettings{};
    sms::audio::WaveformSummary fWaveform{};
    char fStatus[160];

    static bool hit(const float x, const float y, const sms::ui::Rect bounds) noexcept
    {
        return bounds.contains({x, y});
    }

    static float normalizedX(const float x, const sms::ui::Rect bounds) noexcept
    {
        return std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
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

    int hitPad(float x, float y) const
    {
        return localPadFromVisualIndex(mainPadGrid().hit({x, y}));
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

    WaveformEditTarget hitEnvelopeHandle(const float x, const float y) const noexcept
    {
        return sms::ui::waveform::hitEnvelopeHandle({x, y}, envelopeGraphGeometry());
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

    void refreshSelectedWaveform()
    {
        fHasWaveform = false;
        requestWaveform();
    }

    void selectEditorPad(const int pad)
    {
        fSelectedPad = clampPad(static_cast<float>(pad));
        fEditorSettings = {};
        fHasWaveform = false;
        requestWaveform();
        requestRepaint();
    }

    void selectBank(const int bank)
    {
        const int selectedBank = std::clamp(bank, 0, static_cast<int>(midichopper::kBankCount - 1));
        if (selectedBank == fBank || (fArm && fCurrentPad >= 0))
            return;
        const int localPad = hasSelectedPad()
            ? std::clamp(localPadForGlobalPad(fSelectedPad), 0, visiblePadCount() - 1)
            : 0;
        fBank = selectedBank;
        if (fEditorMode)
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
        if (selectedMode != fMidiBankMode)
            setControlValue(kParameterMidiBankMode, static_cast<float>(selectedMode));
        requestRepaint();
    }

    void commitEditorSettings()
    {
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
                pad == static_cast<std::size_t>(fSelectedPad))
                fEditorSettings = decoded;
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
