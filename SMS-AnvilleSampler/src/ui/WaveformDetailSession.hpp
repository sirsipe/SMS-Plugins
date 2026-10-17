#pragma once

#include "EditorSnapshotSession.hpp"
#include "SampleWaveform.hpp"
#include "WaveformViewport.hpp"

#include <chrono>
#include <optional>

namespace midichopper::ui {

/** Owns retained viewport detail, debounce, retries, and stale-reply rejection. */
class WaveformDetailSession {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    [[nodiscard]] const sms::ui::waveform::Viewport& viewport() const noexcept
    { return fViewport; }
    [[nodiscard]] const sms::ui::waveform::CachedDetail* detail() const noexcept
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

    void setReversed(const bool reverse, const Time now) noexcept
    {
        if (fReverse == reverse)
            return;
        fReverse = reverse;
        ++fSequence; // Replies for the previous orientation cannot finish this request.
        static_cast<void>(scheduleIfChanged(true, now));
    }

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
        const auto source = sampleSourceViewport(fViewport, fReverse);
        fRequest = waveformDetailRequest(fRequest, fSequence,
            static_cast<std::uint32_t>(firstPad), padCount, source.start, source.end);
        fDeadline = now + std::chrono::milliseconds(250);
        return fRequest;
    }

    [[nodiscard]] bool accept(const plugin::WaveformDetailReply& reply) noexcept
    {
        const auto source = sampleSourceViewport(fViewport, fReverse);
        if (reply.request.sequence != fRequest.sequence ||
            reply.request.sequence != fSequence ||
            reply.request.firstPad != fRequest.firstPad ||
            reply.request.padCount != fRequest.padCount ||
            reply.request.start != source.start || reply.request.end != source.end ||
            !fViewport.zoomed())
            return false;
        fDetail = {reply.waveform, reply.request.start, reply.request.end};
        fReady = true;
        fDirty = false;
        return true;
    }

private:
    [[nodiscard]] bool scheduleIfChanged(const bool changed, const Time now) noexcept
    {
        if (changed) {
            fDirty = fViewport.zoomed();
            fDeadline = now + std::chrono::milliseconds(50);
        }
        return changed;
    }

    sms::ui::waveform::Viewport fViewport{};
    sms::ui::waveform::CachedDetail fDetail{};
    plugin::WaveformDetailRequest fRequest{};
    std::uint64_t fSequence = 0U;
    bool fReady = false;
    bool fDirty = false;
    bool fReverse = false;
    Time fDeadline{};
};

} // namespace midichopper::ui
