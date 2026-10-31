#pragma once

#include "Audio/RealtimeLatestMailbox.hpp"
#include "ChopEditorProtocol.hpp"
#include "GlobalValueMode.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace midichopper {
class SamplerEngine;
}

namespace midichopper::plugin {

// Owns host parameter values and control-to-audio command handoffs. Control
// writers queue requests; only the audio owner calls applyToEngine(). This
// component does not own the engine, workers, project state, or startup I/O.
class PluginControls {
public:
    using GlobalSettings = std::array<GlobalValueSettings, kGlobalValueControls.size()>;

    PluginControls(int initialLayout, int initialBankMode) noexcept;

    [[nodiscard]] float value(std::uint32_t index) const noexcept;
    void setParameterValue(std::uint32_t index, float value) noexcept;
    void publishParameterValue(std::uint32_t index, float value) noexcept;
    [[nodiscard]] float exchangeParameterValue(std::uint32_t index, float value) noexcept;

    // Control-side parsing and publication. Capture "reset" belongs to the
    // adapter's serialized PadWorkflows reset and must be handled before this.
    [[nodiscard]] bool queueStateCommand(const char* key, const char* value);

    // Bounded, non-throwing and free of allocation, locking and I/O. Preserve
    // settings -> MIDI preview -> triggers ordering before sample-offset MIDI.
    void applyToEngine(SamplerEngine& sampler, const GlobalSettings& globalSettings) noexcept;

private:
    [[nodiscard]] float clampedParameter(std::uint32_t index) const noexcept;
    [[nodiscard]] float effectiveGlobalParameter(
        std::uint32_t index, const GlobalSettings& globalSettings) const noexcept;
    void applySettings(SamplerEngine& sampler, const GlobalSettings& globalSettings) const noexcept;
    void applyChopMidiPreviewConfig(SamplerEngine& sampler) noexcept;
    void applyCommandTriggers(SamplerEngine& sampler) noexcept;

    std::array<std::atomic<float>, kParameterCount> parameters_{};
    std::atomic<std::uint32_t> pendingCommands_{0};
    std::atomic<std::uint32_t> pendingPlaybackRequest_{0};
    std::atomic<std::uint64_t> pendingClearPads_{0};
    std::atomic<std::uint32_t> pendingCaptureTarget_{0};
    sms::audio::RealtimeLatestMailbox<ChopPreviewRequest> chopPreviewMailbox_;
    std::mutex chopMidiPreviewMutex_; // Control writers only.
    std::atomic<std::uint64_t> pendingChopMidiPreviewSequence_{0};
    std::atomic<std::uint32_t> pendingChopMidiPreviewConfig_{0};
    std::array<std::atomic<std::uint64_t>, kChopMidiPreviewPadCount>
        pendingChopMidiPreviewFrames_{};
    std::array<std::atomic<std::uint64_t>, kChopMidiPreviewPadCount>
        pendingChopMidiPreviewEndFrames_{};
    std::uint64_t appliedChopMidiPreviewSequence_ = 0; // Audio-owned.
};

} // namespace midichopper::plugin
