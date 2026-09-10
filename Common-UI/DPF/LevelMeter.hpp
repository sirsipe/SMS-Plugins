#pragma once

#include "DPF/Controls.hpp"
#include "DPF/Theme.hpp"
#include "../LevelMeter.hpp"

#include <algorithm>
#include <array>

namespace sms::ui::dpf {

inline void drawStereoLedMeter(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                               const float left, const float right,
                               const char* const label)
{
    ScopedCanvasState state(canvas);
    const auto& colors = theme();
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);
    canvas.fillColor(colors.surface);
    canvas.fill();
    canvas.strokeColor(colors.outline);
    canvas.strokeWidth(1.0f);
    canvas.stroke();

    constexpr std::uint32_t segmentCount = meter::defaultSegmentCount;
    const meter::StereoGeometry geometry(bounds, segmentCount);
    const std::array active{
        meter::activeSegmentCount(left, segmentCount),
        meter::activeSegmentCount(right, segmentCount),
    };

    constexpr std::array zones{
        meter::Zone::green, meter::Zone::yellow, meter::Zone::red,
    };
    constexpr std::array illuminationStates{false, true};
    for (const bool illuminated : illuminationStates) {
        for (const meter::Zone zone : zones) {
            canvas.beginPath();
            bool hasSegments = false;
            for (std::uint32_t channel = 0; channel < 2U; ++channel) {
                for (std::uint32_t segment = 0; segment < segmentCount; ++segment) {
                    if ((segment < active[channel]) != illuminated ||
                        meter::segmentZone(segment, segmentCount) != zone)
                        continue;
                    const ui::Rect led = geometry.segment(channel, segment);
                    canvas.roundedRect(led.x, led.y, led.width, led.height,
                                       std::min(1.5f, led.height * 0.3f));
                    hasSegments = true;
                }
            }
            if (!hasSegments)
                continue;
            switch (zone) {
            case meter::Zone::green: canvas.fillColor(colors.meterGreen.withAlpha(
                illuminated ? 0.95f : 0.12f)); break;
            case meter::Zone::yellow: canvas.fillColor(colors.meterYellow.withAlpha(
                illuminated ? 0.95f : 0.12f)); break;
            case meter::Zone::red: canvas.fillColor(colors.meterRed.withAlpha(
                illuminated ? 0.95f : 0.12f)); break;
            }
            canvas.fill();
        }
    }

    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(10.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_BOTTOM);
    canvas.fillColor(colors.contentSecondary);
    canvas.text(bounds.x + bounds.width * 0.5f, bounds.y - 7.0f, label, nullptr);
    canvas.fontSize(9.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    const auto leftColumn = geometry.channel(0U);
    const auto rightColumn = geometry.channel(1U);
    canvas.text(leftColumn.x + leftColumn.width * 0.5f,
                bounds.y + bounds.height + 5.0f, "L", nullptr);
    canvas.text(rightColumn.x + rightColumn.width * 0.5f,
                bounds.y + bounds.height + 5.0f, "R", nullptr);
}

} // namespace sms::ui::dpf
