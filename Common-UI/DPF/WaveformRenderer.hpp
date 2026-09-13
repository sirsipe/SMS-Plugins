#pragma once

#include "Controls.hpp"
#include "WaveformEditor.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace sms::ui::dpf {

inline void drawWaveform(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                         const audio::WaveformSummary& summary, const bool hasWaveform,
                         const DGL_NAMESPACE::Color& signal)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);
    canvas.fillColor(colors.canvas);
    canvas.fill();
    canvas.beginPath();
    canvas.moveTo(bounds.x, bounds.y + bounds.height * 0.5f);
    canvas.lineTo(bounds.x + bounds.width, bounds.y + bounds.height * 0.5f);
    canvas.strokeColor(colors.outline);
    canvas.strokeWidth(colors.outlineWidth);
    canvas.stroke();

    if (!hasWaveform)
        return;
    const float scale = waveform::displayScale(summary);
    for (std::size_t index = 0; index < audio::kWaveformBins; ++index) {
        const float x = bounds.x + bounds.width * (static_cast<float>(index) + 0.5f) /
                                      static_cast<float>(audio::kWaveformBins);
        canvas.beginPath();
        canvas.moveTo(x, bounds.y + bounds.height *
            (0.5f - summary.maximum[index] * scale * 0.43f));
        canvas.lineTo(x, bounds.y + bounds.height *
            (0.5f - summary.minimum[index] * scale * 0.43f));
        canvas.strokeColor(signal);
        canvas.strokeWidth(1.2f);
        canvas.stroke();
    }
}

inline void drawCutHandle(DGL_NAMESPACE::NanoVG& canvas, const float x, const float y,
                          const float height, const char* const label,
                          const bool hovered = false)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.beginPath();
    canvas.moveTo(x, y);
    canvas.lineTo(x, y + height);
    canvas.strokeColor(colors.selection);
    canvas.strokeWidth(hovered ? 4.0f : 2.0f);
    canvas.stroke();
    canvas.beginPath();
    canvas.roundedRect(x - (hovered ? 18.0f : 16.0f), y + (hovered ? 5.0f : 7.0f),
                       hovered ? 36.0f : 32.0f, hovered ? 20.0f : 16.0f, 4.0f);
    canvas.fillColor(colors.selection);
    canvas.fill();
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(7.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    canvas.fillColor(colors.canvas);
    canvas.text(x, y + 15.0f, label, nullptr);
}

inline void drawWaveformEditor(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                               const audio::WaveformSummary& summary,
                               const bool hasWaveform,
                               const dsp::SamplePlaybackSettings& settings,
                               const waveform::EditTarget hovered = waveform::EditTarget::none)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 8.0f);
    canvas.fillColor(colors.canvas);
    canvas.fill();
    canvas.strokeColor(colors.outline);
    canvas.stroke();
    canvas.beginPath();
    canvas.moveTo(bounds.x, bounds.y + bounds.height * 0.5f);
    canvas.lineTo(bounds.x + bounds.width, bounds.y + bounds.height * 0.5f);
    canvas.strokeColor(colors.outline.withAlpha(0.7f));
    canvas.strokeWidth(colors.outlineWidth);
    canvas.stroke();

    if (hasWaveform) {
        const float scale = waveform::displayScale(summary);
        for (std::size_t bin = 0; bin < audio::kWaveformBins; ++bin) {
            const float normalized = (static_cast<float>(bin) + 0.5f) /
                                     static_cast<float>(audio::kWaveformBins);
            const float x = bounds.x + normalized * bounds.width;
            canvas.beginPath();
            canvas.moveTo(x, bounds.y + bounds.height *
                (0.5f - summary.maximum[bin] * scale * 0.44f));
            canvas.lineTo(x, bounds.y + bounds.height *
                (0.5f - summary.minimum[bin] * scale * 0.44f));
            canvas.strokeColor(colors.contentSecondary.withAlpha(0.24f));
            canvas.strokeWidth(1.4f);
            canvas.stroke();
        }
        for (std::size_t bin = 0; bin < audio::kWaveformBins; ++bin) {
            const float normalized = (static_cast<float>(bin) + 0.5f) /
                                     static_cast<float>(audio::kWaveformBins);
            if (normalized < settings.start || normalized > settings.end)
                continue;
            const float gain = waveform::envelopeGain(normalized, summary, settings);
            const float x = bounds.x + normalized * bounds.width;
            canvas.beginPath();
            canvas.moveTo(x, bounds.y + bounds.height *
                (0.5f - summary.maximum[bin] * scale * gain * 0.44f));
            canvas.lineTo(x, bounds.y + bounds.height *
                (0.5f - summary.minimum[bin] * scale * gain * 0.44f));
            canvas.strokeColor(colors.activityPlayback);
            canvas.strokeWidth(2.3f);
            canvas.stroke();
        }
        char scaleLabel[32];
        if (scale > 1.05f)
            std::snprintf(scaleLabel, sizeof(scaleLabel), "DISPLAY  x%.1f", scale);
        else
            std::snprintf(scaleLabel, sizeof(scaleLabel), "DISPLAY  1:1");
        canvas.fontSize(9.0f);
        canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                         DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas.fillColor(colors.contentSecondary.withAlpha(0.8f));
        canvas.text(bounds.x + bounds.width - 10.0f, bounds.y + 30.0f, scaleLabel, nullptr);
    } else {
        canvas.fontSize(14.0f);
        canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                         DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas.fillColor(colors.contentSecondary);
        canvas.text(bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f,
                    "EMPTY PAD", nullptr);
    }

    const float startX = bounds.x + bounds.width * settings.start;
    const float endX = bounds.x + bounds.width * settings.end;
    canvas.beginPath();
    canvas.rect(bounds.x, bounds.y, std::max(0.0f, startX - bounds.x), bounds.height);
    canvas.rect(endX, bounds.y,
                std::max(0.0f, bounds.x + bounds.width - endX), bounds.height);
    canvas.fillColor(colors.canvas.withAlpha(0.64f));
    canvas.fill();
    drawCutHandle(canvas, startX, bounds.y, bounds.height, "START",
                  hovered == waveform::EditTarget::regionStart);
    drawCutHandle(canvas, endX, bounds.y, bounds.height, "END",
                  hovered == waveform::EditTarget::regionEnd);
}

inline void drawEnvelopeGraph(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                              const audio::WaveformSummary& summary,
                              const dsp::SamplePlaybackSettings& settings,
                              const waveform::EditTarget hovered = waveform::EditTarget::none)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    canvas.beginPath();
    canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 7.0f);
    canvas.fillColor(colors.canvas);
    canvas.fill();
    canvas.strokeColor(colors.outline);
    canvas.stroke();

    const auto graph = waveform::envelopeGeometry(bounds, summary, settings);
    const float attackWidth = graph.attackX - graph.left;
    const float decayWidth = graph.decayX - graph.attackX;
    const float sustainWidth = graph.releaseX - graph.decayX;
    const float releaseWidth = graph.right - graph.releaseX;
    canvas.beginPath();
    canvas.moveTo(graph.left, graph.sustainY);
    canvas.lineTo(graph.right, graph.sustainY);
    canvas.strokeColor(colors.outline.withAlpha(0.45f));
    canvas.strokeWidth(colors.outlineWidth);
    canvas.stroke();

    const auto drawCurve = [&canvas, &graph, attackWidth, decayWidth, releaseWidth]() {
        canvas.moveTo(graph.left, graph.bottom);
        canvas.bezierTo(graph.left + attackWidth * 0.55f, graph.bottom,
                        graph.attackX - attackWidth * 0.12f,
                        graph.top + (graph.bottom - graph.top) * 0.18f,
                        graph.attackX, graph.top);
        canvas.bezierTo(graph.attackX + decayWidth * 0.18f,
                        graph.top + (graph.sustainY - graph.top) * 0.62f,
                        graph.decayX - decayWidth * 0.25f, graph.sustainY,
                        graph.decayX, graph.sustainY);
        canvas.lineTo(graph.releaseX, graph.sustainY);
        canvas.bezierTo(graph.releaseX + releaseWidth * 0.18f,
                        graph.sustainY + (graph.bottom - graph.sustainY) * 0.62f,
                        graph.right - releaseWidth * 0.25f, graph.bottom,
                        graph.right, graph.bottom);
    };
    canvas.beginPath();
    drawCurve();
    canvas.closePath();
    canvas.fillColor(colors.activityPlayback.withAlpha(0.10f));
    canvas.fill();
    canvas.beginPath();
    drawCurve();
    canvas.strokeColor(colors.activityPlayback);
    canvas.strokeWidth(2.0f);
    canvas.stroke();

    const std::array<ui::Point, 3> anchors{{
        {graph.attackX, graph.top}, {graph.decayX, graph.sustainY},
        {graph.releaseX, graph.sustainY}}};
    const std::array<ui::Point, 3> handles{{
        {graph.attackHandleX, graph.top}, {graph.decayHandleX, graph.sustainY},
        {graph.releaseHandleX, graph.sustainY}}};
    for (std::size_t index = 0; index < handles.size(); ++index) {
        if (std::abs(handles[index].x - anchors[index].x) < 0.5f)
            continue;
        canvas.beginPath();
        canvas.moveTo(anchors[index].x, anchors[index].y);
        canvas.lineTo(handles[index].x, handles[index].y);
        canvas.strokeColor(colors.contentSecondary.withAlpha(0.45f));
        canvas.strokeWidth(colors.outlineWidth);
        canvas.stroke();
    }
    for (std::size_t index = 0; index < handles.size(); ++index) {
        const bool isHovered = static_cast<int>(hovered) ==
            static_cast<int>(waveform::EditTarget::attackNode) + static_cast<int>(index);
        canvas.beginPath();
        canvas.circle(handles[index].x, handles[index].y, isHovered ? 8.0f : 5.0f);
        canvas.fillColor(index == 0U ? colors.controlAccent : colors.activityPlayback);
        canvas.fill();
        canvas.strokeColor(isHovered ? colors.contentPrimary : colors.canvas);
        canvas.strokeWidth(isHovered ? 2.0f : 1.5f);
        canvas.stroke();
    }

    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(7.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                     DGL_NAMESPACE::NanoVG::ALIGN_BOTTOM);
    canvas.fillColor(colors.contentSecondary.withAlpha(0.8f));
    canvas.text(graph.left + attackWidth * 0.5f, bounds.y + bounds.height - 2.0f, "A", nullptr);
    canvas.text(graph.attackX + decayWidth * 0.5f, bounds.y + bounds.height - 2.0f, "D", nullptr);
    canvas.text(graph.decayX + sustainWidth * 0.5f, bounds.y + bounds.height - 2.0f, "S", nullptr);
    canvas.text(graph.releaseX + releaseWidth * 0.5f, bounds.y + bounds.height - 2.0f, "R", nullptr);
    char duration[24];
    std::snprintf(duration, sizeof(duration), "%.2f s", graph.durationSeconds);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                     DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    canvas.fillColor(colors.contentSecondary.withAlpha(0.65f));
    canvas.text(graph.right, graph.top + 2.0f, duration, nullptr);
}

inline void drawEnvelopeSlider(DGL_NAMESPACE::NanoVG& canvas, const ui::Rect bounds,
                               const char* const label, const float value,
                               const bool sustain, const bool hovered = false)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    const float normalized = sustain ? value : waveform::envelopeTimeToNormalized(value);
    canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    canvas.fontSize(9.0f);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                     DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    canvas.fillColor(colors.contentSecondary);
    canvas.text(bounds.x, bounds.y, label, nullptr);
    char display[24];
    if (sustain)
        std::snprintf(display, sizeof(display), "%d%%",
                      static_cast<int>(std::lround(value * 100.0f)));
    else if (value < 1.0f)
        std::snprintf(display, sizeof(display), "%d ms",
                      static_cast<int>(std::lround(value * 1000.0f)));
    else
        std::snprintf(display, sizeof(display), "%.2f s", value);
    canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                     DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    canvas.fillColor(colors.contentPrimary);
    canvas.text(bounds.x + bounds.width, bounds.y, display, nullptr);
    const ui::Rect track = waveform::envelopeSliderTrack(bounds);
    drawSlider(canvas, track.x, track.y, track.width, normalized,
               sustain ? colors.activityPlayback : colors.controlAccent, hovered);
}

} // namespace sms::ui::dpf
