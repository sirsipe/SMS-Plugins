#include "ViewRenderers.hpp"
#include "ProductControlsRenderer.hpp"
#include "PadViewMapping.hpp"
#include "ChopEditor.hpp"
#include "DPF/AnalogMaterials.hpp"
#include "DPF/Controls.hpp"
#include "DPF/WaveformRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace midichopper::ui::rendering {

namespace uiLayout = layout;

namespace {

void drawChopArrow(const ViewRenderContext& ctx, const sms::ui::Rect bounds, const bool pointsRight,
                   const bool isHovered, const bool enabled);
void drawChopExit(const ViewRenderContext& ctx);

void drawChopEditor(const ViewRenderContext& ctx)
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    const auto& colors = sms::ui::dpf::theme();
    sms::ui::dpf::drawPanel(ctx.canvas, uiLayout::mainPanel);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(13.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    char heading[48];
    std::snprintf(heading, sizeof(heading), sms::ui::dpf::themeFormat("%s  /  PAD %02d"),
                  ctx.state.chopSplitMode ? sms::ui::dpf::themeText("SPLIT SAMPLE")
                                          : sms::ui::dpf::themeText("ADJUST CUT POINTS"),
                  localPadForGlobalPad(ctx, ctx.state.chopTargetPad) + 1);
    text(ctx, 46.0f, 120.0f, heading, nullptr);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    const auto chopPad = [&](const int offset) {
        return static_cast<int>(midichopper::advanceVisiblePad(
            static_cast<std::uint32_t>(ctx.state.chopFirstPad), static_cast<std::uint32_t>(offset),
            static_cast<std::uint8_t>(pads.visiblePadCount()), midiBankMode(ctx)));
    };
    const auto bankLabel = [&](const int pad) {
        return 'A' + static_cast<int>(midichopper::bankForPad(
                         static_cast<std::uint32_t>(pad),
                         static_cast<std::uint8_t>(pads.visiblePadCount()), midiBankMode(ctx)));
    };
    char neighbors[96];
    if (ctx.state.chopSplitMode) {
        std::snprintf(neighbors, sizeof(neighbors), "PADS %c%02d + %c%02d  /  DRAG SPLIT LINE",
                      bankLabel(chopPad(0)), localPadForGlobalPad(ctx, chopPad(0)) + 1,
                      bankLabel(chopPad(1)), localPadForGlobalPad(ctx, chopPad(1)) + 1);
    } else {
        std::snprintf(neighbors, sizeof(neighbors),
                      "PADS %c%02d + %c%02d + %c%02d  /  DRAG CUT LINES", bankLabel(chopPad(0)),
                      localPadForGlobalPad(ctx, chopPad(0)) + 1, bankLabel(chopPad(1)),
                      localPadForGlobalPad(ctx, chopPad(1)) + 1, bankLabel(chopPad(2)),
                      localPadForGlobalPad(ctx, chopPad(2)) + 1);
    }
    text(ctx, 936.0f, 122.0f, neighbors, nullptr);

    const auto combined = chop::combinedWaveform(ctx.state.chopWaveforms);
    const auto visible = sms::ui::waveform::visibleSummary(combined, ctx.state.waveformViewport,
                                                           ctx.state.waveformDetail);
    sms::ui::dpf::drawWaveform(ctx.canvas, uiLayout::chopWaveform, visible, ctx.state.chopReady,
                               colors.activityPlayback);
    sms::ui::dpf::drawWaveformViewportLabel(ctx.canvas, uiLayout::chopWaveform,
                                            ctx.state.waveformViewport, combined.sampleRate);
    drawViewportControls(ctx, uiLayout::chopZoom, uiLayout::chopScroll);
    if (ctx.state.chopReady) {
        const float selectedStart =
            ctx.state.chopSplitMode
                ? uiLayout::chopWaveform.x
                : chop::boundaryX(uiLayout::chopWaveform, ctx.state.chopWaveforms,
                                  ctx.state.chopOffsets, 0, ctx.state.waveformViewport);
        const float selectedEnd =
            chop::boundaryX(uiLayout::chopWaveform, ctx.state.chopWaveforms, ctx.state.chopOffsets,
                            ctx.state.chopSplitMode ? 0 : 1, ctx.state.waveformViewport);
        const float clippedStart =
            std::clamp(selectedStart, uiLayout::chopWaveform.x,
                       uiLayout::chopWaveform.x + uiLayout::chopWaveform.width);
        const float clippedEnd =
            std::clamp(selectedEnd, uiLayout::chopWaveform.x,
                       uiLayout::chopWaveform.x + uiLayout::chopWaveform.width);
        ctx.canvas.beginPath();
        ctx.canvas.rect(clippedStart, uiLayout::chopWaveform.y,
                        std::max(0.0f, clippedEnd - clippedStart), uiLayout::chopWaveform.height);
        ctx.canvas.fillColor(colors.selection.withAlpha(0.08f));
        ctx.canvas.fill();
        if (ctx.state.chopSplitMode) {
            if (selectedEnd >= uiLayout::chopWaveform.x &&
                selectedEnd <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                sms::ui::dpf::drawCutHandle(ctx.canvas, selectedEnd, uiLayout::chopWaveform.y,
                                            uiLayout::chopWaveform.height, "SPLIT",
                                            hovered(ctx, InteractiveType::chopBoundary, 0));
            }
        } else {
            if (selectedStart >= uiLayout::chopWaveform.x &&
                selectedStart <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                sms::ui::dpf::drawCutHandle(ctx.canvas, selectedStart, uiLayout::chopWaveform.y,
                                            uiLayout::chopWaveform.height, "CUT 1",
                                            hovered(ctx, InteractiveType::chopBoundary, 0));
            }
            if (selectedEnd >= uiLayout::chopWaveform.x &&
                selectedEnd <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                sms::ui::dpf::drawCutHandle(ctx.canvas, selectedEnd, uiLayout::chopWaveform.y,
                                            uiLayout::chopWaveform.height, "CUT 2",
                                            hovered(ctx, InteractiveType::chopBoundary, 1));
            }
        }
    } else {
        ctx.canvas.fontSize(13.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                             DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        ctx.canvas.fillColor(colors.contentSecondary);
        text(ctx, uiLayout::chopWaveform.x + uiLayout::chopWaveform.width * 0.5f,
             uiLayout::chopWaveform.y + uiLayout::chopWaveform.height * 0.5f,
             ctx.state.chopSplitMode ? "LOADING RAW SAMPLE..."
                                     : "LOADING THREE COMPATIBLE RAW SAMPLES...",
             nullptr);
    }

    const int encodedPlayPad = ctx.state.chopPreviewPosition > 0.0f
                                   ? static_cast<int>(std::floor(ctx.state.chopPreviewPosition)) - 1
                                   : -1;
    const float playFraction =
        ctx.state.chopPreviewPosition > 0.0f
            ? ctx.state.chopPreviewPosition - std::floor(ctx.state.chopPreviewPosition)
            : 0.0f;
    int originalPad = -1;
    for (int index = 0; index < (ctx.state.chopSplitMode ? 1 : 3); ++index) {
        if (ctx.state.chopWaveforms[static_cast<std::size_t>(index)].pad ==
            static_cast<std::uint32_t>(encodedPlayPad))
            originalPad = index;
    }
    const double playSource =
        chop::sourceFrameForPlayhead(ctx.state.chopWaveforms, originalPad, playFraction);
    const auto total = chop::totalFrames(ctx.state.chopWaveforms);
    if (playSource >= 0.0 && total != 0U) {
        const float x = uiLayout::chopWaveform.x +
                        uiLayout::chopWaveform.width *
                            static_cast<float>(ctx.state.waveformViewport.zoomed()
                                                   ? ctx.state.waveformViewport.fraction(playSource)
                                                   : playSource / total);
        if (x >= uiLayout::chopWaveform.x &&
            x <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
            ctx.canvas.beginPath();
            ctx.canvas.moveTo(x, uiLayout::chopWaveform.y + 4.0f);
            ctx.canvas.lineTo(x, uiLayout::chopWaveform.y + uiLayout::chopWaveform.height - 4.0f);
            ctx.canvas.strokeColor(colors.selection);
            ctx.canvas.strokeWidth(2.0f);
            ctx.canvas.stroke();
        }
    }

    const int displayedPads = ctx.state.chopSplitMode ? 2 : 3;
    for (int pad = 0; pad < displayedPads; ++pad) {
        char label[48];
        const auto frames =
            chop::adjustedFrames(ctx.state.chopWaveforms, ctx.state.chopOffsets, pad);
        const double rate = chop::sampleRate(ctx.state.chopWaveforms);
        const double seconds = ctx.state.chopReady && rate > 0.0 ? frames / rate : 0.0;
        const bool previewEnabled = ctx.state.chopReady && frames != 0U && !ctx.state.chopApplying;
        std::snprintf(label, sizeof(label), "PAD %c%02d   %.2f s", bankLabel(chopPad(pad)),
                      localPadForGlobalPad(ctx, chopPad(pad)) + 1, seconds);
        sms::ui::dpf::drawSegment(
            ctx.canvas, uiLayout::chopPadButton(pad), label, ctx.state.chopPreviewPad == pad,
            pad == (ctx.state.chopSplitMode ? 0 : 1) ? colors.selection : colors.activityPlayback,
            hovered(ctx, InteractiveType::chopPadPreview, pad), previewEnabled);
    }
}

void drawChopEditorPanel(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    drawControlPanelBacking(ctx);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(13.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentPrimary);
    text(ctx, 994.0f, 120.0f,
         ctx.state.chopSplitMode ? "INSERT SAMPLE SPLIT" : "THREE-PAD CUT EDIT", nullptr);
    ctx.canvas.fontSize(10.0f);
    ctx.canvas.fillColor(colors.contentSecondary);
    ctx.canvas.textBox(
        994.0f, 164.0f, 222.0f,
        ctx.state.chopSplitMode
            ? "The selected sample is provisionally split in half. Later occupied pads will shift right."
            : "The waveform combines the selected pad with its immediate left and right neighbors.",
        nullptr);
    ctx.canvas.textBox(
        994.0f, 254.0f, 222.0f,
        ctx.state.chopSplitMode
            ? "Drag SPLIT. Click either pad button below the waveform to hear its pending raw slice."
            : "Drag CUT 1 or CUT 2. Click a pad button below the waveform to hear that raw slice using the pending cuts.",
        nullptr);
    ctx.canvas.textBox(
        994.0f, 382.0f, 222.0f,
        ctx.state.chopSplitMode
            ? "Apply inserts both halves and shifts pads atomically. Exit cancels the entire operation."
            : "Exit and arrows save cuts automatically. Cancel Changes restores the loaded cuts. Zero-length pads are cleared.",
        nullptr);
    sms::ui::dpf::drawSegment(
        ctx.canvas, uiLayout::chopAction,
        ctx.state.chopApplying ? (ctx.state.chopSplitMode ? "APPLYING..." : "SAVING...")
                               : (ctx.state.chopSplitMode ? "APPLY" : "CANCEL CHANGES"),
        false, colors.selection, hovered(ctx, InteractiveType::chopAction),
        ctx.state.chopReady && ctx.state.chopDirty && !ctx.state.chopApplying);
    drawChopArrow(ctx, uiLayout::chopPrevious, false, hovered(ctx, InteractiveType::chopPrevious),
                  ctx.state.chopPreviousEnabled && !ctx.state.chopApplying);
    drawChopExit(ctx);
    drawChopArrow(ctx, uiLayout::chopNext, true, hovered(ctx, InteractiveType::chopNext),
                  ctx.state.chopNextEnabled && !ctx.state.chopApplying);
}

void drawChopArrow(const ViewRenderContext& ctx, const sms::ui::Rect bounds, const bool pointsRight,
                   const bool isHovered, const bool enabled)
{
    const auto& colors = sms::ui::dpf::theme();
    sms::ui::dpf::drawRaisedControlSurface(ctx.canvas, bounds, colors.activityPlayback,
                                           {false, isHovered, false, enabled});
    const float centerY = bounds.y + bounds.height * 0.5f;
    ctx.canvas.beginPath();
    ctx.canvas.moveTo(uiLayout::chopArrowTailX(bounds, pointsRight), centerY - 10.0f);
    ctx.canvas.lineTo(uiLayout::chopArrowTipX(bounds, pointsRight), centerY);
    ctx.canvas.lineTo(uiLayout::chopArrowTailX(bounds, pointsRight), centerY + 10.0f);
    ctx.canvas.strokeColor((isHovered ? colors.activityPlayback : colors.contentSecondary)
                               .withAlpha(enabled ? 1.0f : colors.disabledAlpha));
    ctx.canvas.strokeWidth(2.5f);
    ctx.canvas.stroke();
}

void drawChopExit(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    const auto bounds = uiLayout::chopExit;
    const float centerX = bounds.x + bounds.width * 0.5f;
    const float centerY = bounds.y + bounds.height * 0.5f;
    const float radius = bounds.width * 0.5f;
    const bool isHovered = hovered(ctx, InteractiveType::chopExit);
    const bool enabled = !ctx.state.chopApplying;
    const float alpha = enabled ? 1.0f : colors.disabledAlpha;
    ctx.canvas.beginPath();
    ctx.canvas.circle(centerX + 1.0f, centerY + 2.0f, radius + 2.0f);
    ctx.canvas.fillPaint(ctx.canvas.radialGradient(centerX, centerY, radius * 0.35f, radius + 3.0f,
                                                   colors.shadow.withAlpha(0.72f * alpha),
                                                   colors.shadow.withAlpha(0.02f)));
    ctx.canvas.fill();
    ctx.canvas.beginPath();
    ctx.canvas.circle(centerX, centerY, radius);
    ctx.canvas.fillPaint(ctx.canvas.linearGradient(
        centerX, bounds.y, centerX, bounds.y + bounds.height, colors.controlTop.withAlpha(alpha),
        colors.controlBottom.withAlpha(alpha)));
    ctx.canvas.fill();
    ctx.canvas.strokeColor(isHovered && enabled ? colors.controlAccent
                                                : colors.outline.withAlpha(0.8f * alpha));
    ctx.canvas.strokeWidth(isHovered && enabled ? 2.0f : 1.0f);
    ctx.canvas.stroke();
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(10.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    ctx.canvas.fillColor(
        (isHovered ? colors.controlAccent : colors.contentSecondary).withAlpha(alpha));
    text(ctx, centerX, centerY, "EXIT", nullptr);
}

} // namespace

void drawCutEditorView(const ViewRenderContext& ctx)
{
    drawChopEditor(ctx);
    drawChopEditorPanel(ctx);
}

} // namespace midichopper::ui::rendering
