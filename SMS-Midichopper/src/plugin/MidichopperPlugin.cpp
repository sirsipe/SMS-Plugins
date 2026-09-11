#include "DistrhoPlugin.hpp"

#include "Audio/WaveformSummary.hpp"
#include "Audio/RealtimeAccessGate.hpp"
#include "Audio/WavCodec.hpp"
#include "DSP/PeakMeter.hpp"
#include "PadClipboard.hpp"
#include "PadClipboardProtocol.hpp"
#include "PadFileActionProtocol.hpp"
#include "PadFileActions.hpp"
#include "Parameters.hpp"
#include "StateCodec.hpp"
#include "SamplerEngine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

START_NAMESPACE_DISTRHO

namespace {

std::array<std::string, midichopper::kPadCount> makePadStateKeys(const char* const prefix)
{
    std::array<std::string, midichopper::kPadCount> keys;
    for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
        char key[24];
        std::snprintf(key, sizeof(key), "%s%02u", prefix, pad + 1U);
        keys[pad] = key;
    }
    return keys;
}

const auto kPadStateKeys = makePadStateKeys("pad_");
const auto kPadEditStateKeys = makePadStateKeys("pad_edit_");

constexpr std::uint32_t kAudioStateCount = midichopper::kPadCount;
constexpr std::uint32_t kEditStateOffset = kAudioStateCount;
constexpr std::uint32_t kWaveformRequestState = kEditStateOffset + midichopper::kPadCount;
constexpr std::uint32_t kWaveformDataState = kWaveformRequestState + 1U;
constexpr std::uint32_t kPadClearRequestState = kWaveformDataState + 1U;
constexpr std::uint32_t kPadFileRequestState = kPadClearRequestState + 1U;
constexpr std::uint32_t kPadFileBusyState = kPadFileRequestState + 1U;
constexpr std::uint32_t kPadFileStatusState = kPadFileBusyState + 1U;
constexpr std::uint32_t kPadClipboardRequestState = kPadFileStatusState + 1U;
constexpr std::uint32_t kStateCount = kPadClipboardRequestState + 1U;
constexpr const char* kWaveformRequestKey = "waveform_request";
constexpr const char* kWaveformDataKey = "waveform_data";
constexpr const char* kPadClearRequestKey = "pad_clear_request";
constexpr const char* kPadFileRequestKey = "pad_file_request";
constexpr const char* kPadFileBusyKey = "pad_file_busy";
constexpr const char* kPadFileStatusKey = "pad_file_status";
constexpr const char* kPadClipboardRequestKey = "pad_clipboard_request";
static_assert(midichopper::kPadCount <= 64U,
              "pending clear requests use one bit per pad");

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view path)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

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

[[nodiscard]] float decibelsToGain(const float decibels) noexcept
{
    return std::pow(10.0f, decibels / 20.0f);
}

} // namespace

class MidichopperPlugin final : public Plugin {
public:
    MidichopperPlugin()
        : Plugin(midichopper::plugin::kParameterCount, 0, kStateCount),
          sampler_(getSampleRate())
    {
        for (std::uint32_t index = 0; index < midichopper::plugin::kParameterCount; ++index)
            parameters_[index].store(defaultParameterValue(index), std::memory_order_relaxed);
    }

protected:
    const char* getLabel() const override { return "SMSMidichopper"; }
    const char* getDescription() const override
    {
        return "Capture incoming stereo audio into four banks of MIDI-controlled slices.";
    }
    const char* getMaker() const override { return "SudoMetalStudio"; }
    const char* getHomePage() const override
    {
        return "https://github.com/sirsipe/SMS-Plugins/tree/main/SMS-Midichopper";
    }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override
    {
        return d_version(MIDICHOPPER_VERSION_MAJOR,
                         MIDICHOPPER_VERSION_MINOR,
                         MIDICHOPPER_VERSION_PATCH);
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        port.name = input ? (index == 0 ? "Input Left" : "Input Right")
                          : (index == 0 ? "Output Left" : "Output Right");
        port.symbol = input ? (index == 0 ? "in_l" : "in_r")
                            : (index == 0 ? "out_l" : "out_r");
    }

    void initParameter(const uint32_t index, DISTRHO::Parameter& parameter) override
    {
        using namespace midichopper::plugin;
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
                           kParameterIsInteger, "0 = one-shot, 1 = gated.");
            break;
        case kParameterInputMonitor:
            setupParameter(index, parameter, "Input Monitor", "input_monitor", "",
                           kParameterIsBoolean | kParameterIsInteger, "Pass the input to the output.");
            break;
        case kParameterBaseMidiNote:
            setupParameter(index, parameter, "Base MIDI Note", "base_midi_note", "",
                           kParameterIsInteger,
                           "Pad 1 note; All Banks mode limits the effective base to 64.");
            break;
        case kParameterOutputGainDb:
            setupParameter(index, parameter, "Output Gain", "output_gain", "dB",
                           0, "Gain applied to monitored input and pads.");
            break;
        case kParameterFinalize:
            setupParameter(index, parameter, "Finalize", "finalize", "",
                           kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                           "Commit the currently open sequential slice.");
            break;
        case kParameterUndo:
            setupParameter(index, parameter, "Undo Last Slice", "undo", "",
                           kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                           "Discard the most recently committed slice.");
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

    void initState(const uint32_t index, State& state) override
    {
        if (index < kAudioStateCount) {
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
        } else {
            state.key = kPadClipboardRequestKey;
            state.label = "Pad Clipboard Request";
            state.defaultValue = "";
            state.hints = kStateIsOnlyForDSP;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return parameters_[index].load(std::memory_order_relaxed);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= midichopper::plugin::kParameterCount)
            return;

        // DPF/LV2 reports control parameters only when their value changes,
        // while a UI may send the same momentary command more than once. Turn
        // those calls into edges here so each click is honoured, even if the
        // host's last control-port value remains 1.
        if (index >= midichopper::plugin::kParameterFinalize &&
            index <= midichopper::plugin::kParameterClearAll && value >= 0.5f) {
            const auto bit = 1U << (index - midichopper::plugin::kParameterFinalize);
            pendingCommands_.fetch_or(bit, std::memory_order_release);
            parameters_[index].store(0.0f, std::memory_order_relaxed);
            return;
        }

        if (index == midichopper::plugin::kParameterCaptureTargetRequest) {
            const std::uint32_t pad =
                midichopper::plugin::padFromCaptureTargetRequest(value);
            if (pad < midichopper::kPadCount)
                pendingCaptureTarget_.store(pad + 1U, std::memory_order_release);
            parameters_[index].store(0.0f, std::memory_order_relaxed);
            return;
        }

        parameters_[index].store(value, std::memory_order_relaxed);
    }

    String getState(const char* const key) const override
    {
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadStateKeys[pad].c_str()) != 0)
                continue;
            midichopper::PadData snapshot;
            bool exported = false;
            if (!withSamplerPaused([&] {
                    exported = sampler_.exportPad(pad, snapshot);
                }) || !exported || snapshot.frames == 0U)
                return String();
            const auto sourceRate = static_cast<std::uint32_t>(std::clamp(snapshot.sampleRate, 1.0, 384000.0));
            return String(midichopper::plugin::encodePadState(snapshot, sourceRate).c_str());
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) == 0)
                return String(midichopper::plugin::encodePlaybackSettings(
                    sampler_.padPlaybackSettings(pad)).c_str());
        }
        if (std::strcmp(key, kWaveformRequestKey) == 0)
            return String("0");
        if (std::strcmp(key, kWaveformDataKey) == 0)
            return String();
        if (std::strcmp(key, kPadClearRequestKey) == 0)
            return String("0");
        if (std::strcmp(key, kPadFileRequestKey) == 0 ||
            std::strcmp(key, kPadFileStatusKey) == 0)
            return String();
        if (std::strcmp(key, kPadFileBusyKey) == 0)
            return String("0");
        if (std::strcmp(key, kPadClipboardRequestKey) == 0)
            return String();
        return String();
    }

    void setState(const char* const key, const char* const value) override
    {
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadStateKeys[pad].c_str()) != 0)
                continue;
            if (value == nullptr || value[0] == '\0') {
                static_cast<void>(withSamplerPaused([&] { sampler_.clearPad(pad); }));
                return;
            }
            midichopper::plugin::DecodedPadState decoded;
            if (midichopper::plugin::decodePadState(value, decoded))
                static_cast<void>(withSamplerPaused([&] {
                    static_cast<void>(sampler_.importPad(pad, decoded.pad));
                }));
            return;
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) != 0)
                continue;
            sms::dsp::SamplePlaybackSettings settings;
            if (midichopper::plugin::decodePlaybackSettings(value, settings))
                sampler_.setPadPlaybackSettings(pad, settings);
            return;
        }
        if (std::strcmp(key, kWaveformRequestKey) == 0) {
            const char* const input = value != nullptr ? value : "";
            char* end = nullptr;
            const auto requested = std::strtoul(input, &end, 10);
            if (end != input && *end == '\0' && requested < midichopper::kPadCount) {
                const auto pad = static_cast<std::uint32_t>(requested);
                const std::string waveform = makeWaveformState(pad);
                static_cast<void>(updateStateValue(kWaveformDataKey, waveform.c_str()));
                const std::string editor = midichopper::plugin::encodePlaybackSettings(
                    sampler_.padPlaybackSettings(pad));
                static_cast<void>(updateStateValue(kPadEditStateKeys[pad].c_str(), editor.c_str()));
            }
            return;
        }
        if (std::strcmp(key, kWaveformDataKey) == 0)
            return;
        if (std::strcmp(key, kPadClearRequestKey) == 0) {
            const char* const input = value != nullptr ? value : "";
            char* end = nullptr;
            const auto requested = std::strtoul(input, &end, 10);
            if (end != input && *end == '\0' && requested >= 1U &&
                requested <= midichopper::kPadCount) {
                const auto pad = static_cast<std::uint32_t>(requested - 1U);
                pendingClearPads_.fetch_or(
                    std::uint64_t{1} << pad, std::memory_order_release);
            }
            return;
        }
        if (std::strcmp(key, kPadFileRequestKey) == 0) {
            handlePadFileRequest(value != nullptr ? value : "");
            return;
        }
        if (std::strcmp(key, kPadFileBusyKey) == 0 ||
            std::strcmp(key, kPadFileStatusKey) == 0)
            return;
        if (std::strcmp(key, kPadClipboardRequestKey) == 0) {
            handlePadClipboardRequest(value != nullptr ? value : "");
            return;
        }
    }

    void run(const float** const inputs, float** const outputs, const uint32_t frames,
             const MidiEvent* const midiEvents, const uint32_t midiEventCount) override
    {
        const auto samplerAccess = samplerAccess_.audioAccess();
        if (samplerAccess.shouldYield()) {
            inputMeter_.process(inputs[0], inputs[1], frames, getSampleRate());
            std::fill_n(outputs[0], frames, 0.0f);
            std::fill_n(outputs[1], frames, 0.0f);
            outputMeter_.process(outputs[0], outputs[1], frames, getSampleRate());
            updateMeterOutputParameters();
            return;
        }
        applySettings();
        applyCommandTriggers();

        // Observe inputs before processing in case a host supplies in-place
        // buffers. This tap intentionally ignores every plug-in mode/setting.
        inputMeter_.process(inputs[0], inputs[1], frames, getSampleRate());

        std::array<midichopper::MidiEvent, 1024> events{};
        std::uint32_t eventCount = 0;
        for (uint32_t index = 0; index < midiEventCount && eventCount < events.size(); ++index) {
            const MidiEvent& event = midiEvents[index];
            if (event.size < 3U || event.frame >= frames)
                continue;
            const uint8_t status = event.data[0] & 0xf0U;
            if (status != 0x80U && status != 0x90U)
                continue;
            const uint8_t note = event.data[1] & 0x7fU;
            const uint8_t velocity = event.data[2] & 0x7fU;
            const auto type = (status == 0x90U && velocity != 0U)
                ? midichopper::MidiEventType::NoteOn : midichopper::MidiEventType::NoteOff;
            events[eventCount++] = {event.frame, note, velocity, type};
        }

        activateAllBanksMidiBank(events.data(), eventCount);
        sampler_.process(inputs[0], inputs[1], outputs[0], outputs[1], frames, events.data(), eventCount);
        // The output tap follows monitoring, pad voices, envelopes, and gain.
        outputMeter_.process(outputs[0], outputs[1], frames, getSampleRate());
        updateMeterOutputParameters();
        updatePlaybackPadEvent();
        updatePadOutputParameters();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        static_cast<void>(withSamplerPaused([&] {
            sampler_.setSampleRate(newSampleRate);
        }));
    }

private:
    [[nodiscard]] std::string makeWaveformState(const std::uint32_t pad) const
    {
        midichopper::PadData snapshot;
        bool exported = false;
        if (!withSamplerPaused([&] {
                exported = sampler_.exportPad(pad, snapshot);
            }) || !exported || snapshot.frames == 0U)
            return sms::audio::encodeWaveformSummary({pad, 0U, sampler_.sampleRate(), {}, {}});
        return sms::audio::encodeWaveformSummary(sms::audio::summarizeStereo(
            pad, snapshot.stereo.data(), snapshot.frames, snapshot.sampleRate));
    }

    template <class Callback>
    [[nodiscard]] bool withSamplerPaused(Callback&& callback)
    {
        return samplerAccess_.withPaused(std::forward<Callback>(callback));
    }

    template <class Callback>
    [[nodiscard]] bool withSamplerPaused(Callback&& callback) const
    {
        return samplerAccess_.withPaused(std::forward<Callback>(callback));
    }

    void publishFileStatus(const std::string& status)
    {
        static_cast<void>(updateStateValue(kPadFileStatusKey, status.c_str()));
    }

    void publishFileResult(const midichopper::plugin::PadFileResultCode result) noexcept
    {
        padFileResultAlternateHalf_ = !padFileResultAlternateHalf_;
        parameters_[midichopper::plugin::kParameterPadFileResultEvent].store(
            midichopper::plugin::padFileResultEventValue(
                result, padFileResultAlternateHalf_), std::memory_order_relaxed);
    }

    void publishClipboardResult(
        const midichopper::plugin::PadClipboardResultCode result) noexcept
    {
        padClipboardResultAlternateHalf_ = !padClipboardResultAlternateHalf_;
        parameters_[midichopper::plugin::kParameterPadClipboardResultEvent].store(
            midichopper::plugin::padClipboardResultEventValue(
                result, padClipboardResultAlternateHalf_), std::memory_order_relaxed);
    }

    void handlePadClipboardRequest(const std::string_view encoded)
    {
        try {
            handlePadClipboardRequestImpl(encoded);
        } catch (...) {
            publishClipboardResult(midichopper::plugin::PadClipboardResultCode::failed);
        }
    }

    void handlePadClipboardRequestImpl(const std::string_view encoded)
    {
        using midichopper::plugin::PadClipboardAction;
        midichopper::plugin::PadClipboardRequest request;
        if (!midichopper::plugin::decodePadClipboardRequest(encoded, request)) {
            publishClipboardResult(midichopper::plugin::PadClipboardResultCode::failed);
            return;
        }

        bool succeeded = false;
        const bool accessed = withSamplerPaused([&] {
            succeeded = request.action == PadClipboardAction::copy
                ? padClipboard_.copyFrom(sampler_, request.pad)
                : padClipboard_.pasteTo(sampler_, request.pad);
        });
        const auto result = !accessed || !succeeded
            ? midichopper::plugin::PadClipboardResultCode::failed
            : request.action == PadClipboardAction::copy
                ? midichopper::plugin::PadClipboardResultCode::copied
                : midichopper::plugin::PadClipboardResultCode::pasted;

        if (result == midichopper::plugin::PadClipboardResultCode::copied)
            parameters_[midichopper::plugin::kParameterPadClipboardAvailable].store(
                1.0f, std::memory_order_relaxed);
        if (result == midichopper::plugin::PadClipboardResultCode::pasted) {
            const std::string editor = midichopper::plugin::encodePlaybackSettings(
                sampler_.padPlaybackSettings(request.pad));
            static_cast<void>(updateStateValue(
                kPadEditStateKeys[request.pad].c_str(), editor.c_str()));
            const std::string waveform = makeWaveformState(request.pad);
            static_cast<void>(updateStateValue(kWaveformDataKey, waveform.c_str()));
        }
        publishClipboardResult(result);
    }

    void handlePadFileRequest(const std::string_view encoded)
    {
        try {
            handlePadFileRequestImpl(encoded);
        } catch (...) {
            static_cast<void>(updateStateValue(kPadFileStatusKey, "WAV action failed"));
            static_cast<void>(updateStateValue(kPadFileBusyKey, "0"));
            publishFileResult(midichopper::plugin::PadFileResultCode::failed);
        }
    }

    void handlePadFileRequestImpl(const std::string_view encoded)
    {
        using midichopper::plugin::PadFileAction;
        midichopper::plugin::PadFileRequest request;
        if (!midichopper::plugin::decodePadFileRequest(encoded, request)) {
            publishFileStatus("Invalid pad file request");
            publishFileResult(midichopper::plugin::PadFileResultCode::failed);
            return;
        }

        static_cast<void>(updateStateValue(kPadFileBusyKey, "1"));
        std::string status;
        auto result = midichopper::plugin::PadFileResultCode::failed;
        if (request.action == PadFileAction::import) {
            auto loaded = midichopper::plugin::readPadWav(pathFromUtf8(request.path));
            if (!loaded) {
                status = std::move(loaded.error);
            } else {
                midichopper::PadData replacement;
                replacement.sampleRate = loaded.audio.sampleRate;
                replacement.frames = loaded.audio.frames;
                replacement.stereo = std::move(loaded.audio.stereo);
                double sumSquares = 0.0;
                for (const float sample : replacement.stereo) {
                    replacement.peak = std::max(replacement.peak, std::abs(sample));
                    sumSquares += static_cast<double>(sample) * sample;
                }
                replacement.rms = static_cast<float>(std::sqrt(
                    sumSquares / static_cast<double>(replacement.stereo.size())));

                bool imported = false;
                if (!withSamplerPaused([&] {
                        imported = sampler_.importPad(request.pad, replacement);
                        if (imported)
                            sampler_.setPadPlaybackSettings(request.pad, {});
                    })) {
                    status = "Audio did not pause for WAV import";
                } else if (!imported) {
                    status = "Not enough sampler storage for WAV import";
                } else {
                    status = "WAV imported";
                    result = midichopper::plugin::PadFileResultCode::importSucceeded;
                    const std::string editor = midichopper::plugin::encodePlaybackSettings({});
                    static_cast<void>(updateStateValue(
                        kPadEditStateKeys[request.pad].c_str(), editor.c_str()));
                    const std::string waveform = makeWaveformState(request.pad);
                    static_cast<void>(updateStateValue(kWaveformDataKey, waveform.c_str()));
                }
            }
        } else {
            midichopper::PadData snapshot;
            sms::dsp::SamplePlaybackSettings settings;
            bool exported = false;
            if (!withSamplerPaused([&] {
                    exported = sampler_.exportPad(request.pad, snapshot);
                    settings = sampler_.padPlaybackSettings(request.pad);
                })) {
                status = "Audio did not pause for WAV export";
            } else if (!exported || snapshot.frames == 0U) {
                status = "Pad is empty";
            } else {
                sms::audio::WavAudio audio;
                audio.sampleRate = static_cast<std::uint32_t>(
                    std::clamp(snapshot.sampleRate, 1.0, 384000.0));
                audio.frames = snapshot.frames;
                audio.stereo = std::move(snapshot.stereo);
                if (request.action == PadFileAction::exportProcessed)
                    audio = sms::audio::renderProcessedStereo(audio, settings);
                status = midichopper::plugin::writePadWav(pathFromUtf8(request.path), audio);
                if (status.empty())
                {
                    status = request.action == PadFileAction::exportProcessed
                        ? "Processed WAV exported" : "WAV exported";
                    result = request.action == PadFileAction::exportProcessed
                        ? midichopper::plugin::PadFileResultCode::processedExportSucceeded
                        : midichopper::plugin::PadFileResultCode::rawExportSucceeded;
                }
            }
        }
        publishFileStatus(status);
        static_cast<void>(updateStateValue(kPadFileBusyKey, "0"));
        publishFileResult(result);
    }

    static void setupParameter(const std::uint32_t index, DISTRHO::Parameter& parameter,
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

    [[nodiscard]] static float defaultParameterValue(const std::uint32_t index) noexcept
    {
        return midichopper::plugin::parameterRange(index).defaultValue;
    }

    [[nodiscard]] float parameter(const std::uint32_t index) const noexcept
    {
        return parameters_[index].load(std::memory_order_relaxed);
    }

    [[nodiscard]] float clampedParameter(const std::uint32_t index) const noexcept
    {
        const auto range = midichopper::plugin::parameterRange(index);
        return std::clamp(parameter(index), range.minimum, range.maximum);
    }

    void applySettings() noexcept
    {
        using namespace midichopper::plugin;
        midichopper::EngineSettings settings;
        settings.armed = parameter(kParameterMode) >= 0.5f;
        settings.startPad = static_cast<std::uint8_t>(
            clampedParameter(kParameterStartPad) - parameterRanges::startPad.minimum);
        settings.preRollMilliseconds = clampedParameter(kParameterPreRollMs);
        settings.captureMode = parameter(kParameterCaptureMode) >= 0.5f
            ? midichopper::CaptureMode::FixedDuration : midichopper::CaptureMode::Sequential;
        settings.fixedLengthSeconds = clampedParameter(kParameterFixedLengthSeconds);
        settings.playbackMode = parameter(kParameterPlaybackMode) >= 0.5f
            ? midichopper::PlaybackMode::Gated : midichopper::PlaybackMode::OneShot;
        settings.monitorInput = parameter(kParameterInputMonitor) >= 0.5f;
        settings.baseNote = static_cast<std::uint8_t>(clampedParameter(kParameterBaseMidiNote));
        settings.gain = decibelsToGain(clampedParameter(kParameterOutputGainDb));
        settings.maxVoices = static_cast<std::uint8_t>(clampedParameter(kParameterMaxVoices));
        settings.activeBank = static_cast<std::uint8_t>(
            clampedParameter(kParameterActiveBank) - parameterRanges::activeBank.minimum);
        settings.midiBankMode = parameter(kParameterMidiBankMode) >= 0.5f
            ? midichopper::MidiBankMode::AllBanks : midichopper::MidiBankMode::SelectedBank;
        const auto layout = static_cast<std::uint32_t>(
            clampedParameter(kParameterPadLayout));
        settings.padsPerBank = midichopper::padsPerBankForLayout(layout);
        sampler_.setSettings(settings);
    }

    void applyCommandTriggers() noexcept
    {
        const uint32_t commands = pendingCommands_.exchange(0U, std::memory_order_acquire);
        if ((commands & 0x1U) != 0U) sampler_.finalizeRecording();
        if ((commands & 0x2U) != 0U) sampler_.undoLastSlice();
        if ((commands & 0x4U) != 0U) sampler_.clearAllPads();
        const std::uint64_t clearPads =
            pendingClearPads_.exchange(0U, std::memory_order_acquire);
        if (clearPads != 0U) {
            for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
                if ((clearPads & (std::uint64_t{1} << pad)) != 0U)
                    sampler_.clearPad(pad);
            }
        }
        const std::uint32_t captureTarget =
            pendingCaptureTarget_.exchange(0U, std::memory_order_acquire);
        if (captureTarget != 0U)
            sampler_.selectCaptureTarget(captureTarget - 1U);
    }

    void activateAllBanksMidiBank(const midichopper::MidiEvent* const events,
                                  const std::uint32_t eventCount) noexcept
    {
        using namespace midichopper::plugin;
        auto settings = sampler_.settings();
        if (settings.armed || settings.midiBankMode != midichopper::MidiBankMode::AllBanks)
            return;

        std::uint32_t bank = settings.activeBank;
        for (std::uint32_t index = 0; index < eventCount; ++index) {
            if (!events[index].isNoteOn())
                continue;
            const std::uint32_t pad = midichopper::padForMidiNote(
                events[index].note, settings.baseNote, settings.activeBank,
                settings.padsPerBank, settings.midiBankMode);
            if (pad < midichopper::kPadCount)
                bank = midichopper::bankForPad(
                    pad, settings.padsPerBank, settings.midiBankMode);
        }
        if (bank == settings.activeBank || bank >= midichopper::kBankCount)
            return;
        settings.activeBank = static_cast<std::uint8_t>(bank);
        sampler_.setSettings(settings);
        const float parameterValue =
            static_cast<float>(bank) + parameterRanges::activeBank.minimum;
        mirrorInputParameter(kParameterActiveBank, parameterValue);
    }

    void updatePadOutputParameters() noexcept
    {
        using namespace midichopper::plugin;
        auto settings = sampler_.settings();
        std::uint32_t currentCapturePad = midichopper::kPadCount;
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (sampler_.padMetadata(pad).recording) {
                currentCapturePad = pad;
                settings.activeBank = static_cast<std::uint8_t>(midichopper::bankForPad(
                    pad, settings.padsPerBank, settings.midiBankMode));
                break;
            }
        }
        parameters_[kParameterCurrentCapturePad].store(
            currentCapturePad < midichopper::kPadCount ?
                static_cast<float>(currentCapturePad + 1U) : 0.0f,
            std::memory_order_relaxed);
        const std::uint32_t captureTargetPad = sampler_.captureTargetPad();
        parameters_[kParameterCaptureTargetPad].store(
            captureTargetPad < midichopper::kPadCount ?
                static_cast<float>(captureTargetPad + 1U) : 0.0f,
            std::memory_order_relaxed);
        const std::uint32_t displayedCapturePad = currentCapturePad < midichopper::kPadCount
            ? currentCapturePad : captureTargetPad;
        if (displayedCapturePad < midichopper::kPadCount) {
            const std::uint32_t bank = midichopper::bankForPad(
                displayedCapturePad, settings.padsPerBank, settings.midiBankMode);
            const std::uint32_t localPad = midichopper::localPadInBank(
                displayedCapturePad, settings.padsPerBank, settings.midiBankMode);
            if (bank < midichopper::kBankCount && localPad < settings.padsPerBank) {
                settings.activeBank = static_cast<std::uint8_t>(bank);
                mirrorInputParameter(kParameterActiveBank,
                    static_cast<float>(bank) + parameterRanges::activeBank.minimum);
                mirrorInputParameter(kParameterStartPad,
                    static_cast<float>(localPad) + parameterRanges::startPad.minimum);
            }
        }
        const std::uint32_t bankBase = static_cast<std::uint32_t>(settings.activeBank) *
            midichopper::bankStride(settings.padsPerBank, settings.midiBankMode);
        for (std::uint32_t localPad = 0; localPad < midichopper::kPadsPerBank; ++localPad) {
            const bool visible = localPad < settings.padsPerBank;
            const midichopper::PadMetadata metadata =
                visible ? sampler_.padMetadata(bankBase + localPad) : midichopper::PadMetadata{};
            parameters_[kFirstPadStatusParameter + localPad].store(
                visible && metadata.occupied ? 1.0f : 0.0f, std::memory_order_relaxed);
            parameters_[kFirstPadActivityParameter + localPad].store(
                (metadata.recording || metadata.active) ? 1.0f : 0.0f, std::memory_order_relaxed);
        }
    }

    void updatePlaybackPadEvent() noexcept
    {
        using namespace midichopper::plugin;
        const midichopper::PlaybackTrigger trigger = sampler_.lastPlaybackTrigger();
        if (trigger.generation == publishedPlaybackTriggerGeneration_ ||
            trigger.pad >= midichopper::kPadCount)
            return;

        publishedPlaybackTriggerGeneration_ = trigger.generation;
        playbackPadEventAlternateHalf_ = !playbackPadEventAlternateHalf_;
        parameters_[kParameterPlaybackPadEvent].store(
            playbackPadEventValue(trigger.pad, playbackPadEventAlternateHalf_),
            std::memory_order_relaxed);
    }

    void updateMeterOutputParameters() noexcept
    {
        using namespace midichopper::plugin;
        parameters_[kParameterInputLevelLeft].store(
            inputMeter_.left(), std::memory_order_relaxed);
        parameters_[kParameterInputLevelRight].store(
            inputMeter_.right(), std::memory_order_relaxed);
        parameters_[kParameterOutputLevelLeft].store(
            outputMeter_.left(), std::memory_order_relaxed);
        parameters_[kParameterOutputLevelRight].store(
            outputMeter_.right(), std::memory_order_relaxed);
    }

    void mirrorInputParameter(const std::uint32_t index, const float value) noexcept
    {
        const float previous = parameters_[index].exchange(value, std::memory_order_relaxed);
        if (previous != value && canRequestParameterValueChanges())
            static_cast<void>(requestParameterValueChange(index, value));
    }

    midichopper::SamplerEngine sampler_;
    midichopper::PadClipboard padClipboard_;
    sms::dsp::StereoPeakMeter inputMeter_;
    sms::dsp::StereoPeakMeter outputMeter_;
    std::array<std::atomic<float>, midichopper::plugin::kParameterCount> parameters_{};
    std::atomic<std::uint32_t> pendingCommands_{0};
    std::atomic<std::uint64_t> pendingClearPads_{0};
    std::atomic<std::uint32_t> pendingCaptureTarget_{0};
    mutable sms::audio::RealtimeAccessGate samplerAccess_;
    std::uint64_t publishedPlaybackTriggerGeneration_ = 0;
    bool playbackPadEventAlternateHalf_ = false;
    bool padFileResultAlternateHalf_ = false;
    bool padClipboardResultAlternateHalf_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidichopperPlugin)
};

Plugin* createPlugin()
{
    return new MidichopperPlugin();
}

END_NAMESPACE_DISTRHO
