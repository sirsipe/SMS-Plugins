#include "SamplerUiSession.hpp"

#include "BankModePreference.hpp"
#include "LayoutPreference.hpp"
#include "LevelMeter.hpp"
#include "State/SampleMixerSettingsCodec.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"
#include "../plugin/CaptureActionProtocol.hpp"
#include "../plugin/PadColorState.hpp"
#include "../plugin/PadDirectionRequest.hpp"
#include "../plugin/StateKeys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace midichopper::ui
{
namespace
{
using namespace midichopper::plugin;
constexpr auto kMeterFrameInterval = std::chrono::milliseconds(33);
constexpr auto kEditorSnapshotRetryInterval = std::chrono::milliseconds(250);

void copyString(char (&destination)[160], const char *source)
{
    std::strncpy(destination, source, sizeof(destination) - 1);
    destination[sizeof(destination) - 1] = '\0';
}

template <std::size_t N> void copyChars(std::array<char, N> &destination, const char *source)
{
    std::size_t index = 0;
    while (index < N && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    std::fill(destination.begin() + index, destination.end(), '0');
}

int parsePad(const char *value, const int fallback)
{
    if (value == nullptr || *value == '\0')
        return fallback;
    return std::clamp(static_cast<int>(std::lround(std::strtof(value, nullptr))), 0,
                      static_cast<int>(midichopper::kPadCount - 1));
}

template <std::size_t N> void parsePadMask(const char *value, std::array<char, N> &destination)
{
    destination.fill('0');
    if (value == nullptr)
        return;
    if (std::strncmp(value, "0x", 2) == 0 || std::strncmp(value, "0X", 2) == 0) {
        const unsigned long mask = std::strtoul(value + 2, nullptr, 16);
        for (std::size_t i = 0; i < N && i < 16; ++i)
            destination[i] = (mask & (1UL << i)) != 0 ? '1' : '0';
        return;
    }
    copyChars(destination, value);
}
} // namespace

using namespace midichopper::plugin;

SamplerUiSession::SamplerUiSession(SamplerUiHost &host,
                                   const SamplerUiCapabilities capabilities) noexcept
    : fHost(host), fCapabilities(capabilities), fChopEditor(*this),
      fPadActions(*this, fChopEditor, capabilities.colorCount + 1, capabilities.stateTransport,
                  capabilities.fileBrowser)
{
    fState.padState.fill('0');
    fState.padStatus.fill('0');
}

void SamplerUiSession::acceptParameter(std::uint32_t index, float value, const Time now)
{
    if (index >= kParameterInputLevelLeft && index <= kParameterOutputLevelRight) {
        auto &levels =
            index <= kParameterInputLevelRight ? fState.inputLevels : fState.outputLevels;
        const std::size_t channel =
            static_cast<std::size_t>((index - kParameterInputLevelLeft) % 2U);
        const float level = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
        const bool visibleChange = sms::ui::meter::visibleLevelChanged(levels[channel], level);
        levels[channel] = level;
        if (visibleChange)
            fMeterRepaintPending = true;
        return;
    }
    if (fPadActions.acceptParameter(padActionContext(), index, value))
        return;
    if (index == kParameterChopPreviewPosition) {
        fChopEditor.acceptPreviewPosition(value);
        fHost.repaintUi();
        return;
    }
    if (index == kParameterPlaybackPosition) {
        const int pad =
            std::isfinite(value) && value > 0.0f ? static_cast<int>(std::floor(value)) - 1 : -1;
        const bool loop = fEditorAuditionPad == pad && pad >= 0 ? fEditorAuditionLoop
                                                                : fState.playbackMode >= 0.5f;
        fPlaybackIndicator.acceptHostPosition(value, now, loop);
        if (fState.editorMode)
            fHost.repaintUi();
        return;
    }
    if (index == kParameterAnyPlaybackActive) {
        const bool active = value >= 0.5f;
        if (fState.anyPlaybackActive != active) {
            fState.anyPlaybackActive = active;
            fHost.repaintUi();
        }
        return;
    }
    if (index >= kFirstPadStatusParameter && index < kFirstPadActivityParameter) {
        const auto localPad = static_cast<std::size_t>(index - kFirstPadStatusParameter);
        const int pad = globalPad(static_cast<int>(localPad));
        const bool wasOccupied = fState.padState[localPad] != '0';
        const bool isOccupied = value >= 0.5f;
        if (wasOccupied == isOccupied)
            return;
        fState.padState[localPad] = isOccupied ? '1' : '0';
        if (pad == fPadActions.target())
            fPadActions.closeMenu();
        if (pad == fState.selectedPad)
            requestWaveform();
        fHost.repaintUi();
        return;
    }
    if (index >= kFirstPadActivityParameter && index < kParameterMaxVoices) {
        const int localPad = static_cast<int>(index - kFirstPadActivityParameter);
        const bool wasActive = fState.padStatus[static_cast<std::size_t>(localPad)] != '0';
        const bool isActive = value >= 0.5f;
        if (wasActive == isActive)
            return;
        fState.padStatus[static_cast<std::size_t>(localPad)] = isActive ? '1' : '0';
        if (isActive && !fState.armed)
            startLocalPlayhead(globalPad(localPad));
        else if (!isActive && globalPad(localPad) == fPlaybackIndicator.pad())
            stopLocalPlayhead(true);
        fHost.repaintUi();
        return;
    }
    bool changed = true;
    switch (index) {
    case kParameterMode: {
        const bool armed = value >= 0.5f;
        changed = fState.armed != armed;
        if (!changed)
            break;
        fPadActions.closeMenu();
        fHost.invalidateUiInteraction(UiInteractionInvalidation::armChanged);
        if (armed && fChopEditor.active())
            fChopEditor.cancel();
        if (armed)
            fPadActions.invalidatePendingStructure();
        stopEditorAudition();
        fState.armed = armed;
        fState.status[0] = '\0';
        fState.selectedPad = -1;
        fState.lastPlayedPad = -1;
        if (fState.armed) {
            fState.editorMode = false;
            fChopEditor.stopPreview();
            stopLocalPlayhead(false);
            fHost.invalidateUiInteraction(UiInteractionInvalidation::armEntered);
            selectAutomaticArmTarget();
        }
        break;
    }
    case kParameterCaptureMode:
        changed = fState.captureMode != value;
        fState.captureMode = value;
        break;
    case kParameterFixedLengthSeconds:
        changed = fState.fixedLength != value;
        fState.fixedLength = value;
        break;
    case kParameterPlaybackMode:
        changed = fState.playbackMode != value;
        if (changed)
            stopEditorAudition();
        fState.playbackMode = value;
        break;
    case kParameterInputMonitor:
        changed = fState.monitor != value;
        fState.monitor = value;
        break;
    case kParameterStartPad: {
        const int previousStartPad = fState.startPad;
        const int previousSelectedPad = fState.selectedPad;
        fState.startPad = clampLocalPad(value - parameterRanges::startPad.minimum);
        if (fState.armed && fState.currentPad < 0)
            fState.selectedPad = globalPad(fState.startPad);
        changed = previousStartPad != fState.startPad || previousSelectedPad != fState.selectedPad;
        break;
    }
    case kParameterPreRollMs:
        changed = fState.preRoll != value;
        fState.preRoll = value;
        break;
    case kParameterBaseMidiNote: {
        const int baseNote = clampBaseNote(value);
        changed = fState.baseNote != baseNote;
        fState.baseNote = baseNote;
        break;
    }
    case kParameterMidiBankMode: {
        const int localPad = hasSelectedPad() ? localPadForGlobalPad(fState.selectedPad) : -1;
        const int mode = value >= 0.5f ? 1 : 0;
        changed = fState.midiBankMode != mode;
        fState.midiBankMode = mode;
        if (changed) {
            fPadActions.closeMenu();
            if (fChopEditor.active())
                fChopEditor.cancel();
            else if (fState.editorMode && localPad >= 0)
                selectEditorPad(globalPad(std::clamp(localPad, 0, visiblePadCount() - 1)));
            else {
                fState.selectedPad = -1;
                if (!fState.armed)
                    fState.lastPlayedPad = -1;
            }
        }
        break;
    }
    case kParameterOutputGainDb:
        changed = fState.gain != value;
        fState.gain = value;
        break;
    case kParameterMonitorGainDb:
        changed = fState.monitorGain != value;
        fState.monitorGain = value;
        break;
    case kParameterGlobalPan:
        changed = fState.globalPan != value;
        fState.globalPan = value;
        break;
    case kParameterGlobalTuneSemitones:
        changed = fState.globalTune != value;
        fState.globalTune = value;
        break;
    case kParameterGlobalLowpass:
        changed = fState.globalLowpass != value;
        fState.globalLowpass = value;
        break;
    case kParameterGlobalHighpass:
        changed = fState.globalHighpass != value;
        fState.globalHighpass = value;
        break;
    case kParameterGlobalFilterSlope:
        changed = fState.globalFilterSlope != value;
        fState.globalFilterSlope = value;
        break;
    case kParameterGlobalDirty:
        changed = fState.globalDirty != value;
        fState.globalDirty = value;
        break;
    case kParameterGlobalReverse: {
        const bool reverse = value >= 0.5f;
        changed = fState.globalReverse != reverse;
        if (changed) {
            fState.globalReverse = reverse;
            fWaveformDetail.setReversed(
                fState.editorMode && !fChopEditor.active() && effectiveSampleReverse(), now);
            if (fState.mixerSettings.globalMixer >= 0.5f &&
                fState.editorSettingsPad == fState.selectedPad &&
                fState.waveform.pad == static_cast<std::uint32_t>(fState.selectedPad))
                fPlaybackIndicator.flipDirection(fState.selectedPad, fState.waveform.frames,
                                                 displayEditorSettings(), now);
            fHost.invalidateUiInteraction(UiInteractionInvalidation::globalDirectionChanged);
        }
        break;
    }
    case kParameterMaxVoices: {
        const int maxVoices = std::clamp(static_cast<int>(std::lround(value)),
                                         static_cast<int>(parameterRanges::maxVoices.minimum),
                                         static_cast<int>(parameterRanges::maxVoices.maximum));
        changed = fState.maxVoices != maxVoices;
        fState.maxVoices = maxVoices;
        break;
    }
    case kParameterActiveBank: {
        const int bank = std::clamp(static_cast<int>(std::lround(value)) - 1, 0,
                                    static_cast<int>(parameterRanges::activeBank.maximum - 1.0f));
        changed = fState.bank != bank;
        if (!changed)
            break;
        fPadActions.closeMenu();
        const int localPad = hasSelectedPad() ? std::clamp(localPadForGlobalPad(fState.selectedPad),
                                                           0, visiblePadCount() - 1)
                                              : 0;
        fState.bank = bank;
        if (fChopEditor.active())
            fChopEditor.cancel();
        else if (fState.editorMode)
            selectEditorPad(globalPad(localPad));
        else {
            fState.selectedPad = -1;
            if (!fState.armed)
                fState.lastPlayedPad = -1;
        }
        break;
    }
    case kParameterPadLayout: {
        const int layout = std::clamp(static_cast<int>(std::lround(value)),
                                      static_cast<int>(parameterRanges::padLayout.minimum),
                                      static_cast<int>(parameterRanges::padLayout.maximum));
        changed = fState.layout != layout;
        if (!changed)
            break;
        fPadActions.closeMenu();
        stopEditorAudition();
        fState.layout = layout;
        if (fState.startPad >= visiblePadCount())
            fState.startPad = 0;
        if (!fState.armed && !fState.editorMode && !fChopEditor.active())
            fState.lastPlayedPad = -1;
        if (fChopEditor.active())
            fChopEditor.cancel();
        normalizeSelectionForContext();
        break;
    }
    case kParameterCurrentCapturePad: {
        const int pad = static_cast<int>(std::lround(value)) - 1;
        const int currentPad =
            pad >= 0 && pad < static_cast<int>(midichopper::kPadCount) ? pad : -1;
        changed = fState.currentPad != currentPad;
        if (!changed)
            break;
        fState.currentPad = currentPad;
        if (fState.armed && fState.currentPad >= 0) {
            const int bank = bankForGlobalPad(fState.currentPad);
            if (bank >= 0 && bank < static_cast<int>(midichopper::kBankCount)) {
                const int localPad = localPadForGlobalPad(fState.currentPad);
                changed = changed || fState.bank != bank ||
                          fState.selectedPad != fState.currentPad || fState.startPad != localPad;
                fState.bank = bank;
                fState.selectedPad = fState.currentPad;
                fState.startPad = localPad;
            }
        }
        break;
    }
    case kParameterPlaybackPadEvent: {
        const std::uint32_t pad = fPlaybackPadEvents.consume(value);
        // MIDI performance remains audible in the Cut Point Editor, but it must not
        // silently switch the editor bank or discard a pending boundary plan.
        changed = !fState.armed && !fChopEditor.active() && pad < midichopper::kPadCount;
        if (!changed)
            break;
        fPadActions.closeMenu();
        fState.lastPlayedPad = static_cast<int>(pad);
        fState.bank = bankForGlobalPad(fState.lastPlayedPad);
        if (fState.editorMode)
            selectEditorPad(fState.lastPlayedPad, false);
        else {
            if (fState.selectedPad != fState.lastPlayedPad) {
                fState.editorSettings = {};
                fState.editorSettingsPad = -1;
                fState.mixerSettings = {};
                fState.mixerSettingsPad = -1;
            }
            fState.selectedPad = fState.lastPlayedPad;
            requestWaveform();
        }
        startLocalPlayhead(fState.lastPlayedPad);
        break;
    }
    case kParameterCaptureTargetPad: {
        if (!fState.armed) {
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
        changed = fState.selectedPad != target;
        fState.selectedPad = target;
        if (target >= 0) {
            const int bank = bankForGlobalPad(target);
            const int localPad = localPadForGlobalPad(target);
            changed = changed || fState.bank != bank || fState.startPad != localPad;
            fState.bank = bank;
            fState.startPad = localPad;
        }
        break;
    }
    default:
        return;
    }
    if (changed)
        fHost.repaintUi();
}

bool SamplerUiSession::acceptState(const char *key, const char *value)
{
    if (key == nullptr || value == nullptr)
        return false;

    if (std::strcmp(key, "pad_mask") == 0) {
        parsePadMask(value, fState.padState);
        fPadActions.closeMenu();
    } else if (std::strcmp(key, "pad_status") == 0)
        copyChars(fState.padStatus, value);
    else if (std::strcmp(key, "current_pad") == 0) {
        fState.currentPad = parsePad(value, -1);
        if (fState.currentPad >= 0)
            fState.bank = bankForGlobalPad(fState.currentPad);
    } else if (std::strcmp(key, "selected_pad") == 0) {
        const int selectedPad = clampPad(std::strtof(value, nullptr));
        const bool selectionChanged = selectedPad != fState.selectedPad;
        fState.selectedPad = selectedPad;
        fState.bank = std::clamp(bankForGlobalPad(fState.selectedPad), 0,
                                 static_cast<int>(midichopper::kBankCount - 1));
        if (selectionChanged) {
            if (fState.editorMode)
                requestWaveform();
            else
                fEditorSnapshot.complete();
        }
    } else if (std::strcmp(key, "status") == 0)
        copyString(fState.status, value);
    else if (std::strcmp(key, "waveform_data") == 0) {
        midichopper::plugin::EditorSnapshotReply reply;
        if (midichopper::plugin::decodeEditorSnapshotReply(value, reply) &&
            fEditorSnapshot.accept(reply))
            commitEditorSnapshotIfReady();
    } else if (std::strcmp(key, "waveform_detail_data") == 0) {
        midichopper::plugin::WaveformDetailReply reply;
        if (midichopper::plugin::decodeWaveformDetailReply(value, reply))
            static_cast<void>(fWaveformDetail.accept(reply));
    } else if (std::strcmp(key, "chop_snapshot_data") == 0 ||
               std::strcmp(key, "chop_status") == 0) {
        static_cast<void>(fChopEditor.acceptState(key, value));
    } else if (fPadActions.acceptState(padActionContext(), key, value)) {
    } else if (parseEditorState(key, value)) {
    } else if (parseMixerState(key, value)) {
    } else if (parsePadColorState(key, value)) {
    } else
        return false;
    fHost.repaintUi();
    return true;
}

int SamplerUiSession::clampPad(float value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0,
                      static_cast<int>(midichopper::kPadCount - 1));
}

int SamplerUiSession::clampLocalPad(const float value) const
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, visiblePadCount() - 1);
}

int SamplerUiSession::clampBaseNote(const float value)
{
    return std::clamp(static_cast<int>(std::lround(value)),
                      static_cast<int>(parameterRanges::baseMidiNote.minimum),
                      static_cast<int>(parameterRanges::baseMidiNote.maximum));
}

int SamplerUiSession::visiblePadCount() const noexcept
{
    return padLayout().visiblePadCount();
}

int SamplerUiSession::globalPad(const int localPad) const noexcept
{
    return fState.bank * static_cast<int>(midichopper::bankStride(
                             static_cast<std::uint8_t>(visiblePadCount()), midiBankMode())) +
           localPad;
}

midichopper::MidiBankMode SamplerUiSession::midiBankMode() const noexcept
{
    return fState.midiBankMode == 0 ? midichopper::MidiBankMode::SelectedBank
                                    : midichopper::MidiBankMode::AllBanks;
}

int SamplerUiSession::bankForGlobalPad(const int pad) const noexcept
{
    return static_cast<int>(midichopper::bankForPad(static_cast<std::uint32_t>(std::max(pad, 0)),
                                                    static_cast<std::uint8_t>(visiblePadCount()),
                                                    midiBankMode()));
}

int SamplerUiSession::localPadForGlobalPad(const int pad) const noexcept
{
    return static_cast<int>(
        midichopper::localPadInBank(static_cast<std::uint32_t>(std::max(pad, 0)),
                                    static_cast<std::uint8_t>(visiblePadCount()), midiBankMode()));
}

int SamplerUiSession::mappedMidiNote(const int pad) const noexcept
{
    return static_cast<int>(midichopper::midiNoteForPad(static_cast<std::uint32_t>(pad),
                                                        static_cast<std::uint8_t>(fState.baseNote),
                                                        midiBankMode()));
}

int SamplerUiSession::localPadFromVisualIndex(const int visualIndex) const noexcept
{
    return padLayout().localIndex(visualIndex);
}

bool SamplerUiSession::hasSelectedPad() const noexcept
{
    return fState.selectedPad >= 0 && fState.selectedPad < static_cast<int>(midichopper::kPadCount);
}

int SamplerUiSession::firstEmptyLocalPad() const noexcept
{
    for (int localPad = 0; localPad < visiblePadCount(); ++localPad) {
        const char state = fState.padState[static_cast<std::size_t>(localPad)];
        if (state == '0' || state == '.')
            return localPad;
    }
    return 0;
}

void SamplerUiSession::selectAutomaticArmTarget()
{
    const int localPad = firstEmptyLocalPad();
    fState.startPad = localPad;
    fState.selectedPad = globalPad(localPad);
    fState.hasWaveform = false;
    requestWaveform();
    fHost.sendUiParameter(kParameterStartPad, static_cast<float>(localPad + 1));
}

void SamplerUiSession::restoreLastPlayedSelection()
{
    if (fState.lastPlayedPad >= 0 && bankForGlobalPad(fState.lastPlayedPad) == fState.bank) {
        fState.selectedPad = fState.lastPlayedPad;
        requestWaveform();
        return;
    }
    fState.selectedPad = -1;
    fState.hasWaveform = false;
    fEditorSnapshot.complete();
}

void SamplerUiSession::normalizeSelectionForContext()
{
    if (!hasSelectedPad())
        return;
    const int localPad =
        std::clamp(localPadForGlobalPad(fState.selectedPad), 0, visiblePadCount() - 1);
    if (fState.editorMode) {
        selectEditorPad(globalPad(localPad));
        return;
    }
    if (fState.armed && fState.currentPad < 0) {
        fState.startPad = localPad;
        fState.selectedPad = globalPad(localPad);
        return;
    }
    if (!fState.armed && bankForGlobalPad(fState.selectedPad) != fState.bank)
        fState.selectedPad = -1;
}

sms::ui::BankedPadLayout SamplerUiSession::padLayout() const noexcept
{
    return sms::ui::BankedPadLayout(fState.layout);
}

bool SamplerUiSession::effectiveGlobalReverse() const noexcept
{
    return midichopper::ui::sampleGlobalReverse(fState.globalReverse,
                                                fState.mixerSettings.globalMixer);
}

bool SamplerUiSession::effectiveSampleReverse() const noexcept
{
    return midichopper::ui::sampleDisplayReversed(fState.mixerSettings.reverse >= 0.5f,
                                                  effectiveGlobalReverse());
}

sms::dsp::SamplePlaybackSettings SamplerUiSession::displayEditorSettings() const noexcept
{
    return midichopper::ui::sampleEditorSettings(fState.editorSettings, effectiveGlobalReverse());
}

void SamplerUiSession::setLocalStatus(const char *status)
{
    copyString(fState.status, status);
    fHost.repaintUi();
}

void SamplerUiSession::setControlValue(const uint32_t parameter, const float value)
{
    // Hosts are not required to echo a UI-originated parameter change back
    // to this UI immediately. Keep the visible state responsive, then send
    // the exact same value to the plugin.
    acceptParameter(parameter, value);
    fHost.sendUiParameter(parameter, value);
}

bool SamplerUiSession::resetEnabled() const noexcept
{
    return !fPadActions.busy() && !fChopEditor.applying() && !fPadActions.dialogPending();
}

void SamplerUiSession::clearAllPadContents()
{
    releasePlaybackPad();
    stopEditorAudition();
    if (fChopEditor.active())
        fChopEditor.cancel();
    fPadActions.closeMenu();
    fHost.invalidateUiInteraction(UiInteractionInvalidation::clearPads);
    fState.editorMode = false;
    fState.selectedPad = fState.lastPlayedPad = fState.currentPad = -1;
    fState.editorSettingsPad = fState.mixerSettingsPad = -1;
    fState.editorSettings = {};
    fState.mixerSettings = {};
    fEditorSnapshot.complete();
    fState.hasWaveform = false;
    fState.waveform = {};
    fWaveformDetail.reset();
    stopLocalPlayhead(false);
    fState.padState.fill('0');
    fState.padStatus.fill('0');
    fState.padColors.fill(0);
    fPadActions.clearClipboard();
    fHost.invalidateUiInteraction(UiInteractionInvalidation::clearPadsInputs);
    if (fCapabilities.stateTransport) {
        fHost.sendUiState(midichopper::plugin::kCaptureActionRequestKey, "reset");
    }
    setLocalStatus("Clear pads requested");
}

void SamplerUiSession::pressPlaybackPad(const int localPad)
{
    const int pad = globalPad(localPad);
    const int midiNote = mappedMidiNote(pad);
    const int previousMidiNote = fPadPress.press(localPad, midiNote);
    startLocalPlayhead(pad);
    if (fCapabilities.midiInput) {
        if (previousMidiNote >= 0)
            fHost.sendUiNote(static_cast<uint8_t>(previousMidiNote), 0);
        fHost.sendUiNote(static_cast<uint8_t>(midiNote), 127);
    } else {
        static_cast<void>(previousMidiNote);
    }
}

bool SamplerUiSession::selectedPadOccupied() const noexcept
{
    return hasSelectedPad() && bankForGlobalPad(fState.selectedPad) == fState.bank &&
           localPadForGlobalPad(fState.selectedPad) < visiblePadCount() &&
           fState.padState[static_cast<std::size_t>(localPadForGlobalPad(fState.selectedPad))] !=
               '0' &&
           fState.padState[static_cast<std::size_t>(localPadForGlobalPad(fState.selectedPad))] !=
               '.';
}

void SamplerUiSession::sendPlaybackRequest(const midichopper::plugin::PlaybackAction action,
                                           const int pad)
{
    if (fCapabilities.stateTransport) {
        const auto request = std::to_string(midichopper::plugin::encodePlaybackRequest(
            {action, pad >= 0 ? static_cast<std::uint32_t>(pad) : midichopper::kPadCount}));
        fHost.sendUiState("play_stop_request", request.c_str());
    } else {
        static_cast<void>(action);
        static_cast<void>(pad);
    }
}

void SamplerUiSession::stopEditorAudition()
{
    if (fEditorAuditionPad < 0)
        return;
    sendPlaybackRequest(midichopper::plugin::PlaybackAction::StopAudition);
    if (fPlaybackIndicator.pad() == fEditorAuditionPad)
        stopLocalPlayhead(false);
    fEditorAuditionPad = -1;
}

void SamplerUiSession::auditionSelectedEditorPad()
{
    if (fState.editorAuditionMode == midichopper::ui::EditorAuditionMode::Off ||
        !selectedPadOccupied())
        return;
    releasePlaybackPad();
    const bool loop = fState.editorAuditionMode == midichopper::ui::EditorAuditionMode::Loop;
    sendPlaybackRequest(loop ? midichopper::plugin::PlaybackAction::Loop
                             : midichopper::plugin::PlaybackAction::Play,
                        fState.selectedPad);
    fEditorAuditionPad = fState.selectedPad;
    fEditorAuditionLoop = loop;
    fState.anyPlaybackActive = true;
    startLocalPlayhead(fState.selectedPad, loop);
    fHost.repaintUi();
}

void SamplerUiSession::togglePlayStop()
{
    // The DSP decides whether to stop or play from its actual voice state.
    const bool selected = selectedPadOccupied();
    const bool loop =
        fState.editorMode && fState.editorAuditionMode == midichopper::ui::EditorAuditionMode::Loop;
    sendPlaybackRequest(loop ? midichopper::plugin::PlaybackAction::ToggleLoop
                             : midichopper::plugin::PlaybackAction::Toggle,
                        selected ? fState.selectedPad : -1);
    releasePlaybackPad();
    fEditorAuditionPad = -1;
    if (fState.anyPlaybackActive) {
        fState.anyPlaybackActive = false;
        stopLocalPlayhead(false);
    } else if (selected) {
        fState.anyPlaybackActive = true;
        fEditorAuditionPad = fState.selectedPad;
        fEditorAuditionLoop = loop;
        startLocalPlayhead(fState.selectedPad, loop);
    }
    fHost.repaintUi();
}

void SamplerUiSession::startLocalPlayhead(const int pad, const std::optional<bool> loop)
{
    const bool shouldLoop = loop.value_or(fEditorAuditionPad == pad ? fEditorAuditionLoop
                                                                    : fState.playbackMode >= 0.5f);
    if (fPlaybackIndicator.start(pad, fState.editorSettingsPad, displayEditorSettings(),
                                 Clock::now(), shouldLoop, false) &&
        fState.editorMode)
        fHost.repaintUi();
}

void SamplerUiSession::stopLocalPlayhead(const bool holdFinal)
{
    fPlaybackIndicator.stop(holdFinal, Clock::now());
    if (fState.editorMode)
        fHost.repaintUi();
}

void SamplerUiSession::updateLocalPlayhead(const Time now, const float effectiveGlobalTune)
{
    if (fPlaybackIndicator.update(now, fState.selectedPad, fState.hasWaveform, fState.waveform,
                                  displayEditorSettings(), fState.mixerSettings,
                                  effectiveGlobalTune) &&
        fState.editorMode)
        fHost.repaintUi();
}

void SamplerUiSession::releasePlaybackPad()
{
    const int midiNote = fPadPress.release();
    if (fCapabilities.midiInput) {
        if (midiNote >= 0)
            fHost.sendUiNote(static_cast<uint8_t>(midiNote), 0);
    } else {
        static_cast<void>(midiNote);
    }
}

void SamplerUiSession::requestWaveform()
{
    if (fCapabilities.stateTransport) {
        fWaveformDetail.reset();
        if (!hasSelectedPad())
            return;
        fEditorSnapshot.begin(fState.selectedPad);
        sendEditorSnapshotRequest(Clock::now());
    }
}

void SamplerUiSession::requestWaveformDetail()
{
    if (fCapabilities.stateTransport) {
        const auto request = fWaveformDetail.request(
            fChopEditor.active() ? fChopEditor.firstPad() : fState.selectedPad,
            static_cast<std::uint32_t>(fChopEditor.active() && !fChopEditor.split() ? 3 : 1),
            fState.editorMode || fChopEditor.active(), Clock::now());
        if (request) {
            const auto encoded = midichopper::plugin::encodeWaveformDetailRequest(*request);
            fHost.sendUiState("waveform_detail_request", encoded.c_str());
        }
    }
}

void SamplerUiSession::sendEditorSnapshotRequest(const std::chrono::steady_clock::time_point now)
{
    if (fCapabilities.stateTransport) {
        if (!fEditorSnapshot.pending())
            return;
        const auto encoded =
            midichopper::plugin::encodeEditorSnapshotRequest(fEditorSnapshot.request());
        fHost.sendUiState("waveform_request", encoded.c_str());
        fEditorSnapshotRetryDeadline = now + kEditorSnapshotRetryInterval;
    } else {
        static_cast<void>(now);
    }
}

void SamplerUiSession::commitEditorSnapshotIfReady()
{
    if (!fEditorSnapshot.readyFor(fState.selectedPad))
        return;
    const bool retainViewport = fEditorSnapshot.localEdit() &&
                                fState.waveform.pad == fEditorSnapshot.waveform().pad &&
                                fState.waveform.frames == fEditorSnapshot.waveform().frames;
    fState.waveform = fEditorSnapshot.waveform();
    if (!retainViewport)
        fWaveformDetail.reset(fState.waveform.frames);
    fState.hasWaveform = fState.waveform.frames != 0U;
    fState.editorSettings = fEditorSnapshot.playback();
    fState.editorSettingsPad = fState.selectedPad;
    fState.mixerSettings = fEditorSnapshot.mixer();
    fState.mixerSettingsPad = fState.selectedPad;
    fPlaybackIndicator.acceptSettings(fState.selectedPad, displayEditorSettings(), Clock::now());
    fEditorSnapshot.complete();
}

void SamplerUiSession::openSampleEditor(const int targetPad)
{
    const int pad = clampPad(static_cast<float>(targetPad));
    fState.editorMode = true;
    fState.status[0] = '\0';
    if (midichopper::ui::editorPadSelectionChanged(fState.selectedPad, pad))
        selectEditorPad(pad);
    else
        requestWaveform();
    fHost.repaintUi();
}

void SamplerUiSession::selectChopPad(const int pad)
{
    stopEditorAudition();
    fState.editorMode = false;
    fEditorSnapshot.complete();
    fState.selectedPad = pad;
    const int bank = bankForGlobalPad(pad);
    if (fState.bank != bank) {
        fState.bank = bank;
        fHost.sendUiParameter(kParameterActiveBank, static_cast<float>(bank + 1));
    }
}

void SamplerUiSession::leaveChopEditor()
{
    requestWaveform();
}

void SamplerUiSession::resetChopViewport(const std::uint64_t frames)
{
    fWaveformDetail.reset(frames);
}

void SamplerUiSession::setChopStatus(const char *const message)
{
    copyString(fState.status, message);
}

void SamplerUiSession::setChopStructureBusy(const bool busy)
{
    fPadActions.setStructureBusy(busy);
}

void SamplerUiSession::repaintChopEditor()
{
    fHost.repaintUi();
}

void SamplerUiSession::selectEditorPad(const int pad, const bool stopAudition)
{
    const int selectedPad = clampPad(static_cast<float>(pad));
    if (!midichopper::ui::editorPadSelectionChanged(fState.selectedPad, selectedPad))
        return;

    if (stopAudition)
        stopEditorAudition();
    fState.selectedPad = selectedPad;
    requestWaveform();
    fHost.repaintUi();
}

void SamplerUiSession::selectBank(const int bank)
{
    const int selectedBank = std::clamp(bank, 0, static_cast<int>(midichopper::kBankCount - 1));
    if (selectedBank == fState.bank || (fState.armed && fState.currentPad >= 0))
        return;
    if (fChopEditor.active()) {
        setLocalStatus("Apply or Exit before changing bank");
        return;
    }
    const int localPad = hasSelectedPad() ? std::clamp(localPadForGlobalPad(fState.selectedPad), 0,
                                                       visiblePadCount() - 1)
                                          : 0;
    fState.bank = selectedBank;
    if (fState.editorMode)
        selectEditorPad(globalPad(localPad));
    else {
        fState.selectedPad = -1;
        fState.hasWaveform = false;
        fEditorSnapshot.complete();
        if (!fState.armed)
            fState.lastPlayedPad = -1;
    }
    fHost.sendUiParameter(kParameterActiveBank, static_cast<float>(fState.bank + 1));
    fHost.repaintUi();
}

void SamplerUiSession::selectLayout(const int layout)
{
    if (fState.armed && fState.currentPad >= 0)
        return;
    const int selectedLayout =
        std::clamp(layout, static_cast<int>(parameterRanges::padLayout.minimum),
                   static_cast<int>(parameterRanges::padLayout.maximum));
    if (fChopEditor.active()) {
        setLocalStatus("Apply or Exit before changing layout");
        return;
    }
    // An explicit menu selection also remembers the current layout. Host
    // callbacks and automation never write this startup preference.
    const bool remembered = LayoutPreference::write(LayoutPreference::configPath(), selectedLayout);
    if (selectedLayout == fState.layout) {
        if (!remembered)
            setLocalStatus("Layout preference could not be saved");
        return;
    }
    stopEditorAudition();
    fState.layout = selectedLayout;
    if (fState.startPad >= visiblePadCount()) {
        fState.startPad = 0;
        setControlValue(kParameterStartPad, 1.0f);
    }
    normalizeSelectionForContext();
    setControlValue(kParameterPadLayout, static_cast<float>(fState.layout));
    if (!remembered)
        setLocalStatus("Layout changed, but the preference could not be saved");
    fHost.repaintUi();
}

void SamplerUiSession::selectMidiBankMode(const int mode)
{
    if (fState.armed && fState.currentPad >= 0)
        return;
    const int selectedMode = mode == 0 ? 0 : 1;
    if (fChopEditor.active()) {
        setLocalStatus("Apply or Exit before changing MIDI mode");
        return;
    }
    const bool remembered =
        BankModePreference::write(BankModePreference::configPath(), selectedMode);
    if (selectedMode != fState.midiBankMode)
        setControlValue(kParameterMidiBankMode, static_cast<float>(selectedMode));
    if (!remembered)
        setLocalStatus("Bank mode selected, but the preference could not be saved");
    fHost.repaintUi();
}

void SamplerUiSession::commitPlaybackSettings(const sms::dsp::SamplePlaybackSettings &settings)
{
    fState.editorSettings = settings;
    fState.editorSettingsPad = fState.selectedPad;
    if (fCapabilities.stateTransport) {
        const std::string encoded = sms::state::encodeSamplePlaybackSettings(fState.editorSettings);
        fHost.sendUiState(kPadEditStateKeys[static_cast<std::size_t>(fState.selectedPad)].c_str(),
                          encoded.c_str());
    }
    refreshPendingEditorSnapshot();
}

void SamplerUiSession::flipSampleDirection()
{
    if (!hasSelectedPad() || fState.editorSettingsPad != fState.selectedPad ||
        fState.mixerSettingsPad != fState.selectedPad)
        return;
    const auto now = Clock::now();
    fState.editorSettings = midichopper::ui::mirrorSampleRegion(fState.editorSettings);
    fState.mixerSettings.reverse = fState.mixerSettings.reverse >= 0.5f ? 0.0f : 1.0f;
    fState.mixerSettings = sms::dsp::sanitize(fState.mixerSettings);
    fState.editorSettingsPad = fState.mixerSettingsPad = fState.selectedPad;
    fWaveformDetail.setReversed(effectiveSampleReverse(), now);
    fPlaybackIndicator.flipDirection(fState.selectedPad, fState.waveform.frames,
                                     displayEditorSettings(), now);
    fEditorSnapshot.beginLocalEdit(fState.selectedPad, fState.editorSettings, fState.mixerSettings);
    if (fCapabilities.stateTransport) {
        const std::string encoded = midichopper::plugin::encodePadDirectionRequest(
            {static_cast<std::uint32_t>(fState.selectedPad), fState.editorSettings,
             fState.mixerSettings});
        fHost.sendUiState("pad_direction_request", encoded.c_str());
        sendEditorSnapshotRequest(now);
    }
    fHost.repaintUi();
}

void SamplerUiSession::toggleSampleGlobalMixer()
{
    if (!hasSelectedPad() || fState.editorSettingsPad != fState.selectedPad ||
        fState.mixerSettingsPad != fState.selectedPad)
        return;
    fState.mixerSettings.globalMixer = fState.mixerSettings.globalMixer >= 0.5f ? 0.0f : 1.0f;
    const auto now = Clock::now();
    if (fState.globalReverse)
        fPlaybackIndicator.flipDirection(fState.selectedPad, fState.waveform.frames,
                                         displayEditorSettings(), now);
    fHost.invalidateUiInteraction(UiInteractionInvalidation::sampleGlobalMixerChanged);
    commitMixerSettings(fState.mixerSettings);
    fHost.repaintUi();
}

void SamplerUiSession::commitMixerSettings(const sms::dsp::SampleMixerSettings &settings)
{
    fState.mixerSettings = sms::dsp::sanitize(settings);
    fWaveformDetail.setReversed(
        fState.editorMode && !fChopEditor.active() && effectiveSampleReverse(), Clock::now());
    fState.mixerSettingsPad = fState.selectedPad;
    if (fCapabilities.stateTransport) {
        const std::string encoded = sms::state::encodeSampleMixerSettings(fState.mixerSettings);
        fHost.sendUiState(kPadMixerStateKeys[static_cast<std::size_t>(fState.selectedPad)].c_str(),
                          encoded.c_str());
    }
    refreshPendingEditorSnapshot();
}

void SamplerUiSession::refreshPendingEditorSnapshot() noexcept
{
    // A queued snapshot predating this local edit must not overwrite it.
    if (fEditorSnapshot.pending()) {
        fEditorSnapshot.beginLocalEdit(fState.selectedPad, fState.editorSettings,
                                       fState.mixerSettings);
        fEditorSnapshotRetryDeadline = Clock::now();
    }
}

bool SamplerUiSession::parseEditorState(const char *const key, const char *const value)
{
    for (std::size_t pad = 0; pad < kPadEditStateKeys.size(); ++pad) {
        if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) != 0)
            continue;
        sms::dsp::SamplePlaybackSettings decoded;
        if (sms::state::decodeSamplePlaybackSettings(value, decoded)) {
            const int decodedPad = static_cast<int>(pad);
            if (!fEditorSnapshot.pending() && decodedPad == fState.selectedPad) {
                fState.editorSettings = decoded;
                fState.editorSettingsPad = decodedPad;
            }
            if (!fEditorSnapshot.pending())
                fPlaybackIndicator.acceptSettings(decodedPad, decoded, Clock::now());
        }
        return true;
    }
    return false;
}

bool SamplerUiSession::parseMixerState(const char *const key, const char *const value)
{
    for (std::size_t pad = 0; pad < kPadMixerStateKeys.size(); ++pad) {
        if (std::strcmp(key, kPadMixerStateKeys[pad].c_str()) != 0)
            continue;
        sms::dsp::SampleMixerSettings decoded;
        if (sms::state::decodeSampleMixerSettings(value, decoded)) {
            const int decodedPad = static_cast<int>(pad);
            if (!fEditorSnapshot.pending() && decodedPad == fState.selectedPad) {
                fState.mixerSettings = decoded;
                fState.mixerSettingsPad = decodedPad;
            }
        }
        return true;
    }
    return false;
}

bool SamplerUiSession::parsePadColorState(const char *const key, const char *const value)
{
    for (std::size_t pad = 0; pad < kPadColorStateKeys.size(); ++pad) {
        if (std::strcmp(key, kPadColorStateKeys[pad].c_str()) != 0)
            continue;
        fState.padColors[pad] =
            midichopper::plugin::decodePadColorIndex(value, fCapabilities.colorCount);
        return true;
    }
    return false;
}

midichopper::ui::PadActionContext SamplerUiSession::padActionContext() const noexcept
{
    return {globalPad(0),          visiblePadCount(),
            midiBankMode(),        fState.armed,
            fState.editorMode,     fState.selectedPad,
            fState.padState,       fState.editorSettingsPad,
            fState.editorSettings, fState.mixerSettingsPad,
            fState.mixerSettings};
}

void SamplerUiSession::setPadColor(const int pad, const int color)
{
    const auto index = static_cast<std::size_t>(pad);
    fState.padColors[index] = color;
    fHost.sendUiState(kPadColorStateKeys[index].c_str(), std::to_string(color).c_str());
}

void SamplerUiSession::idle(const Time now, const float effectiveGlobalTune)
{
    fPadActions.idle(now);
    if (fMeterRepaintPending && now >= fNextMeterRepaint) {
        fMeterRepaintPending = false;
        fNextMeterRepaint = now + kMeterFrameInterval;
        fHost.repaintUi();
    }
    fChopEditor.idle(now);
    fWaveformDetail.setReversed(
        fState.editorMode && !fChopEditor.active() && effectiveSampleReverse(), now);
    if (fEditorSnapshot.pending() && now >= fEditorSnapshotRetryDeadline)
        sendEditorSnapshotRequest(now);
    if (fWaveformDetail.requestDue(now))
        requestWaveformDetail();
    updateLocalPlayhead(now, effectiveGlobalTune);
}

void SamplerUiSession::rendered(const Time now) noexcept
{
    fMeterRepaintPending = false;
    fNextMeterRepaint = now + kMeterFrameInterval;
}

void SamplerUiSession::missedReplies()
{
    fPadActions.invalidatePendingStructure();
    if (fChopEditor.split() || fChopEditor.applying())
        fChopEditor.cancel();
    requestWaveform();
    setLocalStatus("UI replies were missed; check the pad and retry");
}

void SamplerUiSession::selectMainPadForContextMenu(const int pad)
{
    if (fState.selectedPad != pad) {
        fState.editorSettings = {};
        fState.editorSettingsPad = -1;
        fState.mixerSettings = {};
        fState.mixerSettingsPad = -1;
    }
    fState.selectedPad = pad;
    requestWaveform();
}

void SamplerUiSession::selectCaptureTarget(const int localPad)
{
    fState.selectedPad = globalPad(localPad);
    fState.hasWaveform = false;
    requestWaveform();
    setControlValue(kParameterStartPad, static_cast<float>(localPad + 1));
    fCaptureTargetRequestAlternateHalf = !fCaptureTargetRequestAlternateHalf;
    fHost.sendUiParameter(kParameterCaptureTargetRequest,
                          captureTargetRequestValue(static_cast<std::uint32_t>(globalPad(localPad)),
                                                    fCaptureTargetRequestAlternateHalf));
    setLocalStatus("Press Space or MIDI to start");
}

void SamplerUiSession::closeSampleEditor()
{
    releasePlaybackPad();
    stopEditorAudition();
    fState.editorMode = false;
    fHost.invalidateUiInteraction(UiInteractionInvalidation::sampleEditorClosed);
    restoreLastPlayedSelection();
    fHost.repaintUi();
}

void SamplerUiSession::captureSpace()
{
    if (!fCapabilities.midiInput)
        return;
    const auto note = static_cast<std::uint8_t>(mappedMidiNote(globalPad(0)));
    fHost.sendUiNote(note, 127);
    fHost.sendUiNote(note, 0);
}

void SamplerUiSession::captureAction(const char *action)
{
    if (fCapabilities.stateTransport)
        fHost.sendUiState(kCaptureActionRequestKey, action);
}

void SamplerUiSession::cycleEditorAuditionMode()
{
    stopEditorAudition();
    fState.editorAuditionMode = nextEditorAuditionMode(fState.editorAuditionMode);
    if (fState.editorAuditionMode == EditorAuditionMode::Loop)
        auditionSelectedEditorPad();
    fHost.repaintUi();
}

void SamplerUiSession::sendChopState(const char *key, const std::string &value)
{
    if (fCapabilities.stateTransport)
        fHost.sendUiState(key, value);
}

void SamplerUiSession::sendPadActionState(const char *key, const std::string &value)
{
    sendChopState(key, value);
}

bool SamplerUiSession::showPadFileBrowser(const bool saving, const char *defaultName)
{
    return fCapabilities.fileBrowser && fHost.showUiPadFileBrowser(saving, defaultName);
}

void SamplerUiSession::setPadActionStatus(const char *message)
{
    copyString(fState.status, message);
}

void SamplerUiSession::repaintPadActions()
{
    fHost.repaintUi();
}

void SamplerUiSession::clearPadActionHover() noexcept
{
    fHost.invalidateUiInteraction(UiInteractionInvalidation::padActionHover);
}

void SamplerUiSession::beginPadSampleEditor(const int pad)
{
    openSampleEditor(pad);
}

void SamplerUiSession::refreshPadWaveform()
{
    requestWaveform();
}

} // namespace midichopper::ui
