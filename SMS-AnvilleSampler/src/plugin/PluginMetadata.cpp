#include "PluginMetadata.hpp"

#include "CaptureActionProtocol.hpp"
#include "PadDirectionRequest.hpp"
#include "StateKeys.hpp"

namespace midichopper::plugin {

using namespace DISTRHO;

namespace {

constexpr std::uint32_t kAudioStateCount = midichopper::kPadCount;
constexpr std::uint32_t kEditStateOffset = kAudioStateCount;
constexpr std::uint32_t kWaveformRequestState = kEditStateOffset + midichopper::kPadCount;
constexpr std::uint32_t kWaveformDataState = kWaveformRequestState + 1U;
constexpr std::uint32_t kPadClearRequestState = kWaveformDataState + 1U;
constexpr std::uint32_t kPadFileRequestState = kPadClearRequestState + 1U;
constexpr std::uint32_t kPadFileBusyState = kPadFileRequestState + 1U;
constexpr std::uint32_t kPadFileStatusState = kPadFileBusyState + 1U;
constexpr std::uint32_t kPadClipboardRequestState = kPadFileStatusState + 1U;
constexpr std::uint32_t kChopApplyRequestState = kPadClipboardRequestState + 1U;
constexpr std::uint32_t kChopStatusState = kChopApplyRequestState + 1U;
constexpr std::uint32_t kChopPreviewRequestState = kChopStatusState + 1U;
constexpr std::uint32_t kChopMidiPreviewState = kChopPreviewRequestState + 1U;
constexpr std::uint32_t kMixerStateOffset = kChopMidiPreviewState + 1U;
constexpr std::uint32_t kPadStructureRequestState =
    kMixerStateOffset + midichopper::kPadCount;
constexpr std::uint32_t kPadStructureStatusState = kPadStructureRequestState + 1U;
constexpr std::uint32_t kWaveformDetailRequestState = kPadStructureStatusState + 1U;
constexpr std::uint32_t kWaveformDetailDataState = kWaveformDetailRequestState + 1U;
constexpr std::uint32_t kPadColorStateOffset = kWaveformDetailDataState + 1U;
constexpr std::uint32_t kPlayStopRequestState = kPadColorStateOffset + midichopper::kPadCount;
constexpr std::uint32_t kChopSnapshotRequestState = kPlayStopRequestState + 1U;
constexpr std::uint32_t kChopSnapshotDataState = kChopSnapshotRequestState + 1U;
constexpr std::uint32_t kCaptureActionRequestState = kChopSnapshotDataState + 1U;
constexpr std::uint32_t kPadDirectionRequestState = kCaptureActionRequestState + 1U;
constexpr std::uint32_t kGlobalControlStateOffset = kPadDirectionRequestState + 1U;
constexpr std::uint32_t kStateCount = kGlobalControlStateOffset + kGlobalControlStateKeys.size();
constexpr std::array<const char*, midichopper::kPadsPerBank> kPadOccupiedNames{{
    "Pad 1 Occupied", "Pad 2 Occupied", "Pad 3 Occupied", "Pad 4 Occupied", "Pad 5 Occupied", "Pad 6 Occupied", "Pad 7 Occupied", "Pad 8 Occupied",
    "Pad 9 Occupied", "Pad 10 Occupied", "Pad 11 Occupied", "Pad 12 Occupied", "Pad 13 Occupied", "Pad 14 Occupied", "Pad 15 Occupied", "Pad 16 Occupied",
}};

constexpr std::array<const char*, midichopper::kPadsPerBank> kPadOccupiedSymbols{{
    "pad_1_occupied", "pad_2_occupied", "pad_3_occupied", "pad_4_occupied", "pad_5_occupied", "pad_6_occupied", "pad_7_occupied", "pad_8_occupied",
    "pad_9_occupied", "pad_10_occupied", "pad_11_occupied", "pad_12_occupied", "pad_13_occupied", "pad_14_occupied", "pad_15_occupied", "pad_16_occupied",
}};

constexpr std::array<const char*, midichopper::kPadsPerBank> kPadActivityNames{{
    "Pad 1 Activity", "Pad 2 Activity", "Pad 3 Activity", "Pad 4 Activity", "Pad 5 Activity", "Pad 6 Activity", "Pad 7 Activity", "Pad 8 Activity",
    "Pad 9 Activity", "Pad 10 Activity", "Pad 11 Activity", "Pad 12 Activity", "Pad 13 Activity", "Pad 14 Activity", "Pad 15 Activity", "Pad 16 Activity",
}};

constexpr std::array<const char*, midichopper::kPadsPerBank> kPadActivitySymbols{{
    "pad_1_activity", "pad_2_activity", "pad_3_activity", "pad_4_activity", "pad_5_activity", "pad_6_activity", "pad_7_activity", "pad_8_activity",
    "pad_9_activity", "pad_10_activity", "pad_11_activity", "pad_12_activity", "pad_13_activity", "pad_14_activity", "pad_15_activity", "pad_16_activity",
}};

void setupParameter(const std::uint32_t index, DISTRHO::Parameter& parameter,
                    const char* const name, const char* const symbol, const char* const unit,
                    const uint32_t extraHints, const char* const description)
{
    parameter.hints |= extraHints;
    parameter.name = name;
    parameter.symbol = symbol;
    parameter.unit = unit;
    parameter.description = description;
    const auto range = midichopper::plugin::parameterRange(index);
    parameter.ranges = ParameterRanges(range.defaultValue, range.minimum, range.maximum);
}

} // namespace

std::uint32_t pluginStateCount() noexcept
{
    return kStateCount;
}

void initPluginParameter(const uint32_t index, DISTRHO::Parameter& parameter,
                         const int initialLayout, const int initialBankMode)
{
    parameter.hints = kParameterIsAutomatable;
    switch (index) {
    case kParameterMode:
        setupParameter(index, parameter, "Mode", "mode", "",
                       kParameterIsInteger, "0 = Play, 1 = Arm capture.");
        break;
    case kParameterStartPad:
        setupParameter(index, parameter, "Start Pad", "start_pad", "",
                       kParameterIsInteger, "First destination pad when arming sequential capture.");
        break;
    case kParameterPreRollMs:
        setupParameter(index, parameter, "Pre-roll", "pre_roll", "ms",
                       kParameterIsInteger, "Audio retained immediately before a slice boundary.");
        break;
    case kParameterCaptureMode:
        setupParameter(index, parameter, "Capture Mode", "capture_mode", "",
                       kParameterIsInteger, "0 = Sequential boundaries, 1 = fixed-length capture.");
        break;
    case kParameterFixedLengthSeconds:
        setupParameter(index, parameter, "Fixed Length", "fixed_length", "s",
                       0, "Length of each pad when Capture Mode is Fixed.");
        break;
    case kParameterPlaybackMode:
        setupParameter(index, parameter, "Playback", "playback", "",
                       kParameterIsInteger, "0 = One Shot, 1 = Hold.");
        break;
    case kParameterInputMonitor:
        setupParameter(index, parameter, "Input Monitor", "input_monitor", "",
                       kParameterIsInteger,
                       "0 = Off, 1 = On, 2 = Auto (on while armed).");
        break;
    case kParameterBaseMidiNote:
        setupParameter(index, parameter, "Base MIDI Note", "base_midi_note", "",
                       kParameterIsInteger,
                       "Pad 1 note; All Banks mode limits the effective base to 64.");
        break;
    case kParameterOutputGainDb:
        setupParameter(index, parameter, "Global Volume", "output_gain", "dB",
                       0, "Gain applied to pad playback and raw previews.");
        break;
    case kParameterMonitorGainDb:
        setupParameter(index, parameter, "Monitor Gain", "monitor_gain_db", "dB",
                       0, "Gain applied only to monitored input; recorded audio is unchanged.");
        break;
    case kParameterGlobalPan:
        setupParameter(index, parameter, "Global Pan", "global_pan", "",
                       0, "Stereo balance added to each pad's Pan value.");
        break;
    case kParameterGlobalTuneSemitones:
        setupParameter(index, parameter, "Global Tune", "global_tune", "st",
                       0, "Varispeed semitones added to each pad's Tune value.");
        break;
    case kParameterGlobalLowpass:
        setupParameter(index, parameter, "Global Lowpass", "global_lowpass", "",
                       0, "Lowpass amount, 0 = off; resonance rises with amount.");
        break;
    case kParameterGlobalHighpass:
        setupParameter(index, parameter, "Global Highpass", "global_highpass", "",
                       0, "Highpass amount, 0 = off; resonance rises with amount.");
        break;
    case kParameterGlobalFilterSlope:
        setupParameter(index, parameter, "Global Filter Slope", "global_filter_slope", "",
                       kParameterIsInteger, "0 = 6, 1 = 12, 2 = 24 dB per octave.");
        break;
    case kParameterGlobalDirty:
        setupParameter(index, parameter, "Global Dirty", "global_dirty", "",
                       kParameterIsBoolean | kParameterIsInteger,
                       "Mono 12-bit, 26.04 kHz sample-and-hold before filters and tune.");
        break;
    case kParameterGlobalReverse:
        setupParameter(index, parameter, "Global Reverse", "global_reverse", "",
                       kParameterIsBoolean | kParameterIsInteger,
                       "Turn all pad voices at their current positions; combines with each pad's direction.");
        break;
    case kParameterFinalize:
        setupParameter(index, parameter, "Finalize", "finalize", "",
                       kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                       "Commit the currently open sequential slice.");
        break;
    case kParameterUndo:
        setupParameter(index, parameter, "Undo Last Slice", "undo", "",
                       kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                       "Discard the open slice, or the last committed slice when idle.");
        break;
    case kParameterClearAll:
        setupParameter(index, parameter, "Clear All Pads", "clear_all", "",
                       kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                       "Remove every captured sample.");
        break;
    case kParameterMaxVoices:
        setupParameter(index, parameter, "Max Voices", "max_voices", "",
                       kParameterIsInteger,
                       "Maximum number of pads that may play simultaneously.");
        break;
    case kParameterActiveBank:
        setupParameter(index, parameter, "Active Bank", "active_bank", "",
                       kParameterIsInteger,
                       "Visible bank and capture start; also the playback target in Selected Bank mode.");
        break;
    case kParameterPadLayout:
        setupParameter(index, parameter, "Pad Layout", "pad_layout", "",
                       kParameterIsInteger,
                       "0 = 16 pads, 1 = 12 pads, 2 = 8 pads per bank.");
        // DPF's VST3 cache starts from this default. Keep it coherent with
        // the constructor seed so a restored host value of zero is applied.
        // Dummy/export instances retain the factory default in LV2 metadata.
        parameter.ranges.def = static_cast<float>(initialLayout);
        break;
    case kParameterCurrentCapturePad:
        setupParameter(index, parameter, "Current Capture Pad", "current_capture_pad", "",
                       kParameterIsOutput | kParameterIsInteger,
                       "Global pad number currently receiving captured audio; zero when idle.");
        break;
    case kParameterMidiBankMode:
        setupParameter(index, parameter, "MIDI Bank Mode", "midi_bank_mode", "",
                       kParameterIsBoolean | kParameterIsInteger,
                       "0 = selected bank shares one note range, 1 = all banks use unique notes.");
        // Match the constructor snapshot, as for the layout parameter.
        parameter.ranges.def = static_cast<float>(initialBankMode);
        parameter.enumValues.count = 2;
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = new ParameterEnumerationValue[2]{
            {0.0f, "Selected Bank"}, {1.0f, "All Banks"},
        };
        break;
    case kParameterPlaybackPadEvent:
        setupParameter(index, parameter, "Playback Pad Event", "playback_pad_event", "",
                       kParameterIsOutput | kParameterIsInteger | kParameterIsHidden,
                       "Internal UI notification identifying the most recently played pad.");
        break;
    case kParameterCaptureTargetPad:
        setupParameter(index, parameter, "Capture Target Pad", "capture_target_pad", "",
                       kParameterIsOutput | kParameterIsInteger | kParameterIsHidden,
                       "Internal UI notification identifying the current capture destination.");
        break;
    case kParameterCaptureTargetRequest:
        setupParameter(index, parameter, "Capture Target Request", "capture_target_request", "",
                       kParameterIsInteger | kParameterIsHidden,
                       "Internal UI command selecting a global idle armed capture destination.");
        break;
    case kParameterInputLevelLeft:
        setupParameter(index, parameter, "Input Level Left", "input_level_left", "",
                       kParameterIsOutput | kParameterIsHidden,
                       "Live sample peak of the raw left input.");
        break;
    case kParameterInputLevelRight:
        setupParameter(index, parameter, "Input Level Right", "input_level_right", "",
                       kParameterIsOutput | kParameterIsHidden,
                       "Live sample peak of the raw right input.");
        break;
    case kParameterOutputLevelLeft:
        setupParameter(index, parameter, "Output Level Left", "output_level_left", "",
                       kParameterIsOutput | kParameterIsHidden,
                       "Live sample peak of the final left output.");
        break;
    case kParameterOutputLevelRight:
        setupParameter(index, parameter, "Output Level Right", "output_level_right", "",
                       kParameterIsOutput | kParameterIsHidden,
                       "Live sample peak of the final right output.");
        break;
    case kParameterPadFileResultEvent:
        setupParameter(index, parameter, "Pad File Result Event", "pad_file_result_event", "",
                       kParameterIsOutput | kParameterIsInteger | kParameterIsHidden,
                       "Internal UI notification that a pad file action completed.");
        break;
    case kParameterPadClipboardAvailable:
        setupParameter(index, parameter, "Pad Clipboard Available", "pad_clipboard_available", "",
                       kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger |
                           kParameterIsHidden,
                       "Whether the internal pad clipboard contains a sample.");
        break;
    case kParameterPadClipboardResultEvent:
        setupParameter(index, parameter, "Pad Clipboard Result Event",
                       "pad_clipboard_result_event", "",
                       kParameterIsOutput | kParameterIsInteger | kParameterIsHidden,
                       "Internal UI notification that a pad Copy or Paste completed.");
        break;
    case kParameterChopPreviewPosition:
        setupParameter(index, parameter, "Chop Preview Position",
                       "chop_preview_position", "",
                       kParameterIsOutput | kParameterIsHidden,
                       "Internal UI playhead for raw Chop Editor preview.");
        break;
    case kParameterPlaybackPosition:
        setupParameter(index, parameter, "Sample Playback Position",
                       "sample_playback_position", "",
                       kParameterIsOutput | kParameterIsHidden,
                       "Internal UI playhead for the most recently triggered pad.");
        break;
    case kParameterStopAllPlayback:
        setupParameter(index, parameter, "Stop All Playback", "stop_all_playback", "",
                       kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger |
                           kParameterIsHidden,
                       "Immediately cut all playing samples.");
        break;
    case kParameterAnyPlaybackActive:
        setupParameter(index, parameter, "Any Playback Active", "any_playback_active", "",
                       kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger |
                           kParameterIsHidden,
                       "One while any sample or raw preview is playing.");
        break;
    case kParameterCaptureStorageExhausted:
        setupParameter(index, parameter, "Capture Storage Exhausted", "capture_storage_exhausted", "",
                       kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger |
                           kParameterIsHidden,
                       "Recording stopped because no prepared capture storage was available.");
        break;
    default:
        if (index >= kFirstPadStatusParameter && index < kFirstPadActivityParameter) {
            const std::uint32_t pad = index - kFirstPadStatusParameter;
            setupParameter(index, parameter, kPadOccupiedNames[pad], kPadOccupiedSymbols[pad], "",
                           kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger,
                           "Whether this pad contains captured audio.");
        } else {
            const std::uint32_t pad = index - kFirstPadActivityParameter;
            setupParameter(index, parameter, kPadActivityNames[pad], kPadActivitySymbols[pad], "",
                           kParameterIsOutput, "1 while the pad is recording or playing.");
        }
        break;
    }
}

void initPluginState(const uint32_t index, DISTRHO::State& state,
                 const std::array<GlobalValueSettings, kGlobalValueControls.size()>& initialSettings)
{
    if (index >= kGlobalControlStateOffset) {
        const auto control = index - kGlobalControlStateOffset;
        state.key = kGlobalControlStateKeys[control];
        state.label = "Global Knob Settings";
        state.defaultValue = encodeGlobalValueSettings(initialSettings[control]).c_str();
        state.hints = 0; // Private durable DPF state, never a host parameter.
    } else if (index < kAudioStateCount) {
        state.key = kPadStateKeys[index].c_str();
        state.label = "Pad Sample Audio";
        state.defaultValue = "";
        // Keep potentially large audio blobs on DSP only. DPF/LV2 know this is
        // already Base64; it must not be sent over the DSP<->UI state channel.
        state.hints = kStateIsBase64Blob | kStateIsOnlyForDSP;
    } else if (index < kWaveformRequestState) {
        const auto pad = index - kEditStateOffset;
        state.key = kPadEditStateKeys[pad].c_str();
        state.label = "Pad Sample Editor Settings";
        state.defaultValue = "SP1;0;1;0;0;1;0";
        // This is persisted plug-in state, but it is not a host-editable
        // string parameter. Keeping it private also makes DPF use its
        // direct DSP/UI key-value transport instead of LV2 patch messages.
        state.hints = 0;
    } else if (index == kWaveformRequestState) {
        state.key = kWaveformRequestKey;
        state.label = "Waveform Request";
        state.defaultValue = "0";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kWaveformDataState) {
        state.key = kWaveformDataKey;
        state.label = "Waveform Display Data";
        state.defaultValue = "";
        // A waveform summary is transient display data, not project state.
        state.hints = kStateIsOnlyForUI;
    } else if (index == kPadClearRequestState) {
        state.key = kPadClearRequestKey;
        state.label = "Clear Pad Request";
        state.defaultValue = "0";
        // This command is transient. setState only publishes an atomic
        // request; the sampler mutation happens at an audio block boundary.
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kPadFileRequestState) {
        state.key = kPadFileRequestKey;
        state.label = "Pad File Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kPadFileBusyState) {
        state.key = kPadFileBusyKey;
        state.label = "Pad File Busy";
        state.defaultValue = "0";
        state.hints = kStateIsOnlyForUI;
    } else if (index == kPadFileStatusState) {
        state.key = kPadFileStatusKey;
        state.label = "Pad File Status";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForUI;
    } else if (index == kPadClipboardRequestState) {
        state.key = kPadClipboardRequestKey;
        state.label = "Pad Clipboard Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kChopApplyRequestState) {
        state.key = kChopApplyRequestKey;
        state.label = "Chop Apply Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kChopStatusState) {
        state.key = kChopStatusKey;
        state.label = "Chop Editor Status";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForUI;
    } else if (index == kChopPreviewRequestState) {
        state.key = kChopPreviewRequestKey;
        state.label = "Chop Preview Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kChopMidiPreviewState) {
        state.key = kChopMidiPreviewKey;
        state.label = "Chop MIDI Preview Map";
        state.defaultValue = "CM1;0";
        state.hints = kStateIsOnlyForDSP;
    } else if (index < kPadStructureRequestState) {
        const auto pad = index - kMixerStateOffset;
        state.key = kPadMixerStateKeys[pad].c_str();
        state.label = "Pad Sample Mixer Settings";
        state.defaultValue = "MX4;0;0;0;0;0;0;0;0;1";
        state.hints = 0;
    } else if (index == kPadStructureRequestState) {
        state.key = kPadStructureRequestKey;
        state.label = "Pad Structure Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kWaveformDetailRequestState) {
        state.key = kWaveformDetailRequestKey;
        state.label = "Visible Waveform Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kWaveformDetailDataState) {
        state.key = kWaveformDetailDataKey;
        state.label = "Visible Waveform Data";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForUI;
    } else if (index == kPlayStopRequestState) {
        state.key = kPlayStopRequestKey;
        state.label = "Play/Stop Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kChopSnapshotRequestState) {
        state.key = kChopSnapshotRequestKey;
        state.label = "Chop Snapshot Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kChopSnapshotDataState) {
        state.key = kChopSnapshotDataKey;
        state.label = "Chop Snapshot Data";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForUI;
    } else if (index == kPadDirectionRequestState) {
        state.key = midichopper::plugin::kPadDirectionRequestKey;
        state.label = "Pad Direction Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index == kCaptureActionRequestState) {
        state.key = midichopper::plugin::kCaptureActionRequestKey;
        state.label = "Capture Action Request";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
    } else if (index >= kPadColorStateOffset) {
        state.key = kPadColorStateKeys[index - kPadColorStateOffset].c_str();
        state.label = "Pad Color";
        state.defaultValue = "0";
        state.hints = 0;
    } else {
        state.key = kPadStructureStatusKey;
        state.label = "Pad Structure Status";
        state.defaultValue = "";
        state.hints = kStateIsOnlyForUI;
    }
}

} // namespace midichopper::plugin
