#pragma once

#include "../plugin/EditorSnapshotProtocol.hpp"
#include "../plugin/WaveformDetailProtocol.hpp"

#include <atomic>
#include <chrono>

namespace midichopper::ui {

[[nodiscard]] inline std::uint64_t nextEditorSnapshotSequence() noexcept
{
    static std::atomic<std::uint64_t> next{
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    auto sequence = next.fetch_add(1U, std::memory_order_relaxed);
    if (sequence == 0U)
        sequence = next.fetch_add(1U, std::memory_order_relaxed);
    return sequence;
}

/** One reply supplies the selected waveform and both control groups. */
class EditorSnapshotSession {
public:
    void begin(const int pad) noexcept
    {
        fRequest = {nextEditorSnapshotSequence(), static_cast<std::uint32_t>(pad)};
        fPending = pad >= 0;
        fReady = false;
    }

    [[nodiscard]] bool pending() const noexcept { return fPending; }
    [[nodiscard]] int pad() const noexcept { return static_cast<int>(fRequest.pad); }
    [[nodiscard]] plugin::EditorSnapshotRequest request() const noexcept { return fRequest; }
    [[nodiscard]] bool readyFor(const int pad) const noexcept
    { return fPending && fReady && pad == static_cast<int>(fRequest.pad); }

    [[nodiscard]] bool accept(const plugin::EditorSnapshotReply& reply) noexcept
    {
        if (!fPending || fReady || reply.request.sequence != fRequest.sequence ||
            reply.request.pad != fRequest.pad || reply.waveform.pad != fRequest.pad)
            return false;
        fReply = reply;
        fReady = true;
        return true;
    }

    void complete() noexcept { fPending = false; fReady = false; }
    [[nodiscard]] const auto& waveform() const noexcept { return fReply.waveform; }
    [[nodiscard]] const auto& playback() const noexcept { return fReply.playback; }
    [[nodiscard]] const auto& mixer() const noexcept { return fReply.mixer; }

private:
    plugin::EditorSnapshotRequest fRequest{};
    plugin::EditorSnapshotReply fReply{};
    bool fPending = false;
    bool fReady = false;
};

/** A detail retry keeps its identity; only a new source/range advances it. */
[[nodiscard]] inline plugin::WaveformDetailRequest waveformDetailRequest(
    const plugin::WaveformDetailRequest& previous, std::uint64_t& sequence,
    const std::uint32_t firstPad, const std::uint32_t padCount,
    const std::uint64_t start, const std::uint64_t end) noexcept
{
    if (previous.sequence == sequence && sequence != 0U &&
        previous.firstPad == firstPad && previous.padCount == padCount &&
        previous.start == start && previous.end == end)
        return previous;
    sequence = nextEditorSnapshotSequence();
    return {sequence, firstPad, padCount, start, end};
}

} // namespace midichopper::ui
