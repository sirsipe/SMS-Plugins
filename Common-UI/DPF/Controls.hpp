#pragma once

#include "Theme.hpp"
#include "UI/Geometry.hpp"

#include <algorithm>

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

inline void drawPanel(DGL_NAMESPACE::NanoVG& canvas,
                      const float x, const float y, const float width, const float height)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.beginPath();
    canvas.roundedRect(x, y, width, height, colors.panelRadius);
    canvas.fillColor(colors.surface);
    canvas.fill();
    canvas.strokeColor(colors.outline);
    canvas.strokeWidth(colors.outlineWidth);
    canvas.stroke();
}

inline void drawPanel(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds)
{
    drawPanel(canvas, bounds.x, bounds.y, bounds.width, bounds.height);
}

inline void drawSegment(DGL_NAMESPACE::NanoVG& canvas,
                        const float x, const float y, const float width, const float height,
                        const char* const label, const bool active,
                        const DGL_NAMESPACE::Color& accent)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.beginPath();
    canvas.roundedRect(x, y, width, height, 7.0f);
    canvas.fillColor(active ? accent.withAlpha(0.20f) : colors.surfaceRaised);
    canvas.fill();
    canvas.strokeColor(active ? accent : colors.outline);
    canvas.strokeWidth(active ? 1.5f : colors.outlineWidth);
    canvas.stroke();
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(11.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    canvas.fillColor(active ? accent : colors.contentSecondary);
    canvas.text(x + width * 0.5f, y + height * 0.5f, label, nullptr);
}

inline void drawSegment(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                        const char* const label, const bool active,
                        const DGL_NAMESPACE::Color& accent)
{
    drawSegment(canvas, bounds.x, bounds.y, bounds.width, bounds.height,
                label, active, accent);
}

inline void drawSlider(DGL_NAMESPACE::NanoVG& canvas,
                       const float x, const float y, const float width, const float value,
                       const DGL_NAMESPACE::Color& accent)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.beginPath();
    const float normalizedValue = std::clamp(value, 0.0f, 1.0f);
    canvas.roundedRect(x, y, width, 6.0f, 3.0f);
    canvas.fillColor(colors.surfaceRaised);
    canvas.fill();
    canvas.beginPath();
    canvas.roundedRect(x, y, width * normalizedValue, 6.0f, 3.0f);
    canvas.fillColor(accent.withAlpha(0.8f));
    canvas.fill();
    canvas.beginPath();
    canvas.circle(x + width * normalizedValue, y + 3.0f, 8.0f);
    canvas.fillColor(accent);
    canvas.fill();
}

inline void drawAction(DGL_NAMESPACE::NanoVG& canvas,
                       const float x, const float y, const float width, const float height,
                       const char* const label, const DGL_NAMESPACE::Color& accent,
                       const bool active)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.beginPath();
    canvas.roundedRect(x, y, width, height, colors.controlRadius);
    canvas.fillColor(active ? accent.withAlpha(0.28f) : colors.surfaceRaised);
    canvas.fill();
    canvas.strokeColor(accent.withAlpha(active ? 1.0f : 0.65f));
    canvas.strokeWidth(colors.outlineWidth);
    canvas.stroke();
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(9.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    canvas.fillColor(active ? accent : colors.contentSecondary);
    canvas.text(x + width * 0.5f, y + height * 0.5f, label, nullptr);
}

inline void drawAction(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                       const char* const label, const DGL_NAMESPACE::Color& accent,
                       const bool active)
{
    drawAction(canvas, bounds.x, bounds.y, bounds.width, bounds.height,
               label, accent, active);
}

} // namespace sms::ui::dpf
