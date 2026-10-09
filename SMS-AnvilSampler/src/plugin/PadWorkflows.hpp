#pragma once

#include "Audio/RealtimeCommandDispatcher.hpp"
#include "PadClipboard.hpp"
#include "PadClipboardProtocol.hpp"
#include "PadFileActionProtocol.hpp"
#include "SamplerEngine.hpp"
#include "StateKeys.hpp"

#include <mutex>
#include <string>
#include <string_view>

namespace midichopper::plugin {

/** Format adapter supplies transport and output-parameter notifications. */
class PadWorkflowObserver {
public:
    virtual ~PadWorkflowObserver() = default;
    virtual void publishUiState(const char* key, const char* value) = 0;
    virtual void publishFileResult(PadFileResultCode result) noexcept = 0;
    virtual void publishClipboardResult(PadClipboardResultCode result) noexcept = 0;
    virtual void clipboardAvailable() noexcept = 0;
};

/** Control-thread pad workflows. Owns clipboard and split plans; serializes
 * workflows with durable state reads/writes and sample-rate reconfiguration.
 * Audio processing must never call this controller or its mutation mutex.
 * Engine/dispatcher references and observer outlive this instance.
 */
class PadWorkflows {
public:
    PadWorkflows(SamplerEngine& sampler, sms::audio::RealtimeCommandDispatcher& dispatcher,
                 PadWorkflowObserver& observer) noexcept
        : sampler_(sampler), controlDispatcher_(dispatcher), observer_(observer) {}

    [[nodiscard]] std::mutex& mutationMutex() const noexcept { return padMutationMutex_; }
    void handlePadStructureRequest(std::string_view encoded);
    void handleChopApplyRequest(std::string_view encoded);
    void handlePadClipboardRequest(std::string_view encoded);
    void handlePadFileRequest(std::string_view encoded);
    [[nodiscard]] bool makeWaveformState(std::uint32_t pad, std::string& waveform,
        sms::dsp::SamplePlaybackSettings* playback = nullptr,
        sms::dsp::SampleMixerSettings* mixer = nullptr,
        sms::audio::WaveformSummary* capturedSummary = nullptr) const;

private:
    struct PendingSplitPlan {
        bool active = false;
        std::uint64_t id = 0U;
        std::uint32_t visibleFirst = 0U;
        std::uint32_t visibleCount = 0U;
        std::uint32_t firstPad = 0U;
        std::uint32_t emptyPad = 0U;
        std::uint32_t generationCount = 0U;
        std::array<std::uint64_t, midichopper::kPadsPerBank> generations{};
    };

    void publishPadSettings(std::uint32_t firstPad, std::uint32_t padCount);
    void publishPadStructureError(std::uint64_t planId, const char* message);
    void publishFileStatus(const std::string& status)
    { observer_.publishUiState(kPadFileStatusKey, status.c_str()); }
    void handleChopApplyRequestImpl(std::string_view encoded);
    void handlePadClipboardRequestImpl(std::string_view encoded);
    void handlePadFileRequestImpl(std::string_view encoded);

    SamplerEngine& sampler_;
    sms::audio::RealtimeCommandDispatcher& controlDispatcher_;
    PadWorkflowObserver& observer_;
    mutable std::mutex padMutationMutex_;
    PadClipboard padClipboard_;
    PendingSplitPlan pendingSplitPlan_{};
    std::uint64_t nextSplitPlanId_ = 1U;
};

} // namespace midichopper::plugin
