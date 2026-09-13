#pragma once

#include "Theme.hpp"
#include "UI/Geometry.hpp"

#include <algorithm>
#include <cstdint>

namespace sms::ui::dpf {

/** Keeps reusable drawing primitives from leaking NanoVG state to callers. */
class ScopedCanvasState {
public:
    explicit ScopedCanvasState(DGL_NAMESPACE::NanoVG& canvas) : canvas_(canvas)
    {
        canvas_.save();
    }

    ~ScopedCanvasState()
    {
        canvas_.restore();
    }

    ScopedCanvasState(const ScopedCanvasState&) = delete;
    ScopedCanvasState& operator=(const ScopedCanvasState&) = delete;

private:
    DGL_NAMESPACE::NanoVG& canvas_;
};

inline void drawFineGrain(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    canvas.scissor(bounds.x, bounds.y, bounds.width, bounds.height);

    // A fixed sequence makes the powder-coat texture stable between repaints.
    const int grainCount = std::clamp(
        static_cast<int>(bounds.width * bounds.height / 2500.0f), 8, 180);
    std::uint32_t random = 0x6d2b79f5U;
    canvas.beginPath();
    for (int index = 0; index < grainCount; ++index) {
        random = random * 1664525U + 1013904223U;
        const float x = bounds.x + static_cast<float>(random & 0xffffU) / 65535.0f * bounds.width;
        random = random * 1664525U + 1013904223U;
        const float y = bounds.y + static_cast<float>(random & 0xffffU) / 65535.0f * bounds.height;
        const float length = 0.7f + static_cast<float>((random >> 16U) & 3U) * 0.25f;
        canvas.moveTo(x, y);
        canvas.lineTo(x + length, y + 0.25f);
    }
    canvas.strokeColor(colors.edgeHighlight.withAlpha(colors.materialGrainAlpha));
    canvas.strokeWidth(0.55f);
    canvas.stroke();
}

inline void drawChassis(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    canvas.beginPath();
    canvas.roundedRect(bounds.x + 1.0f, bounds.y + 1.0f,
                       bounds.width - 2.0f, bounds.height - 2.0f, 8.0f);
    canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y,
        bounds.x, bounds.y + bounds.height, colors.chassisTop, colors.chassisBottom));
    canvas.fill();
    canvas.strokeColor(colors.chassisEdge.withAlpha(0.72f));
    canvas.strokeWidth(1.4f);
    canvas.stroke();

    canvas.beginPath();
    canvas.roundedRect(bounds.x + 4.0f, bounds.y + 4.0f,
                       bounds.width - 8.0f, bounds.height - 8.0f, 6.0f);
    canvas.strokeColor(colors.edgeHighlight.withAlpha(0.18f));
    canvas.strokeWidth(1.0f);
    canvas.stroke();
    drawFineGrain(canvas, {bounds.x + 5.0f, bounds.y + 5.0f,
                           bounds.width - 10.0f, bounds.height - 10.0f});
}

inline void drawScrew(DGL_NAMESPACE::NanoVG& canvas, const float x, const float y)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    canvas.beginPath();
    canvas.circle(x + 1.0f, y + 1.5f, 8.5f);
    canvas.fillPaint(canvas.radialGradient(x, y, 2.0f, 9.0f,
        colors.shadow.withAlpha(0.75f), colors.shadow.withAlpha(0.02f)));
    canvas.fill();
    canvas.beginPath();
    canvas.circle(x, y, 7.0f);
    canvas.fillPaint(canvas.linearGradient(x, y - 7.0f, x, y + 7.0f,
        colors.controlTop, colors.recessEdge));
    canvas.fill();
    canvas.strokeColor(colors.edgeHighlight.withAlpha(0.30f));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
    canvas.beginPath();
    canvas.moveTo(x - 3.2f, y - 3.2f);
    canvas.lineTo(x + 3.2f, y + 3.2f);
    canvas.moveTo(x + 3.2f, y - 3.2f);
    canvas.lineTo(x - 3.2f, y + 3.2f);
    canvas.strokeColor(colors.shadow.withAlpha(0.85f));
    canvas.strokeWidth(1.4f);
    canvas.stroke();
}

inline void drawRecessedPanel(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                              const float radius = -1.0f)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    const float corner = radius >= 0.0f ? radius : colors.panelRadius;
    canvas.beginPath();
    canvas.roundedRect(bounds.x - 3.0f, bounds.y - 3.0f,
                       bounds.width + 6.0f, bounds.height + 6.0f, corner + 3.0f);
    canvas.fillPaint(canvas.boxGradient(bounds.x - 3.0f, bounds.y - 3.0f,
        bounds.width + 6.0f, bounds.height + 6.0f, corner + 3.0f, 5.0f,
        colors.shadow.withAlpha(0.78f), colors.shadow.withAlpha(0.02f)));
    canvas.fill();
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, corner);
    canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y,
        bounds.x, bounds.y + bounds.height, colors.panelBottom, colors.panelTop));
    canvas.fill();
    canvas.strokeColor(colors.recessEdge);
    canvas.strokeWidth(2.0f);
    canvas.stroke();
    canvas.beginPath();
    canvas.roundedRect(bounds.x + 2.5f, bounds.y + 2.5f,
                       bounds.width - 5.0f, bounds.height - 5.0f,
                       std::max(1.0f, corner - 2.0f));
    canvas.strokeColor(colors.edgeHighlight.withAlpha(0.16f));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
    drawFineGrain(canvas, {bounds.x + 3.0f, bounds.y + 3.0f,
                           bounds.width - 6.0f, bounds.height - 6.0f});
}

inline void drawInsetSurface(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                             const float radius)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, radius);
    canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y,
        bounds.x, bounds.y + bounds.height, colors.recess, colors.surface));
    canvas.fill();
    canvas.strokeColor(colors.recessEdge);
    canvas.strokeWidth(1.8f);
    canvas.stroke();
    canvas.beginPath();
    canvas.roundedRect(bounds.x + 2.0f, bounds.y + 2.0f,
                       bounds.width - 4.0f, bounds.height - 4.0f,
                       std::max(1.0f, radius - 1.5f));
    canvas.strokeColor(colors.edgeHighlight.withAlpha(0.12f));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
}

struct ControlSurfaceState {
    bool active = false;
    bool hovered = false;
    bool pressed = false;
    bool enabled = true;
};

inline void drawRaisedControlSurface(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                                     const DGL_NAMESPACE::Color& accent,
                                     const ControlSurfaceState state = {})
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    const float depression = state.pressed ? 1.0f : 0.0f;
    const float radius = colors.controlRadius;
    const float alpha = state.enabled ? 1.0f : colors.disabledAlpha;
    if (state.active && state.enabled) {
        canvas.beginPath();
        canvas.roundedRect(bounds.x - 4.0f, bounds.y - 4.0f,
                           bounds.width + 8.0f, bounds.height + 8.0f, radius + 4.0f);
        canvas.fillPaint(canvas.boxGradient(bounds.x, bounds.y, bounds.width, bounds.height,
            radius, 7.0f, accent.withAlpha(0.18f), accent.withAlpha(0.0f)));
        canvas.fill();
    }
    canvas.beginPath();
    canvas.roundedRect(bounds.x - 2.0f, bounds.y - 1.0f + depression,
                       bounds.width + 4.0f, bounds.height + 4.0f, radius + 2.0f);
    canvas.fillPaint(canvas.boxGradient(bounds.x - 2.0f, bounds.y - 1.0f + depression,
        bounds.width + 4.0f, bounds.height + 4.0f, radius + 2.0f, 4.0f,
        colors.shadow.withAlpha(colors.shallowShadowAlpha * alpha),
        colors.shadow.withAlpha(0.0f)));
    canvas.fill();

    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y + depression, bounds.width, bounds.height, radius);
    if (state.active) {
        canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y,
            bounds.x, bounds.y + bounds.height,
            accent.withAlpha(0.34f * alpha), colors.controlBottom.withAlpha(alpha)));
    } else {
        canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y,
            bounds.x, bounds.y + bounds.height,
            colors.controlTop.withAlpha(alpha), colors.controlBottom.withAlpha(alpha)));
    }
    canvas.fill();
    if (state.hovered && state.enabled) {
        canvas.beginPath();
        canvas.roundedRect(bounds.x + 1.0f, bounds.y + 1.0f + depression,
                           bounds.width - 2.0f, bounds.height - 2.0f,
                           std::max(1.0f, radius - 1.0f));
        canvas.fillColor(accent.withAlpha(colors.hoverFillAlpha));
        canvas.fill();
    }
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y + depression, bounds.width, bounds.height, radius);
    canvas.strokeColor((state.active || state.hovered) && state.enabled
        ? accent : colors.outline.withAlpha(alpha));
    canvas.strokeWidth(state.active ? 1.8f : (state.hovered ? 1.35f : 1.0f));
    canvas.stroke();
    canvas.beginPath();
    canvas.moveTo(bounds.x + radius, bounds.y + 1.5f + depression);
    canvas.lineTo(bounds.x + bounds.width - radius, bounds.y + 1.5f + depression);
    canvas.strokeColor(colors.edgeHighlight.withAlpha(
        colors.bevelHighlightAlpha * alpha * (state.pressed ? 0.35f : 1.0f)));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
}

struct PadSurfaceState {
    bool occupied = false;
    bool active = false;
    bool recording = false;
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool enabled = true;
};

inline void drawRubberPad(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                          const PadSurfaceState state, const float radius = 9.0f)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    const auto& accent = state.recording ? colors.activityCapture : colors.activityPlayback;
    const float depression = state.pressed ? 2.0f : 0.0f;
    const float alpha = state.enabled ? 1.0f : colors.disabledAlpha;

    if (state.selected && state.enabled) {
        canvas.beginPath();
        canvas.roundedRect(bounds.x - 5.0f, bounds.y - 5.0f,
                           bounds.width + 10.0f, bounds.height + 10.0f, radius + 5.0f);
        canvas.fillPaint(canvas.boxGradient(bounds.x, bounds.y, bounds.width, bounds.height,
            radius, 9.0f, colors.selection.withAlpha(0.24f),
            colors.selection.withAlpha(0.0f)));
        canvas.fill();
    }
    canvas.beginPath();
    canvas.roundedRect(bounds.x - 3.0f, bounds.y - 2.0f + depression,
                       bounds.width + 6.0f, bounds.height + 7.0f, radius + 3.0f);
    canvas.fillPaint(canvas.boxGradient(bounds.x - 3.0f, bounds.y - 2.0f + depression,
        bounds.width + 6.0f, bounds.height + 7.0f, radius + 3.0f, 6.0f,
        colors.shadow.withAlpha(0.72f * alpha), colors.shadow.withAlpha(0.0f)));
    canvas.fill();
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y + depression, bounds.width, bounds.height, radius);
    canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y + depression,
        bounds.x, bounds.y + bounds.height + depression,
        colors.rubberTop.withAlpha(alpha), colors.rubberBottom.withAlpha(alpha)));
    canvas.fill();
    if ((state.occupied || state.active || state.recording) && state.enabled) {
        canvas.beginPath();
        canvas.roundedRect(bounds.x + 2.0f, bounds.y + 2.0f + depression,
                           bounds.width - 4.0f, bounds.height - 4.0f, radius - 2.0f);
        canvas.fillColor(accent.withAlpha(state.active ? 0.18f : 0.08f));
        canvas.fill();
    }
    if (state.hovered && state.enabled) {
        canvas.beginPath();
        canvas.roundedRect(bounds.x + 2.0f, bounds.y + 2.0f + depression,
                           bounds.width - 4.0f, bounds.height - 4.0f, radius - 2.0f);
        canvas.fillColor(colors.selection.withAlpha(colors.hoverFillAlpha));
        canvas.fill();
    }
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y + depression, bounds.width, bounds.height, radius);
    canvas.strokeColor((state.selected || state.hovered) && state.enabled
        ? colors.selection : colors.outline.withAlpha(0.72f * alpha));
    canvas.strokeWidth(state.selected ? 2.0f : (state.hovered ? 1.5f : 1.0f));
    canvas.stroke();
    canvas.beginPath();
    canvas.moveTo(bounds.x + radius, bounds.y + 1.5f + depression);
    canvas.lineTo(bounds.x + bounds.width - radius, bounds.y + 1.5f + depression);
    canvas.strokeColor(colors.edgeHighlight.withAlpha(
        colors.bevelHighlightAlpha * alpha * (state.pressed ? 0.35f : 1.0f)));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
    drawFineGrain(canvas, {bounds.x + 4.0f, bounds.y + 4.0f + depression,
                           bounds.width - 8.0f, bounds.height - 8.0f});
}

inline void drawLed(DGL_NAMESPACE::NanoVG& canvas, const float x, const float y,
                    const DGL_NAMESPACE::Color& color, const bool illuminated)
{
    const Theme& colors = theme();
    const ScopedCanvasState canvasState(canvas);
    if (illuminated) {
        canvas.beginPath();
        canvas.circle(x, y, 11.0f);
        canvas.fillPaint(canvas.radialGradient(x, y, 2.0f, 11.0f,
            color.withAlpha(0.34f), color.withAlpha(0.0f)));
        canvas.fill();
    }
    canvas.beginPath();
    canvas.circle(x, y, 6.0f);
    canvas.fillPaint(canvas.radialGradient(x - 1.5f, y - 2.0f, 0.8f, 6.0f,
        illuminated ? colors.contentPrimary : colors.outline,
        illuminated ? color : colors.recessEdge));
    canvas.fill();
    canvas.strokeColor(colors.shadow.withAlpha(0.75f));
    canvas.strokeWidth(1.0f);
    canvas.stroke();
}

} // namespace sms::ui::dpf
