#pragma once

#include "AnalogMaterials.hpp"
#include "UI/Geometry.hpp"

#include <algorithm>
#include <cmath>

namespace sms::ui::dpf {

inline void drawPanel(DGL_NAMESPACE::NanoVG& canvas,
                      const float x, const float y, const float width, const float height)
{
    drawRecessedPanel(canvas, {x, y, width, height});
}

inline void drawPanel(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds)
{
    drawPanel(canvas, bounds.x, bounds.y, bounds.width, bounds.height);
}

inline void drawSegment(DGL_NAMESPACE::NanoVG& canvas,
                        const float x, const float y, const float width, const float height,
                        const char* const label, const bool active,
                        const DGL_NAMESPACE::Color& accent, const bool hovered = false,
                        const bool enabled = true)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    drawRaisedControlSurface(canvas, {x, y, width, height}, accent,
                             {active, hovered, false, enabled});
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(11.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    canvas.fillColor(enabled
        ? (hovered ? accent
                   : (active ? colors.controlActiveContent : colors.contentSecondary))
        : colors.contentSecondary.withAlpha(colors.disabledAlpha));
    canvas.text(x + width * 0.5f, y + height * 0.5f, label, nullptr);
}

inline void drawSegment(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                        const char* const label, const bool active,
                        const DGL_NAMESPACE::Color& accent, const bool hovered = false,
                        const bool enabled = true)
{
    drawSegment(canvas, bounds.x, bounds.y, bounds.width, bounds.height,
                label, active, accent, hovered, enabled);
}

inline void drawSlider(DGL_NAMESPACE::NanoVG& canvas,
                       const float x, const float y, const float width, const float value,
                       const DGL_NAMESPACE::Color& accent, const bool hovered = false)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    const float normalizedValue = std::clamp(value, 0.0f, 1.0f);
    canvas.beginPath();
    canvas.roundedRect(x - 1.0f, y - 1.0f, width + 2.0f, 8.0f, 4.0f);
    canvas.fillColor(colors.recessEdge);
    canvas.fill();
    canvas.beginPath();
    canvas.roundedRect(x, y, width, 6.0f, 3.0f);
    canvas.fillPaint(canvas.linearGradient(x, y, x, y + 6.0f,
        colors.shadow, colors.surfaceRaised));
    canvas.fill();
    canvas.beginPath();
    canvas.roundedRect(x, y, width * normalizedValue, 6.0f, 3.0f);
    canvas.fillColor(accent.withAlpha(0.8f));
    canvas.fill();
    const float capX = x + width * normalizedValue;
    canvas.beginPath();
    canvas.circle(capX + 1.0f, y + 4.5f, 9.0f);
    canvas.fillPaint(canvas.radialGradient(capX, y + 3.0f, 2.0f, 10.0f,
        colors.shadow.withAlpha(0.65f), colors.shadow.withAlpha(0.0f)));
    canvas.fill();
    canvas.beginPath();
    canvas.circle(capX, y + 3.0f, 8.0f);
    canvas.fillPaint(canvas.linearGradient(capX, y - 5.0f, capX, y + 11.0f,
        accent.plus(18), accent.minus(48)));
    canvas.fill();
    canvas.strokeColor(colors.contentPrimary.withAlpha(0.22f));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
    if (hovered) {
        canvas.strokeColor(accent.withAlpha(colors.hoverHaloAlpha));
        canvas.strokeWidth(3.0f);
        canvas.stroke();
    }
}

inline void drawKnob(DGL_NAMESPACE::NanoVG& canvas,
                     const float centerX, const float centerY, const float radius,
                     const float value, const DGL_NAMESPACE::Color& accent,
                     const bool hovered = false)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    const float normalized = std::clamp(value, 0.0f, 1.0f);
    constexpr float pi = 3.14159265358979323846f;
    constexpr float startAngle = pi * 0.75f;
    constexpr float sweep = pi * 1.5f;
    const float angle = startAngle + normalized * sweep;

    canvas.beginPath();
    canvas.circle(centerX + 1.0f, centerY + 2.0f, radius + 2.0f);
    canvas.fillPaint(canvas.radialGradient(centerX, centerY, radius * 0.35f, radius + 3.0f,
        colors.shadow.withAlpha(0.72f), colors.shadow.withAlpha(0.02f)));
    canvas.fill();
    canvas.beginPath();
    canvas.circle(centerX, centerY, radius);
    canvas.fillPaint(canvas.linearGradient(centerX, centerY - radius,
        centerX, centerY + radius, colors.surfaceRaised.plus(12), colors.recessEdge));
    canvas.fill();
    canvas.strokeColor(hovered ? accent : colors.outline.withAlpha(0.8f));
    canvas.strokeWidth(hovered ? 2.0f : 1.0f);
    canvas.stroke();

    canvas.beginPath();
    canvas.moveTo(centerX + std::cos(angle) * radius * 0.35f,
                  centerY + std::sin(angle) * radius * 0.35f);
    canvas.lineTo(centerX + std::cos(angle) * radius * 0.78f,
                  centerY + std::sin(angle) * radius * 0.78f);
    canvas.strokeColor(accent);
    canvas.strokeWidth(2.5f);
    canvas.stroke();
}

inline void drawAction(DGL_NAMESPACE::NanoVG& canvas,
                       const float x, const float y, const float width, const float height,
                       const char* const label, const DGL_NAMESPACE::Color& accent,
                       const bool active, const bool hovered = false)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    drawRaisedControlSurface(canvas, {x, y, width, height}, accent,
                             {active, hovered, false, true});
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(9.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    canvas.fillColor(hovered ? accent
                             : (active ? colors.controlActiveContent
                                       : colors.contentSecondary));
    canvas.text(x + width * 0.5f, y + height * 0.5f, label, nullptr);
}

inline void drawAction(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                       const char* const label, const DGL_NAMESPACE::Color& accent,
                       const bool active, const bool hovered = false)
{
    drawAction(canvas, bounds.x, bounds.y, bounds.width, bounds.height,
               label, accent, active, hovered);
}

} // namespace sms::ui::dpf
