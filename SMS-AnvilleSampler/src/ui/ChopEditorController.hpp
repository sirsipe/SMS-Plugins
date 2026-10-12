#pragma once

#include "ChopEditorSession.hpp"
#include "WaveformViewport.hpp"
#include "../plugin/PadStructureProtocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace midichopper::ui {

/** Effects supplied by the DPF adapter; the workflow never depends on DPF. */
class ChopEditorHost {
public:
    virtual ~ChopEditorHost() = default;
    virtual void sendChopState(const char* key, const std::string& value) = 0;
    virtual void selectChopPad(int pad) = 0;
    virtual void leaveChopEditor() = 0;
    virtual void resetChopViewport(std::uint64_t frames) = 0;
    virtual void setChopStatus(const char* message) = 0;
    virtual void setChopStructureBusy(bool busy) = 0;
    virtual void repaintChopEditor() = 0;
};

/** Owns cut/split editor lifecycle, preview commands, edits, and stale results. */
class ChopEditorController {
public:
    using Clock = std::chrono::steady_clock;
    explicit ChopEditorController(ChopEditorHost& host) noexcept : fHost(host) {}

    [[nodiscard]] bool active() const noexcept { return fActive; }
    [[nodiscard]] bool split() const noexcept { return fSplit; }
    [[nodiscard]] bool applying() const noexcept { return fApplying; }
    [[nodiscard]] int firstPad() const noexcept { return fFirstPad; }
    [[nodiscard]] std::uint64_t splitPlanId() const noexcept { return fPlanId; }
    [[nodiscard]] int activeBoundary() const noexcept { return fActiveBoundary; }
    [[nodiscard]] float previewPosition() const noexcept { return fPreviewPosition; }
    [[nodiscard]] int previewPad() const noexcept { return fPreviewPad; }
    [[nodiscard]] const auto& waveforms() const noexcept { return fSession.waveforms; }
    [[nodiscard]] const auto& offsets() const noexcept { return fSession.offsets; }
    [[nodiscard]] bool ready() const noexcept
    { return fSplit ? chop::ready(fSession.waveforms) : fSession.ready(); }
    [[nodiscard]] bool dirty() const noexcept
    {
        return fSplit ? ready() : std::any_of(fSession.offsets.begin(), fSession.offsets.end(),
            [](const std::int64_t offset) { return offset != 0; });
    }
    [[nodiscard]] bool canNavigate(const int direction, const int bankFirst,
                                   const int visibleCount) const noexcept
    {
        return fActive && direction != 0 &&
            chop::navigationTarget(fTargetPad - bankFirst, direction, visibleCount) >= 0;
    }

    void open(const int targetPad, const int bankFirst, const int visibleCount)
    {
        const int localPad = targetPad - bankFirst;
        if (targetPad < 0 || localPad <= 0 || localPad + 1 >= visibleCount)
            return;
        stopPreview();
        fActive = true;
        fSplit = false;
        fPlanId = 0U;
        fTargetPad = targetPad;
        fFirstPad = targetPad - 1;
        fHost.selectChopPad(targetPad);
        resetBaseline();
        fApplying = false;
        fHost.setChopStatus("");
        muteMidi();
        fHost.repaintChopEditor();
    }

    void openSplit(const plugin::SplitPlanReady& plan)
    {
        if (plan.targetPad >= kPadCount || plan.emptyPad <= plan.targetPad ||
            plan.emptyPad >= kPadCount || plan.waveform.pad != plan.targetPad ||
            plan.waveform.frames < 2U)
            return;
        stopPreview();
        fActive = fSplit = true;
        fTargetPad = fFirstPad = static_cast<int>(plan.targetPad);
        fPlanId = plan.planId;
        fHost.selectChopPad(fTargetPad);
        resetBaseline();
        fSession.waveforms[0] = plan.waveform;
        fSession.waveforms[1] = {plan.targetPad + 1U, 0U, plan.waveform.sampleRate, {}, {}};
        fSession.waveforms[2] = fSession.waveforms[1];
        fHost.resetChopViewport(plan.waveform.frames);
        fSession.offsets[0] = static_cast<std::int64_t>(plan.waveform.frames / 2U) -
                              static_cast<std::int64_t>(plan.waveform.frames);
        fApplying = false;
        fHost.setChopStatus("");
        updateMidi();
        fHost.repaintChopEditor();
    }

    void navigate(const int direction, const int bankFirst, const int visibleCount)
    {
        if (fApplying || !canNavigate(direction, bankFirst, visibleCount))
            return;
        const int next = chop::navigationTarget(fTargetPad - bankFirst, direction, visibleCount);
        if (fSplit)
            cancelPlan(fPlanId);
        open(bankFirst + next, bankFirst, visibleCount);
    }

    void cancelPlan(const std::uint64_t planId)
    {
        if (planId != 0U) {
            plugin::PadStructureRequest request;
            request.action = plugin::PadStructureAction::cancelSplit;
            request.planId = planId;
            fHost.sendChopState("pad_structure_request", plugin::encodePadStructureRequest(request));
        }
    }

    void cancel()
    {
        if (fSplit)
            cancelPlan(fPlanId);
        stopPreview();
        disableMidi();
        fActive = fSplit = false;
        fPlanId = 0U;
        fFirstPad = -1;
        fSession.cancel();
        fActiveBoundary = -1;
        fApplying = false;
        fHost.setChopStatus("");
        fHost.leaveChopEditor();
        fHost.repaintChopEditor();
    }

    void idle(const Clock::time_point now)
    {
        if (fActive && !fSplit && fSession.requestDue(now))
            fHost.sendChopState("chop_snapshot_request",
                                plugin::encodeChopSnapshotRequest(fSession.request(now)));
    }

    /** Returns true when a matching baseline/status was handled. */
    [[nodiscard]] bool acceptState(const std::string_view key, const char* value)
    {
        if (!fActive || fSplit)
            return false;
        if (key == "chop_snapshot_data") {
            plugin::ChopSnapshotReply reply;
            plugin::ChopApplyStatus error;
            if (plugin::decodeChopSnapshotReply(value, reply) && fSession.accept(reply)) {
                if (ready()) {
                    fHost.resetChopViewport(chop::totalFrames(fSession.waveforms));
                    fHost.setChopStatus("");
                    updateMidi();
                } else {
                    muteMidi();
                    fHost.setChopStatus("Non-empty samples must use one sample rate");
                }
                return true;
            }
            if (plugin::decodeChopSnapshotError(value, error) && fSession.acceptsError(error.sequence)) {
                fHost.setChopStatus(error.message.data());
                return true;
            }
        } else if (key == "chop_status") {
            plugin::ChopApplyStatus status;
            if (plugin::decodeChopApplyStatus(value, status) && fSession.acceptStatus(status)) {
                fApplying = false;
                stopPreview();
                resetBaseline();
                muteMidi();
                fHost.setChopStatus(status.success ? "Cut points applied" : status.message.data());
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool acceptSplitStatus(const char* value, const int bankFirst,
                                         const int visibleCount)
    {
        if (fPlanId != 0U && "PS1;O;" + std::to_string(fPlanId) + ";S" == value) {
            fHost.setChopStructureBusy(false);
            const int target = chop::postSplitEditorTarget(fTargetPad - bankFirst, visibleCount);
            if (target >= 0)
                open(bankFirst + target, bankFirst, visibleCount);
            else
                cancel();
            fHost.setChopStatus("Sample split applied");
            return true;
        }
        if (fSplit && std::string_view(value).starts_with("PS1;E;")) {
            char* end = nullptr;
            const auto planId = std::strtoull(value + 6U, &end, 10);
            if (end == value + 6U || end == nullptr || *end != ';' || planId != fPlanId)
                return false;
            fHost.setChopStructureBusy(false);
            fPlanId = 0U;
            fSplit = false;
            cancel();
            fHost.setChopStatus(end + 1U);
            return true;
        }
        return false;
    }

    void preview(const int padInEditor)
    {
        if (!ready() || padInEditor < 0 || padInEditor >= 3) {
            fHost.setChopStatus("Non-empty samples must use one sample rate");
            fHost.repaintChopEditor();
            return;
        }
        const auto start = chop::adjustedStartFrame(fSession.waveforms, fSession.offsets, padInEditor);
        const auto frames = chop::adjustedFrames(fSession.waveforms, fSession.offsets, padInEditor);
        if (frames == 0U)
            return;
        fHost.sendChopState("chop_preview_request", plugin::encodeChopPreviewRequest(
            {true, static_cast<std::uint32_t>(fFirstPad),
             fSplit ? 1U : 3U, start, start + frames}));
        fPlaying = true;
        fPreviewPad = padInEditor;
        fHost.repaintChopEditor();
    }

    void stopPreview()
    {
        fHost.sendChopState("chop_preview_request", plugin::encodeChopPreviewRequest(
            {false, static_cast<std::uint32_t>(std::max(fFirstPad, 0)), fSplit ? 1U : 3U, 0U, 0U}));
        fPlaying = false;
        fPreviewPad = -1;
        fHost.repaintChopEditor();
    }

    void acceptPreviewPosition(const float value) noexcept
    {
        if (std::isfinite(value) && value > 0.0f) {
            fPreviewPosition = value;
            fPlaying = true;
            fPreviewPad = -1;
            if (fActive && ready()) {
                const int originalPad = static_cast<int>(std::floor(value)) - 1 - fFirstPad;
                const double frame = chop::sourceFrameForPlayhead(
                    fSession.waveforms, originalPad, value - std::floor(value));
                for (int pad = 0; frame >= 0.0 && pad < (fSplit ? 2 : 3); ++pad) {
                    const auto start = chop::adjustedStartFrame(fSession.waveforms, fSession.offsets, pad);
                    const auto frames = chop::adjustedFrames(fSession.waveforms, fSession.offsets, pad);
                    if (frames != 0U && frame >= static_cast<double>(start) &&
                        frame < static_cast<double>(start + frames)) {
                        fPreviewPad = pad;
                        break;
                    }
                }
            }
        } else {
            if (fPlaying)
                fPreviewPosition = 0.0f;
            fPlaying = false;
            fPreviewPad = -1;
        }
    }

    void disableMidi() { sendMidi({}); }

    void apply()
    {
        if (!ready()) {
            fHost.setChopStatus("Non-empty samples must use one sample rate");
            fHost.repaintChopEditor();
            return;
        }
        if (fSplit) {
            const auto frame = chop::adjustedBoundary(fSession.waveforms, fSession.offsets, 0);
            if (fPlanId == 0U || frame <= 0 ||
                frame >= static_cast<std::int64_t>(fSession.waveforms[0].frames)) {
                fHost.setChopStatus("Both split halves must contain audio");
                fHost.repaintChopEditor();
                return;
            }
            plugin::PadStructureRequest request;
            request.action = plugin::PadStructureAction::applySplit;
            request.planId = fPlanId;
            request.splitFrame = static_cast<std::uint32_t>(frame);
            stopPreview();
            muteMidi();
            fApplying = true;
            fHost.setChopStructureBusy(true);
            fHost.setChopStatus("Applying sample split...");
            fHost.repaintChopEditor();
            fHost.sendChopState("pad_structure_request", plugin::encodePadStructureRequest(request));
        } else {
            const auto request = fSession.applyRequest();
            stopPreview();
            muteMidi();
            fApplying = true;
            fHost.setChopStatus("Applying chop boundaries...");
            fHost.repaintChopEditor();
            fHost.sendChopState("chop_apply_request", plugin::encodeChopApplyRequest(request));
        }
    }

    void beginBoundaryDrag(const int boundary, const float x) noexcept
    {
        fActiveBoundary = boundary;
        fDragStartX = x;
        fDragStartOffset = fSession.offsets[static_cast<std::size_t>(boundary)];
    }
    void endBoundaryDrag() noexcept { fActiveBoundary = -1; }
    void dragBoundary(const float x, const float width, const sms::ui::waveform::Viewport& viewport)
    {
        const auto total = chop::totalFrames(fSession.waveforms);
        const auto delta = static_cast<std::int64_t>(std::llround((x - fDragStartX) *
            static_cast<double>(viewport.zoomed() ? viewport.end - viewport.start : total) / width));
        editBoundary(fActiveBoundary, fDragStartOffset + delta);
    }
    void wheelBoundary(const int boundary, const float delta, const sms::ui::Rect bounds,
                       const sms::ui::waveform::Viewport& viewport)
    {
        editBoundary(boundary, chop::wheelAdjustedBoundaryOffset(
            fSession.waveforms, fSession.offsets, boundary, delta, bounds, viewport));
    }

private:
    void resetBaseline()
    {
        fHost.resetChopViewport(0U);
        fSession.waveforms.fill({});
        fSession.offsets.fill(0);
        if (!fSplit && fFirstPad >= 0)
            fSession.begin(static_cast<std::uint32_t>(fFirstPad));
        else
            fSession.cancel();
        fActiveBoundary = -1;
        fPreviewPosition = 0.0f;
        fPreviewPad = -1;
    }
    void editBoundary(const int boundary, const std::int64_t requested)
    {
        auto clamped = chop::clampBoundaryOffset(fSession.waveforms, fSession.offsets, boundary, requested);
        if (fSplit && boundary == 0 && fSession.waveforms[0].frames >= 2U) {
            const auto original = static_cast<std::int64_t>(fSession.waveforms[0].frames);
            clamped = std::clamp(clamped, 1 - original, std::int64_t{-1});
        }
        fSession.offsets[static_cast<std::size_t>(boundary)] = clamped;
        updateMidi();
        fHost.setChopStatus("");
        fHost.repaintChopEditor();
    }
    void sendMidi(const plugin::ChopMidiPreviewRequest& request)
    { fHost.sendChopState("chop_midi_preview", plugin::encodeChopMidiPreviewRequest(request)); }
    void muteMidi()
    {
        if (fFirstPad < 0)
            return;
        plugin::ChopMidiPreviewRequest request;
        request.active = true;
        request.firstPad = static_cast<std::uint32_t>(fFirstPad);
        request.sourcePadCount = fSplit ? 1U : 3U;
        sendMidi(request);
    }
    void updateMidi()
    {
        if (!fActive || fApplying || fFirstPad < 0 || !ready()) {
            if (fActive && fFirstPad >= 0)
                muteMidi();
            else
                disableMidi();
            return;
        }
        plugin::ChopMidiPreviewRequest request;
        request.active = true;
        request.firstPad = static_cast<std::uint32_t>(fFirstPad);
        request.sourcePadCount = fSplit ? 1U : 3U;
        request.previewPadCount = fSplit ? 2U : 3U;
        for (std::uint32_t pad = 0; pad < request.previewPadCount; ++pad) {
            request.sourceFrames[pad] = chop::adjustedStartFrame(
                fSession.waveforms, fSession.offsets, static_cast<int>(pad));
            request.sourceEndFrames[pad] = request.sourceFrames[pad] +
                chop::adjustedFrames(fSession.waveforms, fSession.offsets, static_cast<int>(pad));
        }
        sendMidi(request);
    }

    ChopEditorHost& fHost;
    ChopEditorSession fSession;
    bool fActive = false;
    bool fSplit = false;
    std::uint64_t fPlanId = 0U;
    int fTargetPad = -1;
    int fFirstPad = -1;
    int fActiveBoundary = -1;
    float fDragStartX = 0.0f;
    std::int64_t fDragStartOffset = 0;
    bool fPlaying = false;
    int fPreviewPad = -1;
    bool fApplying = false;
    float fPreviewPosition = 0.0f;
};

} // namespace midichopper::ui
