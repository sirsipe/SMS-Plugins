#include "ProductControlsRenderer.hpp"
#include "ViewRenderers.hpp"
#include "DPF/AnalogMaterials.hpp"
#include "DPF/Controls.hpp"
#include "DPF/LevelMeter.hpp"
#include "Parameters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace midichopper::ui::rendering {

namespace uiLayout = layout;

namespace {

void drawLevelFader(const ViewRenderContext& ctx, const int index, const sms::ui::Rect bounds,
                    const std::array<float, 2>& levels, const float gainDb, const char* const label)
{
    const auto& colors = sms::ui::dpf::theme();
    sms::ui::dpf::drawStereoLedMeter(ctx.canvas, bounds, levels[0], levels[1], label, 18.0f);
    const auto track = uiLayout::levelFaderTrack(bounds);
    ctx.canvas.beginPath();
    ctx.canvas.roundedRect(track.x, bounds.y + 2.0f, track.width, bounds.height - 4.0f, 3.0f);
    ctx.canvas.fillColor(colors.recessEdge);
    ctx.canvas.fill();
    const auto range = index == 1
                           ? ctx.state.globalValueSettings[0]
                           : plugin::defaultGlobalValueSettings(plugin::kParameterOutputGainDb);
    for (int tick = 0; tick <= 12; ++tick) {
        const float y = track.y + track.height * static_cast<float>(tick) / 12.0f;
        const float cx = bounds.x + bounds.width * 0.5f;
        ctx.canvas.beginPath();
        ctx.canvas.moveTo(cx - 9.0f, y);
        ctx.canvas.lineTo(cx - 5.0f, y);
        ctx.canvas.moveTo(cx + 5.0f, y);
        ctx.canvas.lineTo(cx + 9.0f, y);
        ctx.canvas.strokeColor(colors.contentSecondary.withAlpha(tick == 4 ? 0.95f : 0.45f));
        ctx.canvas.strokeWidth(tick == 4 ? 1.5f : 1.0f);
        ctx.canvas.stroke();
    }
    const float normalized =
        std::clamp((gainDb - range.minimum) / (range.maximum - range.minimum), 0.0f, 1.0f);
    const float capY = track.y + (1.0f - normalized) * track.height;
    sms::ui::dpf::drawFaderCap(
        ctx.canvas, {bounds.x + bounds.width * 0.5f - 9.0f, capY - 17.0f, 18.0f, 34.0f}, true,
        colors.controlAccent, hovered(ctx, InteractiveType::levelFader, index));
    const auto valueBounds = uiLayout::levelValueLabel(index);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    char value[24];
    std::snprintf(value, sizeof(value), sms::ui::dpf::themeFormat("%+.1f dB"), gainDb);
    const bool entering =
        isTarget(ctx.state.mixerValueEntryTarget, InteractiveType::levelValueLabel, index);
    if (entering || hovered(ctx, InteractiveType::levelValueLabel, index)) {
        ctx.canvas.beginPath();
        ctx.canvas.roundedRect(valueBounds.x + uiLayout::contentOffsetX, valueBounds.y,
                               valueBounds.width, valueBounds.height, 2.0f);
        ctx.canvas.fillColor(colors.controlAccent.withAlpha(0.18f));
        ctx.canvas.fill();
    }
    ctx.canvas.fillColor(entering ? colors.selection : colors.contentPrimary);
    text(ctx, bounds.x + bounds.width * 0.5f, valueBounds.y + valueBounds.height * 0.5f,
         entering ? ctx.state.mixerValueEntryText : value, nullptr);
}

void drawToggleFrame(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                     const InteractiveType type)
{
    const auto& colors = sms::ui::dpf::theme();
    ctx.canvas.beginPath();
    ctx.canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 5.0f);
    ctx.canvas.fillPaint(ctx.canvas.linearGradient(
        bounds.x, bounds.y, bounds.x, bounds.y + bounds.height, colors.shadow, colors.recess));
    ctx.canvas.fill();
    ctx.canvas.strokeColor(hovered(ctx, type) ? colors.controlAccent : colors.recessEdge);
    ctx.canvas.strokeWidth(1.5f);
    ctx.canvas.stroke();
}

} // namespace

void drawViewportControls(const ViewRenderContext& ctx, const sms::ui::Rect zoom,
                          const sms::ui::Rect scroll)
{
    const auto& colors = sms::ui::dpf::theme();
    const auto& viewport = ctx.state.waveformViewport;
    const float alpha = viewport.total != 0U ? 1.0f : colors.disabledAlpha;
    ctx.canvas.beginPath();
    ctx.canvas.roundedRect(zoom.x, zoom.y, zoom.width, zoom.height, 4.0f);
    ctx.canvas.roundedRect(scroll.x, scroll.y, scroll.width, scroll.height, 4.0f);
    ctx.canvas.fillColor(colors.recess.withAlpha(alpha));
    ctx.canvas.fill();
    ctx.canvas.strokeColor(colors.outline.withAlpha(alpha));
    ctx.canvas.strokeWidth(1.0f);
    ctx.canvas.stroke();

    const float centerY = zoom.y + (1.0f - viewport.zoomPosition()) * (zoom.height - 12.0f) + 6.0f;
    ctx.canvas.beginPath();
    ctx.canvas.roundedRect(zoom.x + 6.0f, zoom.y + 3.0f, 4.0f, zoom.height - 6.0f, 2.0f);
    ctx.canvas.fillColor(colors.recessEdge.withAlpha(alpha));
    ctx.canvas.fill();
    for (int tick = 0; tick <= 10; ++tick) {
        const float tickY =
            zoom.y + 6.0f + (zoom.height - 12.0f) * static_cast<float>(tick) / 10.0f;
        ctx.canvas.beginPath();
        ctx.canvas.moveTo(zoom.x + 1.0f, tickY);
        ctx.canvas.lineTo(zoom.x + 4.0f, tickY);
        ctx.canvas.moveTo(zoom.x + 12.0f, tickY);
        ctx.canvas.lineTo(zoom.x + 15.0f, tickY);
        ctx.canvas.strokeColor(colors.contentSecondary.withAlpha(0.5f * alpha));
        ctx.canvas.strokeWidth(1.0f);
        ctx.canvas.stroke();
    }
    sms::ui::dpf::drawFaderCap(
        ctx.canvas, {zoom.x + 1.0f, centerY - 6.0f, zoom.width - 2.0f, 12.0f}, true,
        colors.selection, hovered(ctx, InteractiveType::waveformZoom), viewport.total != 0U);

    const auto window = viewport.scrollThumb(scroll);
    sms::ui::dpf::drawFaderCap(
        ctx.canvas, {window.x, window.y + 1.0f, window.width, window.height - 2.0f}, false,
        colors.selection, hovered(ctx, InteractiveType::waveformScroll), viewport.total != 0U);
}

void drawMixerKnob(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                   const InteractiveType type, const int index, const char* const label,
                   const float value, const float minimum, const float maximum,
                   const char* const display)
{
    const auto& colors = sms::ui::dpf::theme();
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, bounds.x + bounds.width * 0.5f, bounds.y, label, nullptr);
    const auto valueType = type == InteractiveType::mixerKnob
                               ? InteractiveType::mixerValueLabel
                               : InteractiveType::globalMixerValueLabel;
    const bool editing = isTarget(ctx.state.mixerValueEntryTarget, valueType, index);
    const auto valueBounds = type == InteractiveType::mixerKnob
                                 ? uiLayout::mixerValueLabel(index)
                                 : uiLayout::globalMixerValueLabel(index);
    if (editing) {
        ctx.canvas.beginPath();
        ctx.canvas.roundedRect(valueBounds.x + 2.0f, valueBounds.y, valueBounds.width - 4.0f,
                               valueBounds.height, 3.0f);
        ctx.canvas.fillColor(colors.recess);
        ctx.canvas.fill();
        ctx.canvas.strokeColor(colors.selection);
        ctx.canvas.strokeWidth(1.0f);
        ctx.canvas.stroke();
    }
    char entry[32];
    std::snprintf(entry, sizeof(entry), sms::ui::dpf::themeFormat("%s%s"),
                  editing ? ctx.state.mixerValueEntryText : display, editing ? "_" : "");
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentPrimary);
    text(ctx, bounds.x + bounds.width * 0.5f, bounds.y + 56.0f, entry, nullptr);
    const float normalized = bipolarKnobPosition(value, minimum, maximum);
    sms::ui::dpf::drawKnob(ctx.canvas, bounds.x + bounds.width * 0.5f, bounds.y + 31.0f, 16.0f,
                           normalized, colors.controlAccent, hovered(ctx, type, index));
}

void drawDirtySwitch(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                     const InteractiveType type, const bool enabled)
{
    const auto& colors = sms::ui::dpf::theme();
    const float centerX = bounds.x + bounds.width * 0.5f;
    const float slotY = bounds.y + 20.0f;
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, centerX, bounds.y, "DIRTY", nullptr);
    ctx.canvas.beginPath();
    ctx.canvas.roundedRect(centerX - 23.0f, slotY, 46.0f, 23.0f, 11.0f);
    ctx.canvas.fillPaint(ctx.canvas.linearGradient(centerX, slotY, centerX, slotY + 23.0f,
                                                   colors.shadow, colors.recess));
    ctx.canvas.fill();
    ctx.canvas.strokeColor(colors.recessEdge);
    ctx.canvas.strokeWidth(1.5f);
    ctx.canvas.stroke();
    const float leverX = centerX + (enabled ? 11.0f : -11.0f);
    ctx.canvas.beginPath();
    ctx.canvas.circle(leverX, slotY + 11.5f, 9.5f);
    ctx.canvas.fillPaint(
        ctx.canvas.radialGradient(leverX - 3.0f, slotY + 7.0f, 1.0f, 14.0f,
                                  enabled ? colors.controlAccent.plus(28) : colors.edgeHighlight,
                                  enabled ? colors.controlAccent.minus(45) : colors.controlBottom));
    ctx.canvas.fill();
    ctx.canvas.strokeColor(hovered(ctx, type, 6) ? colors.controlAccent : colors.outline);
    ctx.canvas.strokeWidth(1.3f);
    ctx.canvas.stroke();
    ctx.canvas.fillColor(enabled ? colors.controlAccent : colors.contentSecondary);
    text(ctx, centerX, bounds.y + 56.0f, enabled ? "ON" : "OFF", nullptr);
}

void drawDirectionToggle(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                         const InteractiveType type, const bool reverse)
{
    const auto& colors = sms::ui::dpf::theme();
    drawToggleFrame(ctx, bounds, type);
    const float centerY = bounds.y + bounds.height * 0.5f;
    const float direction = reverse ? -1.0f : 1.0f;
    const float arrowWidth = bounds.width * 0.11f;
    const float arrowHeight = bounds.height * 0.18f;
    ctx.canvas.fillColor(reverse ? colors.controlAccent : colors.contentSecondary.withAlpha(0.5f));
    for (int arrow = 0; arrow < 2; ++arrow) {
        const float centerX = bounds.x + bounds.width * (arrow == 0 ? 0.37f : 0.63f);
        ctx.canvas.beginPath();
        ctx.canvas.moveTo(centerX + direction * arrowWidth, centerY);
        ctx.canvas.lineTo(centerX - direction * arrowWidth, centerY - arrowHeight);
        ctx.canvas.lineTo(centerX - direction * arrowWidth, centerY + arrowHeight);
        ctx.canvas.closePath();
        ctx.canvas.fill();
    }
}

void drawGlobalMixerToggle(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    const auto bounds = uiLayout::padGlobalMixer;
    const bool enabled = ctx.state.mixerSettings.globalMixer >= 0.5f;
    drawToggleFrame(ctx, bounds, InteractiveType::padGlobalMixer);
    const float centerX = bounds.x + bounds.width * 0.5f;
    const float centerY = bounds.y + bounds.height * 0.5f;
    ctx.canvas.strokeColor(enabled ? colors.controlAccent
                                   : colors.contentSecondary.withAlpha(0.5f));
    ctx.canvas.strokeWidth(2.4f);
    ctx.canvas.beginPath();
    ctx.canvas.arc(centerX, centerY, 9.0f, -0.7853982f, 3.9269908f, DGL_NAMESPACE::NanoVG::CW);
    ctx.canvas.stroke();
    ctx.canvas.beginPath();
    ctx.canvas.moveTo(centerX, centerY - 11.0f);
    ctx.canvas.lineTo(centerX, centerY);
    ctx.canvas.stroke();
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, centerX, uiLayout::mixerKnob(4).y - 1.0f, "GLOBAL MIXER", nullptr);
}

void drawSlopeSlider(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                     const InteractiveType type, const float slope)
{
    const auto& colors = sms::ui::dpf::theme();
    const auto track = uiLayout::slopeTrack(bounds);
    const int index = std::clamp(static_cast<int>(std::lround(slope)), 0, 2);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, bounds.x + bounds.width * 0.5f, bounds.y + 2.0f, "SLOPE", nullptr);
    char value[20];
    std::snprintf(value, sizeof(value), sms::ui::dpf::themeFormat("%d dB"),
                  index == 0   ? 6
                  : index == 1 ? 12
                               : 24);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentPrimary);
    text(ctx, track.x + track.width, bounds.y + 5.0f, value, nullptr);
    sms::ui::dpf::drawSlider(ctx.canvas, track.x, track.y, track.width,
                             static_cast<float>(index) * 0.5f, colors.controlAccent,
                             hovered(ctx, type, 5));
    for (int tick = 0; tick < 3; ++tick) {
        const float tickX = track.x + track.width * static_cast<float>(tick) * 0.5f;
        ctx.canvas.beginPath();
        ctx.canvas.moveTo(tickX, track.y - 4.0f);
        ctx.canvas.lineTo(tickX, track.y - 1.0f);
        ctx.canvas.strokeColor(colors.contentSecondary.withAlpha(0.65f));
        ctx.canvas.strokeWidth(1.0f);
        ctx.canvas.stroke();
    }
}

void drawGlobalMixer(const ViewRenderContext& ctx)
{
    if (ctx.state.armed)
        return;
    const auto& colors = sms::ui::dpf::theme();
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(11.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 994.0f, uiLayout::globalMixerLabelY, "GLOBAL MIXER", nullptr);
    drawDirectionToggle(ctx, uiLayout::globalDirection, InteractiveType::globalDirection,
                        ctx.state.globalReverse);
    char pan[24];
    char tune[24];
    char low[16];
    char high[16];
    if (std::abs(ctx.state.globalPan) < 0.005f)
        std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%s"),
                      sms::ui::dpf::themeText("CENTER"));
    else
        std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%c%d"),
                      ctx.state.globalPan < 0.0f ? 'L' : 'R',
                      static_cast<int>(std::lround(std::abs(ctx.state.globalPan) * 100.0f)));
    std::snprintf(tune, sizeof(tune), sms::ui::dpf::themeFormat("%+.2f st"),
                  ctx.state.globalTuneSemitones);
    std::snprintf(low, sizeof(low), sms::ui::dpf::themeFormat("%.0f%%"),
                  ctx.state.globalLowpass * 100.0f);
    std::snprintf(high, sizeof(high), sms::ui::dpf::themeFormat("%.0f%%"),
                  ctx.state.globalHighpass * 100.0f);
    drawMixerKnob(ctx, uiLayout::globalMixerKnob(1), InteractiveType::globalMixerKnob, 1, "PAN",
                  ctx.state.globalPan, ctx.state.globalValueSettings[1].minimum,
                  ctx.state.globalValueSettings[1].maximum, pan);
    drawDirtySwitch(ctx, uiLayout::globalMixerKnob(6), InteractiveType::globalMixerKnob,
                    ctx.state.globalDirty >= 0.5f);
    drawMixerKnob(ctx, uiLayout::globalMixerKnob(2), InteractiveType::globalMixerKnob, 2, "TUNE",
                  ctx.state.globalTuneSemitones, ctx.state.globalValueSettings[2].minimum,
                  ctx.state.globalValueSettings[2].maximum, tune);
    drawMixerKnob(ctx, uiLayout::globalMixerKnob(3), InteractiveType::globalMixerKnob, 3, "LOWPASS",
                  ctx.state.globalLowpass, ctx.state.globalValueSettings[3].minimum,
                  ctx.state.globalValueSettings[3].maximum, low);
    drawMixerKnob(ctx, uiLayout::globalMixerKnob(4), InteractiveType::globalMixerKnob, 4,
                  "HIGHPASS", ctx.state.globalHighpass, ctx.state.globalValueSettings[4].minimum,
                  ctx.state.globalValueSettings[4].maximum, high);
    drawSlopeSlider(ctx, uiLayout::globalMixerKnob(5), InteractiveType::globalMixerKnob,
                    ctx.state.globalFilterSlope);
}

void drawControlPanelBacking(const ViewRenderContext& ctx)
{
    const sms::ui::dpf::ScopedCanvasState panelState(ctx.canvas);
    ctx.canvas.globalAlpha(sms::ui::dpf::theme().controlPanelAlpha);
    sms::ui::dpf::drawPanel(ctx.canvas, uiLayout::sidePanel);
}

void drawLevelFaders(const ViewRenderContext& ctx)
{
    drawLevelFader(ctx, 0, uiLayout::inputMeter, ctx.state.inputLevels, ctx.state.monitorGainDb,
                   "IN");
    drawLevelFader(ctx, 1, uiLayout::outputMeter, ctx.state.outputLevels, ctx.state.outputGainDb,
                   "OUT");
}

} // namespace midichopper::ui::rendering
