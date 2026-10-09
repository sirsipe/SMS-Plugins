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

enum class ControlSymbol { play, stop, record };

inline void drawSymbolButton(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                             const char* const label, const ControlSymbol symbol,
                             const bool active, const DGL_NAMESPACE::Color& accent,
                             const bool hovered = false)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    drawRaisedControlSurface(canvas, bounds, accent, {active, hovered, false, true});
    const float centerX = bounds.x + bounds.width * 0.5f;
    const float centerY = bounds.y + bounds.height * 0.5f;
    const float iconX = centerX - 23.0f;
    canvas.beginPath();
    if (symbol == ControlSymbol::play) {
        canvas.moveTo(iconX - 5.0f, centerY - 6.0f);
        canvas.lineTo(iconX + 6.0f, centerY);
        canvas.lineTo(iconX - 5.0f, centerY + 6.0f);
        canvas.closePath();
    } else if (symbol == ControlSymbol::stop) {
        canvas.rect(iconX - 5.0f, centerY - 5.0f, 10.0f, 10.0f);
    } else {
        canvas.circle(iconX, centerY, 5.5f);
    }
    canvas.fillColor(symbol == ControlSymbol::record ? colors.intentDanger
                                                    : colors.faderTop);
    canvas.fill();
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(11.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    canvas.fillColor(colors.contentPrimary);
    canvas.text(centerX + 10.0f, centerY, label, nullptr);
}

/** Ivory console cap; its dark index line is perpendicular to travel. */
inline void drawFaderCap(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                         const bool verticalTravel, const DGL_NAMESPACE::Color& accent,
                         const bool hovered = false, const bool enabled = true)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    const float alpha = enabled ? 1.0f : colors.disabledAlpha;
    canvas.beginPath();
    canvas.roundedRect(bounds.x + 1.5f, bounds.y + 2.0f,
                       bounds.width + 1.0f, bounds.height + 1.0f, 1.5f);
    canvas.fillColor(colors.shadow.withAlpha(0.65f * alpha));
    canvas.fill();
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 1.0f);
    canvas.fillPaint(canvas.linearGradient(bounds.x, bounds.y,
        bounds.x + bounds.width, bounds.y + bounds.height,
        colors.faderTop.withAlpha(alpha), colors.faderBottom.withAlpha(alpha)));
    canvas.fill();
    canvas.strokeColor(hovered ? accent : colors.edgeHighlight.withAlpha(alpha));
    canvas.strokeWidth(hovered ? 1.5f : 1.0f);
    canvas.stroke();
    canvas.beginPath();
    canvas.rect(bounds.x + 2.0f, bounds.y + 2.0f,
                bounds.width - 4.0f, bounds.height - 4.0f);
    canvas.fillColor(colors.faderTop.withAlpha(0.55f * alpha));
    canvas.fill();
    const float cx = bounds.x + bounds.width * 0.5f;
    const float cy = bounds.y + bounds.height * 0.5f;
    canvas.beginPath();
    canvas.moveTo(verticalTravel ? bounds.x + 1.0f : cx,
                  verticalTravel ? cy : bounds.y + 1.0f);
    canvas.lineTo(verticalTravel ? bounds.x + bounds.width - 1.0f : cx,
                  verticalTravel ? cy : bounds.y + bounds.height - 1.0f);
    canvas.strokeColor(colors.recessEdge.withAlpha(alpha));
    canvas.strokeWidth(1.5f);
    canvas.stroke();
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
    for (int tick = 0; tick <= 10; ++tick) {
        const float tickX = x + width * static_cast<float>(tick) / 10.0f;
        const float length = tick % 5 == 0 ? 4.0f : 2.0f;
        canvas.beginPath();
        canvas.moveTo(tickX, y - 2.0f);
        canvas.lineTo(tickX, y - 2.0f - length);
        canvas.moveTo(tickX, y + 8.0f);
        canvas.lineTo(tickX, y + 8.0f + length);
        canvas.strokeColor(colors.contentSecondary.withAlpha(0.55f));
        canvas.strokeWidth(1.0f);
        canvas.stroke();
    }
    const float capX = x + width * normalizedValue;
    drawFaderCap(canvas, {capX - 7.0f, y - 5.0f, 14.0f, 16.0f}, false,
                 accent, hovered);
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

    for (int tick = 0; tick <= 10; ++tick) {
        const float tickAngle = startAngle + sweep * static_cast<float>(tick) / 10.0f;
        const float inner = radius + 3.0f;
        const float outer = radius + (tick % 5 == 0 ? 8.0f : 6.0f);
        canvas.beginPath();
        canvas.moveTo(centerX + std::cos(tickAngle) * inner,
                      centerY + std::sin(tickAngle) * inner);
        canvas.lineTo(centerX + std::cos(tickAngle) * outer,
                      centerY + std::sin(tickAngle) * outer);
        canvas.strokeColor(tick <= static_cast<int>(std::lround(normalized * 10.0f))
            ? accent.withAlpha(0.75f) : colors.contentSecondary.withAlpha(0.36f));
        canvas.strokeWidth(tick % 5 == 0 ? 1.5f : 1.0f);
        canvas.stroke();
    }

    canvas.beginPath();
    canvas.circle(centerX + 1.0f, centerY + 2.0f, radius + 2.0f);
    canvas.fillPaint(canvas.radialGradient(centerX, centerY, radius * 0.35f, radius + 3.0f,
        colors.shadow.withAlpha(0.72f), colors.shadow.withAlpha(0.02f)));
    canvas.fill();
    canvas.beginPath();
    canvas.circle(centerX, centerY, radius);
    canvas.fillPaint(canvas.linearGradient(centerX, centerY - radius,
        centerX, centerY + radius, colors.edgeHighlight.withAlpha(0.75f),
        colors.recessEdge));
    canvas.fill();
    canvas.strokeColor(hovered ? accent : colors.outline.withAlpha(0.8f));
    canvas.strokeWidth(hovered ? 2.0f : 1.0f);
    canvas.stroke();
    canvas.beginPath();
    canvas.circle(centerX, centerY, radius - 3.0f);
    canvas.fillPaint(canvas.radialGradient(centerX - radius * 0.3f,
        centerY - radius * 0.35f, 2.0f, radius * 1.25f,
        colors.surfaceRaised.plus(24), colors.controlBottom));
    canvas.fill();
    canvas.strokeColor(colors.edgeHighlight.withAlpha(0.30f));
    canvas.strokeWidth(0.8f);
    canvas.stroke();
    canvas.beginPath();
    canvas.circle(centerX, centerY, radius * 0.18f);
    canvas.fillColor(colors.recessEdge.withAlpha(0.65f));
    canvas.fill();

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
