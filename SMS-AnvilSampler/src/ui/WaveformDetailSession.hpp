#pragma once

#include "EditorSnapshotSession.hpp"
#include "WaveformViewport.hpp"

#include <chrono>
#include <optional>

namespace midichopper::ui {

/** Owns viewport detail identity, debounce, retries, and stale-reply rejection. */
class WaveformDetailSession {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    [[nodiscard]] const sms::ui::waveform::Viewport& viewport() const noexcept
    { return fViewport; }
    [[nodiscard]] const sms::audio::WaveformSummary* detail() const noexcept
    { return fReady ? &fDetail : nullptr; }

    [[nodiscard]] bool zoom(const float delta, const float anchorX,
                            const sms::ui::Rect bounds, const Time now) noexcept
    { return scheduleIfChanged(fViewport.zoom(delta, anchorX, bounds), now); }
    [[nodiscard]] bool pan(const float delta, const Time now) noexcept
    { return scheduleIfChanged(fViewport.pan(delta), now); }
    [[nodiscard]] bool setZoomPosition(const float position, const Time now) noexcept
    { return scheduleIfChanged(fViewport.setZoomPosition(position), now); }
    [[nodiscard]] bool setScrollPosition(const float position, const Time now) noexcept
    { return scheduleIfChanged(fViewport.setScrollPosition(position), now); }

    void reset(const std::uint64_t frames = 0U) noexcept
    {
        ++fSequence;
        fViewport.reset(frames);
        fReady = fDirty = false;
    }

    [[nodiscard]] bool requestDue(const Time now) const noexcept
    { return fDirty && now >= fDeadline; }

    [[nodiscard]] std::optional<plugin::WaveformDetailRequest> request(
        const int firstPad, const std::uint32_t padCount, const bool editorVisible,
        const Time now) noexcept
    {
        if (!fViewport.zoomed() || !editorVisible) {
            fDirty = false;
            return std::nullopt;
        }
        if (firstPad < 0)
            return std::nullopt;
        fRequest = waveformDetailRequest(fRequest, fSequence,
            static_cast<std::uint32_t>(firstPad), padCount, fViewport.start, fViewport.end);
        fDeadline = now + std::chrono::milliseconds(250);
        return fRequest;
    }

    [[nodiscard]] bool accept(const plugin::WaveformDetailReply& reply) noexcept
    {
        if (reply.request.sequence != fRequest.sequence ||
            reply.request.sequence != fSequence ||
            reply.request.firstPad != fRequest.firstPad ||
            reply.request.padCount != fRequest.padCount ||
            reply.request.start != fViewport.start || reply.request.end != fViewport.end ||
            !fViewport.zoomed())
            return false;
        fDetail = reply.waveform;
        fReady = true;
        fDirty = false;
        return true;
    }

private:
    [[nodiscard]] bool scheduleIfChanged(const bool changed, const Time now) noexcept
    {
        if (changed) {
            fReady = false;
            fDirty = fViewport.zoomed();
            fDeadline = now + std::chrono::milliseconds(50);
        }
        return changed;
    }

    sms::ui::waveform::Viewport fViewport{};
    sms::audio::WaveformSummary fDetail{};
    plugin::WaveformDetailRequest fRequest{};
    std::uint64_t fSequence = 0U;
    bool fReady = false;
    bool fDirty = false;
    Time fDeadline{};
};

} // namespace midichopper::ui
