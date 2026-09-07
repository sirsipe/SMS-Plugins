#include "DistrhoPlugin.hpp"

#include "Audio/WaveformSummary.hpp"
#include "Parameters.hpp"
#include "StateCodec.hpp"
#include "SamplerEngine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

constexpr std::array<const char*, midichopper::kPadCount> kPadStateKeys{{
    "pad_01", "pad_02", "pad_03", "pad_04", "pad_05", "pad_06", "pad_07", "pad_08",
    "pad_09", "pad_10", "pad_11", "pad_12", "pad_13", "pad_14", "pad_15", "pad_16",
}};

constexpr std::array<const char*, midichopper::kPadCount> kPadEditStateKeys{{
    "pad_edit_01", "pad_edit_02", "pad_edit_03", "pad_edit_04",
    "pad_edit_05", "pad_edit_06", "pad_edit_07", "pad_edit_08",
    "pad_edit_09", "pad_edit_10", "pad_edit_11", "pad_edit_12",
    "pad_edit_13", "pad_edit_14", "pad_edit_15", "pad_edit_16",
}};

constexpr std::uint32_t kAudioStateCount = midichopper::kPadCount;
constexpr std::uint32_t kEditStateOffset = kAudioStateCount;
constexpr std::uint32_t kWaveformRequestState = kEditStateOffset + midichopper::kPadCount;
constexpr std::uint32_t kWaveformDataState = kWaveformRequestState + 1U;
constexpr std::uint32_t kStateCount = kWaveformDataState + 1U;
constexpr const char* kWaveformRequestKey = "waveform_request";
constexpr const char* kWaveformDataKey = "waveform_data";

constexpr std::array<const char*, midichopper::kPadCount> kPadOccupiedNames{{
    "Pad 1 Occupied", "Pad 2 Occupied", "Pad 3 Occupied", "Pad 4 Occupied", "Pad 5 Occupied", "Pad 6 Occupied", "Pad 7 Occupied", "Pad 8 Occupied",
    "Pad 9 Occupied", "Pad 10 Occupied", "Pad 11 Occupied", "Pad 12 Occupied", "Pad 13 Occupied", "Pad 14 Occupied", "Pad 15 Occupied", "Pad 16 Occupied",
}};

constexpr std::array<const char*, midichopper::kPadCount> kPadOccupiedSymbols{{
    "pad_1_occupied", "pad_2_occupied", "pad_3_occupied", "pad_4_occupied", "pad_5_occupied", "pad_6_occupied", "pad_7_occupied", "pad_8_occupied",
    "pad_9_occupied", "pad_10_occupied", "pad_11_occupied", "pad_12_occupied", "pad_13_occupied", "pad_14_occupied", "pad_15_occupied", "pad_16_occupied",
}};

constexpr std::array<const char*, midichopper::kPadCount> kPadActivityNames{{
    "Pad 1 Activity", "Pad 2 Activity", "Pad 3 Activity", "Pad 4 Activity", "Pad 5 Activity", "Pad 6 Activity", "Pad 7 Activity", "Pad 8 Activity",
    "Pad 9 Activity", "Pad 10 Activity", "Pad 11 Activity", "Pad 12 Activity", "Pad 13 Activity", "Pad 14 Activity", "Pad 15 Activity", "Pad 16 Activity",
}};

constexpr std::array<const char*, midichopper::kPadCount> kPadActivitySymbols{{
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
        return "Capture incoming stereo audio into 16 sequential MIDI-controlled slices.";
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
            setupParameter(parameter, "Mode", "mode", "", 0.0f, 0.0f, 1.0f,
                           kParameterIsInteger, "0 = Play, 1 = Arm capture.");
            break;
        case kParameterStartPad:
            setupParameter(parameter, "Start Pad", "start_pad", "", 1.0f, 1.0f, 16.0f,
                           kParameterIsInteger, "First destination pad when arming sequential capture.");
            break;
        case kParameterPreRollMs:
            setupParameter(parameter, "Pre-roll", "pre_roll", "ms", 0.0f, 0.0f, 100.0f,
                           kParameterIsInteger, "Audio retained immediately before a slice boundary.");
            break;
        case kParameterCaptureMode:
            setupParameter(parameter, "Capture Mode", "capture_mode", "", 0.0f, 0.0f, 1.0f,
                           kParameterIsInteger, "0 = Sequential boundaries, 1 = fixed-length capture.");
            break;
        case kParameterFixedLengthSeconds:
            setupParameter(parameter, "Fixed Length", "fixed_length", "s", 1.0f, 0.01f, 30.0f,
                           0, "Length of each pad when Capture Mode is Fixed.");
            break;
        case kParameterPlaybackMode:
            setupParameter(parameter, "Playback", "playback", "", 0.0f, 0.0f, 1.0f,
                           kParameterIsInteger, "0 = one-shot, 1 = gated.");
            break;
        case kParameterInputMonitor:
            setupParameter(parameter, "Input Monitor", "input_monitor", "", 1.0f, 0.0f, 1.0f,
                           kParameterIsBoolean | kParameterIsInteger, "Pass the input to the output.");
            break;
        case kParameterBaseMidiNote:
            setupParameter(parameter, "Base MIDI Note", "base_midi_note", "", 36.0f, 0.0f, 112.0f,
                           kParameterIsInteger, "Pad 1 note; pads occupy this note through +15.");
            break;
        case kParameterOutputGainDb:
            setupParameter(parameter, "Output Gain", "output_gain", "dB", 0.0f, -24.0f, 12.0f,
                           0, "Gain applied to monitored input and pads.");
            break;
        case kParameterFinalize:
            setupParameter(parameter, "Finalize", "finalize", "", 0.0f, 0.0f, 1.0f,
                           kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                           "Commit the currently open sequential slice.");
            break;
        case kParameterUndo:
            setupParameter(parameter, "Undo Last Slice", "undo", "", 0.0f, 0.0f, 1.0f,
                           kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                           "Discard the most recently committed slice.");
            break;
        case kParameterClearAll:
            setupParameter(parameter, "Clear All Pads", "clear_all", "", 0.0f, 0.0f, 1.0f,
                           kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger,
                           "Remove every captured sample.");
            break;
        case kParameterMaxVoices:
            setupParameter(parameter, "Max Voices", "max_voices", "", 16.0f, 1.0f, 16.0f,
                           kParameterIsInteger,
                           "Maximum number of pads that may play simultaneously.");
            break;
        default:
            if (index >= kFirstPadStatusParameter && index < kFirstPadActivityParameter) {
                const std::uint32_t pad = index - kFirstPadStatusParameter;
                setupParameter(parameter, kPadOccupiedNames[pad], kPadOccupiedSymbols[pad], "", 0.0f, 0.0f, 1.0f,
                               kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger,
                               "Whether this pad contains captured audio.");
            } else {
                const std::uint32_t pad = index - kFirstPadActivityParameter;
                setupParameter(parameter, kPadActivityNames[pad], kPadActivitySymbols[pad], "", 0.0f, 0.0f, 1.0f,
                               kParameterIsOutput, "1 while the pad is recording or playing.");
            }
            break;
        }
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index < kAudioStateCount) {
            state.key = kPadStateKeys[index];
            state.label = kPadOccupiedNames[index];
            state.defaultValue = "";
            // Keep potentially large audio blobs on DSP only. DPF/LV2 know this is
            // already Base64; it must not be sent over the DSP<->UI state channel.
            state.hints = kStateIsBase64Blob | kStateIsOnlyForDSP;
        } else if (index < kWaveformRequestState) {
            const auto pad = index - kEditStateOffset;
            state.key = kPadEditStateKeys[pad];
            state.label = "Pad Sample Editor Settings";
            state.defaultValue = "SP1;0;1;0;0;1;0";
            state.hints = kStateIsHostReadable;
        } else if (index == kWaveformRequestState) {
            state.key = kWaveformRequestKey;
            state.label = "Waveform Request";
            state.defaultValue = "0";
            state.hints = kStateIsOnlyForDSP;
        } else {
            state.key = kWaveformDataKey;
            state.label = "Waveform Display Data";
            state.defaultValue = "";
            state.hints = kStateIsHostReadable;
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

        parameters_[index].store(value, std::memory_order_relaxed);
    }

    String getState(const char* const key) const override
    {
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadStateKeys[pad]) != 0)
                continue;
            midichopper::PadData snapshot;
            if (!sampler_.exportPad(pad, snapshot) || snapshot.frames == 0U)
                return String();
            const auto sourceRate = static_cast<std::uint32_t>(std::clamp(snapshot.sampleRate, 1.0, 384000.0));
            return String(midichopper::plugin::encodePadState(snapshot, sourceRate).c_str());
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadEditStateKeys[pad]) == 0)
                return String(midichopper::plugin::encodePlaybackSettings(
                    sampler_.padPlaybackSettings(pad)).c_str());
        }
        if (std::strcmp(key, kWaveformRequestKey) == 0)
            return String("0");
        if (std::strcmp(key, kWaveformDataKey) == 0)
            return String();
        return String();
    }

    void setState(const char* const key, const char* const value) override
    {
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadStateKeys[pad]) != 0)
                continue;
            if (value == nullptr || value[0] == '\0') {
                sampler_.clearPad(pad);
                return;
            }
            midichopper::plugin::DecodedPadState decoded;
            if (midichopper::plugin::decodePadState(value, decoded))
                static_cast<void>(sampler_.importPad(pad, decoded.pad));
            return;
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadEditStateKeys[pad]) != 0)
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
                static_cast<void>(updateStateValue(kPadEditStateKeys[pad], editor.c_str()));
            }
            return;
        }
        if (std::strcmp(key, kWaveformDataKey) == 0)
            return;
    }

    void run(const float** const inputs, float** const outputs, const uint32_t frames,
             const MidiEvent* const midiEvents, const uint32_t midiEventCount) override
    {
        applySettings();
        applyCommandTriggers();

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

        sampler_.process(inputs[0], inputs[1], outputs[0], outputs[1], frames, events.data(), eventCount);
        updatePadOutputParameters();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        sampler_.setSampleRate(newSampleRate);
    }

private:
    [[nodiscard]] std::string makeWaveformState(const std::uint32_t pad) const
    {
        midichopper::PadData snapshot;
        if (!sampler_.exportPad(pad, snapshot) || snapshot.frames == 0U)
            return sms::audio::encodeWaveformSummary({pad, 0U, sampler_.sampleRate(), {}, {}});
        return sms::audio::encodeWaveformSummary(sms::audio::summarizeStereo(
            pad, snapshot.stereo.data(), snapshot.frames, snapshot.sampleRate));
    }

    static void setupParameter(DISTRHO::Parameter& parameter, const char* const name, const char* const symbol,
                               const char* const unit, const float def, const float min, const float max,
                               const uint32_t extraHints, const char* const description)
    {
        parameter.hints |= extraHints;
        parameter.name = name;
        parameter.symbol = symbol;
        parameter.unit = unit;
        parameter.description = description;
        parameter.ranges = ParameterRanges(def, min, max);
    }

    [[nodiscard]] static float defaultParameterValue(const std::uint32_t index) noexcept
    {
        using namespace midichopper::plugin;
        switch (index) {
        case kParameterStartPad: return 1.0f;
        case kParameterFixedLengthSeconds: return 1.0f;
        case kParameterInputMonitor: return 1.0f;
        case kParameterBaseMidiNote: return 36.0f;
        case kParameterMaxVoices: return 16.0f;
        default: return 0.0f;
        }
    }

    [[nodiscard]] float parameter(const std::uint32_t index) const noexcept
    {
        return parameters_[index].load(std::memory_order_relaxed);
    }

    void applySettings() noexcept
    {
        using namespace midichopper::plugin;
        midichopper::EngineSettings settings;
        settings.armed = parameter(kParameterMode) >= 0.5f;
        settings.startPad = static_cast<std::uint8_t>(std::clamp(parameter(kParameterStartPad), 1.0f, 16.0f) - 1.0f);
        settings.preRollMilliseconds = std::clamp(parameter(kParameterPreRollMs), 0.0f, 100.0f);
        settings.captureMode = parameter(kParameterCaptureMode) >= 0.5f
            ? midichopper::CaptureMode::FixedDuration : midichopper::CaptureMode::Sequential;
        settings.fixedLengthSeconds = std::clamp(static_cast<double>(parameter(kParameterFixedLengthSeconds)), 0.01, 30.0);
        settings.playbackMode = parameter(kParameterPlaybackMode) >= 0.5f
            ? midichopper::PlaybackMode::Gated : midichopper::PlaybackMode::OneShot;
        settings.monitorInput = parameter(kParameterInputMonitor) >= 0.5f;
        settings.baseNote = static_cast<std::uint8_t>(std::clamp(parameter(kParameterBaseMidiNote), 0.0f, 112.0f));
        settings.gain = decibelsToGain(std::clamp(parameter(kParameterOutputGainDb), -24.0f, 12.0f));
        settings.maxVoices = static_cast<std::uint8_t>(
            std::clamp(parameter(kParameterMaxVoices), 1.0f, 16.0f));
        sampler_.setSettings(settings);
    }

    void applyCommandTriggers() noexcept
    {
        const uint32_t commands = pendingCommands_.exchange(0U, std::memory_order_acquire);
        if ((commands & 0x1U) != 0U) sampler_.finalizeRecording();
        if ((commands & 0x2U) != 0U) sampler_.undoLastSlice();
        if ((commands & 0x4U) != 0U) sampler_.clearAllPads();
    }

    void updatePadOutputParameters() noexcept
    {
        using namespace midichopper::plugin;
        for (uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            const midichopper::PadMetadata metadata = sampler_.padMetadata(pad);
            parameters_[kFirstPadStatusParameter + pad].store(metadata.occupied ? 1.0f : 0.0f,
                                                               std::memory_order_relaxed);
            parameters_[kFirstPadActivityParameter + pad].store(
                (metadata.recording || metadata.active) ? 1.0f : 0.0f, std::memory_order_relaxed);
        }
    }

    midichopper::SamplerEngine sampler_;
    std::array<std::atomic<float>, midichopper::plugin::kParameterCount> parameters_{};
    std::atomic<std::uint32_t> pendingCommands_{0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidichopperPlugin)
};

Plugin* createPlugin()
{
    return new MidichopperPlugin();
}

END_NAMESPACE_DISTRHO
