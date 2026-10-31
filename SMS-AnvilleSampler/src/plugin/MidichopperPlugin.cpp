#include "DistrhoPlugin.hpp"

#include "Audio/WaveformSummary.hpp"
#include "Audio/RealtimeAccessGate.hpp"
#include "Audio/RealtimeCommandDispatcher.hpp"
#include "Audio/RealtimeLatestMailbox.hpp"
#include "ControlWorker.hpp"
#include "ChopEditorProtocol.hpp"
#include "DSP/PeakMeter.hpp"
#include "EditorSnapshotProtocol.hpp"
#include "PadWorkflows.hpp"
#include "PadDirectionRequest.hpp"
#include "StateKeys.hpp"
#include "PadClipboardProtocol.hpp"
#include "PadColorState.hpp"
#include "PadFileActionProtocol.hpp"
#include "PadStructureProtocol.hpp"
#include "Parameters.hpp"
#include "GlobalValueMode.hpp"
#include "KnobPreference.hpp"
#include "LayoutPreference.hpp"
#include "BankModePreference.hpp"
#include "CaptureActionProtocol.hpp"
#include "PluginUiBridge.hpp"
#include "PluginMetadata.hpp"
#include "PluginControls.hpp"
#include "StateCodec.hpp"
#include "WaveformDetailProtocol.hpp"
#include "SamplerEngine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

START_NAMESPACE_DISTRHO

namespace {

using namespace midichopper::plugin;

// DPF's LV2 updateStateValue synchronously echoes the published value through
// setState. That notification must not mutate the engine a second time.
thread_local const void* publishingUiInstance = nullptr;

struct HostMidiCursor {
    const MidiEvent* events = nullptr;
    std::uint32_t count = 0;
    std::uint32_t frames = 0;
    std::uint32_t index = 0;

    static bool next(void* const opaque, midichopper::MidiEvent& result) noexcept
    {
        auto& cursor = *static_cast<HostMidiCursor*>(opaque);
        while (cursor.index < cursor.count) {
            const MidiEvent& event = cursor.events[cursor.index++];
            if (event.size < 3U || event.frame >= cursor.frames)
                continue;
            const std::uint8_t status = event.data[0] & 0xf0U;
            if (status != 0x80U && status != 0x90U)
                continue;
            const std::uint8_t velocity = event.data[2] & 0x7fU;
            result = {event.frame, static_cast<std::uint8_t>(event.data[1] & 0x7fU),
                velocity, status == 0x90U && velocity != 0U
                    ? midichopper::MidiEventType::NoteOn
                    : midichopper::MidiEventType::NoteOff};
            return true;
        }
        return false;
    }
};

} // namespace

class MidichopperPlugin final : public PluginUiBridge,
                                private midichopper::plugin::PadWorkflowObserver {
public:
    MidichopperPlugin()
        : PluginUiBridge(midichopper::plugin::kParameterCount, pluginStateCount()),
          initialLayout_(isDummyInstance() ? 0 : LayoutPreference::read(LayoutPreference::configPath())),
          initialBankMode_(isDummyInstance() ? BankModePreference::defaultMode
              : BankModePreference::read(BankModePreference::configPath())),
          initialGlobalValueSettings_(isDummyInstance() ? defaultGlobalValueSettings()
              : KnobPreference::read(KnobPreference::configPath())),
          sampler_(getSampleRate()),
          controls_(initialLayout_, initialBankMode_),
          padWorkflows_(sampler_, controlDispatcher_, *this)
    {
        sampler_.setControlDispatcher(&controlDispatcher_,
            &sms::audio::RealtimeCommandDispatcher::dispatchFromEngine);
    }

    ~MidichopperPlugin() override
    {
        controlDispatcher_.shutdown();
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        controlWorker_.stop();
#endif
        sampler_.setControlDispatcher(nullptr, nullptr);
    }

    [[nodiscard]] std::uint64_t uiMessageCursor() const override
    {
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        return uiMessageBus_.cursor();
#else
        return 1U;
#endif
    }

    [[nodiscard]] bool readUiMessage(
        std::uint64_t& cursor,
        midichopper::plugin::UiMessageBus::Message& message,
        bool& skipped) const override
    {
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        return uiMessageBus_.read(cursor, message, skipped);
#else
        static_cast<void>(cursor);
        static_cast<void>(message);
        skipped = false;
        return false;
#endif
    }

    void stopUiPreview() override
    {
        const std::string encoded =
            midichopper::plugin::encodeChopMidiPreviewRequest({});
        setState(kChopMidiPreviewKey, encoded.c_str());
    }

protected:
    const char* getLabel() const override { return "SMSAnvilleSampler"; }
    const char* getDescription() const override
    {
        return "Capture incoming stereo audio into four banks of MIDI-controlled slices.";
    }
    const char* getMaker() const override { return "SudoMetalStudio"; }
    const char* getHomePage() const override
    {
        return "https://github.com/sirsipe/SMS-Plugins/tree/main/SMS-AnvilleSampler";
    }
    // Additional permission is shipped in HOST-LINKING-EXCEPTION.txt.
    const char* getLicense() const override { return "AGPL-3.0-only"; }
    uint32_t getVersion() const override
    {
        return d_version(ANVILLESAMPLER_VERSION_MAJOR,
                         ANVILLESAMPLER_VERSION_MINOR,
                         ANVILLESAMPLER_VERSION_PATCH);
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
#ifdef ANVILLESAMPLER_VST3
        // Cubase routes audio into instruments through an auxiliary input bus.
        // DPF maps this hint to VST3 kAux, retaining the stereo channel group.
        if (input)
            port.hints |= kAudioPortIsSidechain;
#endif
        port.name = input ? (index == 0 ? "Input Left" : "Input Right")
                          : (index == 0 ? "Output Left" : "Output Right");
        port.symbol = input ? (index == 0 ? "in_l" : "in_r")
                            : (index == 0 ? "out_l" : "out_r");
    }

    void initParameter(const uint32_t index, DISTRHO::Parameter& parameter) override
    {
        initPluginParameter(index, parameter, initialLayout_, initialBankMode_);
    }

    void initState(const uint32_t index, State& state) override
    {
        initPluginState(index, state, initialGlobalValueSettings_);
    }

    float getParameterValue(const uint32_t index) const override
    {
        return controls_.value(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        controls_.setParameterValue(index, value);
    }

    String getState(const char* const key) const override
    {
        for (std::size_t i = 0; i < kGlobalControlStateKeys.size(); ++i) {
            if (std::strcmp(key, kGlobalControlStateKeys[i]) == 0) {
                const std::lock_guard lock(globalValueSettingsMutex_);
                return String(encodeGlobalValueSettings(desiredGlobalValueSettings_[i]).c_str());
            }
        }
        const std::lock_guard lock(padWorkflows_.mutationMutex());
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadStateKeys[pad].c_str()) != 0)
                continue;
            midichopper::PadData snapshot;
            if (!sampler_.exportPublishedPad(pad, snapshot) || snapshot.frames == 0U)
                return String();
            const auto sourceRate = static_cast<std::uint32_t>(std::clamp(snapshot.sampleRate, 1.0, 384000.0));
            return String(midichopper::plugin::encodePadState(snapshot, sourceRate).c_str());
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) == 0) {
                sms::dsp::SamplePlaybackSettings settings;
                sms::dsp::SampleMixerSettings mixer;
                sampler_.snapshotPadSettings(pad, settings, mixer);
                return String(midichopper::plugin::encodePlaybackSettings(
                    settings).c_str());
            }
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadMixerStateKeys[pad].c_str()) == 0) {
                sms::dsp::SamplePlaybackSettings playback;
                sms::dsp::SampleMixerSettings settings;
                sampler_.snapshotPadSettings(pad, playback, settings);
                return String(midichopper::plugin::encodeMixerSettings(
                    settings).c_str());
            }
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadColorStateKeys[pad].c_str()) == 0)
                return String(std::to_string(
                    padColors_[pad].load(std::memory_order_relaxed)).c_str());
        }
        if (std::strcmp(key, kWaveformRequestKey) == 0)
            return String();
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
        if (std::strcmp(key, kChopApplyRequestKey) == 0 ||
            std::strcmp(key, kChopStatusKey) == 0 ||
            std::strcmp(key, kChopPreviewRequestKey) == 0 ||
            std::strcmp(key, kChopMidiPreviewKey) == 0)
            return String();
        if (std::strcmp(key, kPadStructureRequestKey) == 0 ||
            std::strcmp(key, kPadStructureStatusKey) == 0)
            return String();
        if (std::strcmp(key, midichopper::plugin::kCaptureActionRequestKey) == 0)
            return String();
        if (std::strcmp(key, midichopper::plugin::kPadDirectionRequestKey) == 0)
            return String();
        if (std::strcmp(key, kPlayStopRequestKey) == 0)
            return String();
        if (std::strcmp(key, kWaveformDetailRequestKey) == 0 ||
            std::strcmp(key, kWaveformDetailDataKey) == 0)
            return String();
        if (std::strcmp(key, kChopSnapshotRequestKey) == 0 ||
            std::strcmp(key, kChopSnapshotDataKey) == 0)
            return String();
        return String();
    }

    void setState(const char* const key, const char* const value) override
    {
        if (publishingUiInstance == this)
            return;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        const bool transient = std::strcmp(key, kWaveformRequestKey) == 0 ||
            (std::strcmp(key, midichopper::plugin::kCaptureActionRequestKey) == 0 &&
                value != nullptr && std::strcmp(value, "reset") == 0) ||
            std::strcmp(key, kWaveformDetailRequestKey) == 0 ||
            std::strcmp(key, kPadFileRequestKey) == 0 ||
            std::strcmp(key, kPadClipboardRequestKey) == 0 ||
            std::strcmp(key, kChopApplyRequestKey) == 0 ||
            std::strcmp(key, kChopSnapshotRequestKey) == 0 ||
            std::strcmp(key, kPadStructureRequestKey) == 0;
        if (transient) {
            if (value == nullptr || value[0] == '\0')
                return;
            if (!controlWorker_.submit([this, command = std::string(key),
                                        payload = std::string(value)] {
                    try { applyState(command.c_str(), payload.c_str()); }
                    catch (...) { reportControlFailure(command.c_str(), payload.c_str()); }
                }))
                reportControlFailure(key, value);
            return;
        }
#endif
        applyState(key, value);
    }

    void applyState(const char* const key, const char* const value)
    {
        for (std::size_t i = 0; i < kGlobalControlStateKeys.size(); ++i) {
            if (std::strcmp(key, kGlobalControlStateKeys[i]) != 0)
                continue;
            GlobalValueSettings settings;
            if (value != nullptr && decodeGlobalValueSettings(
                    kGlobalValueControls[i].parameter, value, settings)) {
                const std::lock_guard lock(globalValueSettingsMutex_);
                desiredGlobalValueSettings_[i] = settings;
                globalValueSettingsMailbox_.publish(desiredGlobalValueSettings_);
                // Publish while control writers are serialized so views see accepted order.
                const auto encoded = encodeGlobalValueSettings(settings);
                publishUiState(key, encoded.c_str());
            }
            return;
        }
        if (std::strcmp(key, kCaptureActionRequestKey) == 0 &&
            value != nullptr && std::strcmp(value, "reset") == 0) {
            padWorkflows_.reset();
            // The initiating UI invalidates its snapshots/colors locally;
            // per-slot replies would overflow the bounded UI message bus.
            for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad)
                padColors_[pad].store(0, std::memory_order_relaxed);
            publishUiState(kPadFileStatusKey, "Pads cleared");
            return;
        }
        if (controls_.queueStateCommand(key, value))
            return;
        if (std::strcmp(key, kChopSnapshotRequestKey) == 0) {
            midichopper::plugin::ChopSnapshotRequest request;
            if (midichopper::plugin::decodeChopSnapshotRequest(value, request)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                midichopper::plugin::ChopSnapshotReply reply;
                reply.request = request;
                const bool captured = sampler_.getChopSnapshot(
                    request.firstPad, request.padCount, reply.waveforms,
                    std::span<std::uint64_t>{reply.generations.data(), request.padCount});
                const auto encoded = captured
                    ? midichopper::plugin::encodeChopSnapshotReply(reply)
                    : midichopper::plugin::encodeChopSnapshotError(
                        request.sequence, "Pads changed; reopen Adjust Cut Points");
                publishUiState(kChopSnapshotDataKey, encoded.c_str());
            }
            return;
        }
        if (std::strcmp(key, kChopSnapshotDataKey) == 0)
            return;
        if (std::strcmp(key, midichopper::plugin::kPadDirectionRequestKey) == 0) {
            midichopper::plugin::PadDirectionRequest request;
            if (value != nullptr && midichopper::plugin::decodePadDirectionRequest(value, request)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                sampler_.restorePadEditorSettings(request.pad, request.playback, request.mixer);
            }
            return;
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadColorStateKeys[pad].c_str()) != 0)
                continue;
            padColors_[pad].store(midichopper::plugin::decodePadColorIndex(
                value != nullptr ? value : "", 255), std::memory_order_relaxed);
            return;
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadStateKeys[pad].c_str()) != 0)
                continue;
            if (value == nullptr || value[0] == '\0') {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                static_cast<void>(sampler_.restorePad(pad, nullptr));
                return;
            }
            midichopper::plugin::DecodedPadState decoded;
            if (midichopper::plugin::decodePadState(value, decoded)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                static_cast<void>(sampler_.restorePad(pad, &decoded.pad));
            }
            return;
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadMixerStateKeys[pad].c_str()) != 0)
                continue;
            sms::dsp::SampleMixerSettings settings;
            if (midichopper::plugin::decodeMixerSettings(value, settings)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                static_cast<void>(sampler_.restorePadMixerSettings(pad, settings));
            }
            return;
        }
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if (std::strcmp(key, kPadEditStateKeys[pad].c_str()) != 0)
                continue;
            sms::dsp::SamplePlaybackSettings settings;
            if (midichopper::plugin::decodePlaybackSettings(value, settings)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                static_cast<void>(sampler_.restorePadPlaybackSettings(pad, settings));
            }
            return;
        }
        if (std::strcmp(key, kWaveformRequestKey) == 0) {
            const char* const input = value != nullptr ? value : "";
            midichopper::plugin::EditorSnapshotRequest request;
            if (midichopper::plugin::decodeEditorSnapshotRequest(input, request)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                midichopper::plugin::EditorSnapshotReply reply;
                reply.request = request;
                std::string waveform;
                if (padWorkflows_.makeWaveformState(request.pad, waveform, &reply.playback,
                                      &reply.mixer, &reply.waveform)) {
                    const auto encoded = midichopper::plugin::encodeEditorSnapshotReply(reply);
                    publishUiState(kWaveformDataKey, encoded.c_str());
                }
                return;
            }
            char* end = nullptr;
            const auto requested = std::strtoul(input, &end, 10);
            if (end != input && *end == '\0' && requested < midichopper::kPadCount) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                const auto pad = static_cast<std::uint32_t>(requested);
                std::string waveform;
                sms::dsp::SamplePlaybackSettings playbackSettings;
                sms::dsp::SampleMixerSettings mixerSettings;
                if (padWorkflows_.makeWaveformState(pad, waveform, &playbackSettings, &mixerSettings)) {
                    publishUiState(kWaveformDataKey, waveform.c_str());
                    const std::string editor = midichopper::plugin::encodePlaybackSettings(
                        playbackSettings);
                    publishUiState(
                        kPadEditStateKeys[pad].c_str(), editor.c_str());
                    const std::string mixer = midichopper::plugin::encodeMixerSettings(
                        mixerSettings);
                    publishUiState(
                        kPadMixerStateKeys[pad].c_str(), mixer.c_str());
                }
            }
            return;
        }
        if (std::strcmp(key, kWaveformDataKey) == 0)
            return;
        if (std::strcmp(key, kWaveformDetailRequestKey) == 0) {
            midichopper::plugin::WaveformDetailRequest request;
            if (value != nullptr &&
                midichopper::plugin::decodeWaveformDetailRequest(value, request)) {
                const std::lock_guard lock(padWorkflows_.mutationMutex());
                sms::audio::WaveformSummary summary;
                if (sampler_.summarizePadRange(request.firstPad, request.padCount,
                                               request.start, request.end, summary)) {
                    const auto encoded = midichopper::plugin::encodeWaveformDetailReply(
                        {request, summary});
                    publishUiState(kWaveformDetailDataKey, encoded.c_str());
                }
            }
            return;
        }
        if (std::strcmp(key, kWaveformDetailDataKey) == 0)
            return;
        if (std::strcmp(key, kPadFileRequestKey) == 0) {
            if (value != nullptr && value[0] != '\0')
                padWorkflows_.handlePadFileRequest(value);
            return;
        }
        if (std::strcmp(key, kPadFileBusyKey) == 0 ||
            std::strcmp(key, kPadFileStatusKey) == 0)
            return;
        if (std::strcmp(key, kPadClipboardRequestKey) == 0) {
            if (value != nullptr && value[0] != '\0')
                padWorkflows_.handlePadClipboardRequest(value);
            return;
        }
        if (std::strcmp(key, kChopApplyRequestKey) == 0) {
            if (value != nullptr && value[0] != '\0')
                padWorkflows_.handleChopApplyRequest(value);
            return;
        }
        if (std::strcmp(key, kChopStatusKey) == 0)
            return;
        if (std::strcmp(key, kPadStructureRequestKey) == 0) {
            if (value != nullptr && value[0] != '\0')
                padWorkflows_.handlePadStructureRequest(value);
            return;
        }
        if (std::strcmp(key, kPadStructureStatusKey) == 0)
            return;
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
        sampler_.applyPendingState();
        controlDispatcher_.service();
        static_cast<void>(globalValueSettingsMailbox_.tryConsume(activeGlobalValueSettings_));
        controls_.applyToEngine(sampler_, activeGlobalValueSettings_);

        // Observe inputs before processing in case a host supplies in-place
        // buffers. This tap intentionally ignores every plug-in mode/setting.
        inputMeter_.process(inputs[0], inputs[1], frames, getSampleRate());

        activateAllBanksMidiBank(midiEvents, midiEventCount, frames);
        HostMidiCursor cursor{midiEvents, midiEventCount, frames};
        sampler_.process(inputs[0], inputs[1], outputs[0], outputs[1], frames,
            midichopper::MidiEventSource{&cursor, &HostMidiCursor::next});
        // The output tap follows monitoring, pad voices, envelopes, and gain.
        outputMeter_.process(outputs[0], outputs[1], frames, getSampleRate());
        updateMeterOutputParameters();
        updatePlaybackPadEvent();
        updatePadOutputParameters();
        controls_.publishParameterValue(midichopper::plugin::kParameterChopPreviewPosition,
            sampler_.chopPreviewPosition());
        controls_.publishParameterValue(midichopper::plugin::kParameterPlaybackPosition,
            sampler_.playbackPosition());
        controls_.publishParameterValue(midichopper::plugin::kParameterAnyPlaybackActive,
            sampler_.anyPlaybackActive() ? 1.0f : 0.0f);
    }

    void activate() override { controlDispatcher_.activate(); }
    void deactivate() override { controlDispatcher_.deactivate(); }

    void sampleRateChanged(const double newSampleRate) override
    {
        const std::lock_guard lock(padWorkflows_.mutationMutex());
        static_cast<void>(withSamplerPaused([&] {
            sampler_.setSampleRate(newSampleRate);
        }));
    }

private:
    void reportControlFailure(const char* key, const char* value)
    {
        if (std::strcmp(key, midichopper::plugin::kCaptureActionRequestKey) == 0) {
            publishUiState(kPadFileStatusKey, "Clear pads failed; retry when audio is running");
        } else if (std::strcmp(key, kPadFileRequestKey) == 0) {
            publishUiState(kPadFileStatusKey, "WAV action failed; retry when audio is running");
            publishUiState(kPadFileBusyKey, "0");
            publishFileResult(midichopper::plugin::PadFileResultCode::failed);
        } else if (std::strcmp(key, kPadClipboardRequestKey) == 0) {
            publishClipboardResult(midichopper::plugin::PadClipboardResultCode::failed);
        } else if (std::strcmp(key, kChopApplyRequestKey) == 0) {
            midichopper::plugin::ChopApplyRequest request;
            static_cast<void>(midichopper::plugin::decodeChopApplyRequest(value, request));
            const auto status = midichopper::plugin::encodeChopStatus(
                request.sequence, false, "Chop action failed; retry");
            publishUiState(kChopStatusKey, status.c_str());
        } else if (std::strcmp(key, kChopSnapshotRequestKey) == 0) {
            midichopper::plugin::ChopSnapshotRequest request;
            static_cast<void>(midichopper::plugin::decodeChopSnapshotRequest(value, request));
            const auto status = midichopper::plugin::encodeChopSnapshotError(
                request.sequence, "Could not read pads; retry");
            publishUiState(kChopSnapshotDataKey, status.c_str());
        } else if (std::strcmp(key, kPadStructureRequestKey) == 0) {
            midichopper::plugin::PadStructureRequest request;
            const bool decoded = midichopper::plugin::decodePadStructureRequest(value, request);
            const auto status = "PS1;E;" + std::to_string(decoded ? request.planId : 0U) +
                ";Pad action failed; retry";
            publishUiState(kPadStructureStatusKey, status.c_str());
        }
    }

    void publishUiState(const char* const key, const char* const value) override
    {
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (!updateStateValue(key, value))
            static_cast<void>(uiMessageBus_.publish(key, value));
#else
        struct RestorePublisher {
            const void* previous = publishingUiInstance;
            ~RestorePublisher() { publishingUiInstance = previous; }
        } restore;
        publishingUiInstance = this;
        static_cast<void>(updateStateValue(key, value));
#endif
    }

    template <class Callback>
    [[nodiscard]] bool withSamplerPaused(Callback&& callback) const
    {
        return samplerAccess_.withPaused(std::forward<Callback>(callback));
    }

    void clipboardAvailable(const bool available) noexcept override
    {
        controls_.publishParameterValue(midichopper::plugin::kParameterPadClipboardAvailable,
            available ? 1.0f : 0.0f);
    }

    void publishFileResult(const midichopper::plugin::PadFileResultCode result) noexcept override
    {
        const bool alternateHalf =
            (padFileResultSequence_.fetch_add(1U, std::memory_order_relaxed) & 1U) == 0U;
        controls_.publishParameterValue(midichopper::plugin::kParameterPadFileResultEvent,
            midichopper::plugin::padFileResultEventValue(
                result, alternateHalf));
    }

    void publishClipboardResult(
        const midichopper::plugin::PadClipboardResultCode result) noexcept override
    {
        const bool alternateHalf =
            (padClipboardResultSequence_.fetch_add(1U, std::memory_order_relaxed) & 1U) == 0U;
        controls_.publishParameterValue(midichopper::plugin::kParameterPadClipboardResultEvent,
            midichopper::plugin::padClipboardResultEventValue(
                result, alternateHalf));
    }

    void activateAllBanksMidiBank(const MidiEvent* const events,
                                  const std::uint32_t eventCount,
                                  const std::uint32_t frames) noexcept
    {
        using namespace midichopper::plugin;
        auto settings = sampler_.settings();
        if (settings.armed || sampler_.chopMidiPreviewActive() ||
            settings.midiBankMode != midichopper::MidiBankMode::AllBanks)
            return;

        std::uint32_t bank = settings.activeBank;
        HostMidiCursor cursor{events, eventCount, frames};
        midichopper::MidiEvent event;
        while (HostMidiCursor::next(&cursor, event)) {
            if (!event.isNoteOn())
                continue;
            const std::uint32_t pad = midichopper::padForMidiNote(
                event.note, settings.baseNote, settings.activeBank,
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
        controls_.publishParameterValue(kParameterCurrentCapturePad,
            currentCapturePad < midichopper::kPadCount ?
                static_cast<float>(currentCapturePad + 1U) : 0.0f);
        const std::uint32_t captureTargetPad = sampler_.captureTargetPad();
        controls_.publishParameterValue(kParameterCaptureTargetPad,
            captureTargetPad < midichopper::kPadCount ?
                static_cast<float>(captureTargetPad + 1U) : 0.0f);
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
            controls_.publishParameterValue(kFirstPadStatusParameter + localPad,
                visible && metadata.occupied ? 1.0f : 0.0f);
            controls_.publishParameterValue(kFirstPadActivityParameter + localPad,
                (metadata.recording || metadata.active) ? 1.0f : 0.0f);
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
        controls_.publishParameterValue(kParameterPlaybackPadEvent,
            playbackPadEventValue(trigger.pad, playbackPadEventAlternateHalf_));
    }

    void updateMeterOutputParameters() noexcept
    {
        using namespace midichopper::plugin;
        controls_.publishParameterValue(kParameterInputLevelLeft,
            inputMeter_.left());
        controls_.publishParameterValue(kParameterInputLevelRight,
            inputMeter_.right());
        controls_.publishParameterValue(kParameterOutputLevelLeft,
            outputMeter_.left());
        controls_.publishParameterValue(kParameterOutputLevelRight,
            outputMeter_.right());
    }

    void mirrorInputParameter(const std::uint32_t index, const float value) noexcept
    {
        const float previous = controls_.exchangeParameterValue(index, value);
        if (previous != value && canRequestParameterValueChanges())
            static_cast<void>(requestParameterValueChange(index, value));
    }

    const int initialLayout_;
    const int initialBankMode_;
    using GlobalSettingsArray = decltype(defaultGlobalValueSettings());
    const GlobalSettingsArray initialGlobalValueSettings_;
    midichopper::SamplerEngine sampler_;
    sms::dsp::StereoPeakMeter inputMeter_;
    sms::dsp::StereoPeakMeter outputMeter_;
    midichopper::plugin::PluginControls controls_;
    std::array<std::atomic<int>, midichopper::kPadCount> padColors_{};
    mutable std::mutex globalValueSettingsMutex_;
    GlobalSettingsArray desiredGlobalValueSettings_ = initialGlobalValueSettings_;
    GlobalSettingsArray activeGlobalValueSettings_ = initialGlobalValueSettings_; // Audio-owned.
    sms::audio::RealtimeLatestMailbox<GlobalSettingsArray> globalValueSettingsMailbox_;
    mutable sms::audio::RealtimeAccessGate samplerAccess_;
    std::uint64_t publishedPlaybackTriggerGeneration_ = 0;
    bool playbackPadEventAlternateHalf_ = false;
    std::atomic<std::uint32_t> padFileResultSequence_{0};
    std::atomic<std::uint32_t> padClipboardResultSequence_{0};
    mutable sms::audio::RealtimeCommandDispatcher controlDispatcher_;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
    midichopper::plugin::ControlWorker controlWorker_;
#endif
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
    midichopper::plugin::UiMessageBus uiMessageBus_;
#endif
    midichopper::plugin::PadWorkflows padWorkflows_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidichopperPlugin)
};

Plugin* createPlugin()
{
    return new MidichopperPlugin();
}

END_NAMESPACE_DISTRHO
