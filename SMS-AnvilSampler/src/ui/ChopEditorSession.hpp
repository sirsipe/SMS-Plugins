#pragma once

#include "ChopEditor.hpp"
#include "../plugin/ChopEditorProtocol.hpp"

#include <chrono>
#include <atomic>

namespace midichopper::ui {

/** Collects a coherent ordinary-editor baseline and rejects superseded replies.
 * Retries preserve the request identity; navigation and Apply refresh replace it.
 * ChopEditorController owns the workflow and split-plan lifecycle; its host
 * supplies transport and presentation effects.
 */
class ChopEditorSession {
public:
    using Clock = std::chrono::steady_clock;
    std::array<sms::audio::WaveformSummary, chop::kPadCount> waveforms{};
    std::array<std::int64_t, chop::kBoundaryCount> offsets{};

    void begin(const std::uint32_t firstPad) noexcept
    {
        waveforms.fill({});
        offsets.fill(0);
        fReady = false;
        fPending = true;
        fApplying = false;
        // Shared across UI instances: a reopened editor must not accept a
        // queued reply from an earlier instance with the same pad selection.
        static std::atomic<std::uint64_t> nextSequence{
            static_cast<std::uint64_t>(Clock::now().time_since_epoch().count())};
        auto sequence = nextSequence.fetch_add(1U, std::memory_order_relaxed);
        if (sequence == 0U)
            sequence = nextSequence.fetch_add(1U, std::memory_order_relaxed);
        fRequest = {sequence, firstPad, chop::kPadCount};
        fRetryDeadline = {};
    }

    void cancel() noexcept
    {
        fPending = false;
        fReady = false;
        fApplying = false;
    }

    [[nodiscard]] bool requestDue(const Clock::time_point now) const noexcept
    {
        return fPending && now >= fRetryDeadline;
    }

    [[nodiscard]] plugin::ChopSnapshotRequest request(const Clock::time_point now) noexcept
    {
        fRetryDeadline = now + std::chrono::milliseconds(250);
        return fRequest;
    }

    [[nodiscard]] bool accept(const plugin::ChopSnapshotReply& reply) noexcept
    {
        if (!fPending || !matches(reply.request))
            return false;
        for (std::size_t index = 0; index < waveforms.size(); ++index) {
            if (reply.waveforms[index].pad != fRequest.firstPad + index)
                return false;
        }
        waveforms = reply.waveforms;
        fGenerations = reply.generations;
        fReady = true;
        fPending = false;
        return true;
    }

    [[nodiscard]] bool acceptsError(const std::uint64_t sequence) const noexcept
    {
        return fPending && sequence == fRequest.sequence;
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return fReady && chop::ready(waveforms);
    }

    [[nodiscard]] plugin::ChopApplyRequest applyRequest() noexcept
    {
        plugin::ChopApplyRequest result;
        result.firstPad = fRequest.firstPad;
        result.padCount = chop::kPadCount;
        result.sequence = fRequest.sequence;
        result.expectedGenerations = fGenerations;
        result.revisionChecked = true;
        std::copy(offsets.begin(), offsets.end(), result.boundaryOffsets.begin());
        fApplying = true;
        return result;
    }

    [[nodiscard]] bool acceptStatus(const plugin::ChopApplyStatus& status) noexcept
    {
        if (!fApplying || status.sequence != fRequest.sequence)
            return false;
        fApplying = false;
        return true;
    }

private:
    [[nodiscard]] bool matches(const plugin::ChopSnapshotRequest& request) const noexcept
    {
        return request.sequence == fRequest.sequence &&
            request.firstPad == fRequest.firstPad && request.padCount == fRequest.padCount;
    }

    plugin::ChopSnapshotRequest fRequest{};
    std::array<std::uint64_t, kPadsPerBank> fGenerations{};
    Clock::time_point fRetryDeadline{};
    bool fPending = false;
    bool fReady = false;
    bool fApplying = false;
};

} // namespace midichopper::ui
