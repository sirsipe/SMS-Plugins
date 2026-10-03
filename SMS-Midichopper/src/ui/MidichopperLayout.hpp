#pragma once

#include "UI/Geometry.hpp"

namespace midichopper::ui::layout {

inline constexpr unsigned int canvasWidth = 1344U;
inline constexpr unsigned int canvasHeight = 756U;
inline constexpr unsigned int minimumWidth = 960U;
inline constexpr unsigned int minimumHeight = 540U;
inline constexpr float contentOffsetX = 40.0f;
inline constexpr sms::ui::Rect contentBounds{
    0.0f, 0.0f, static_cast<float>(canvasWidth) - contentOffsetX * 2.0f,
    static_cast<float>(canvasHeight)};

inline constexpr sms::ui::Rect inputMeter{8.0f, 96.0f, 24.0f, 590.0f};
inline constexpr sms::ui::Rect outputMeter{1312.0f, 96.0f, 24.0f, 590.0f};

inline constexpr sms::ui::Rect mainPanel{24.0f, 96.0f, 934.0f, 596.0f};
inline constexpr sms::ui::Rect sidePanel{974.0f, 96.0f, 266.0f, 596.0f};
inline constexpr sms::ui::Rect menuButton{1208.0f, 25.0f, 32.0f, 32.0f};
inline constexpr sms::ui::Rect menuPanel{1048.0f, 68.0f, 192.0f, 228.0f};
inline constexpr sms::ui::Rect footer{24.0f, 706.0f, 1216.0f, 32.0f};
inline constexpr sms::ui::Rect mainPadBounds{46.0f, 196.0f, 890.0f, 470.0f};
inline constexpr sms::ui::Rect overviewWaveform{46.0f, 176.0f, 890.0f, 10.0f};
inline constexpr sms::ui::Rect editorWaveform{46.0f, 158.0f, 890.0f, 222.0f};
inline constexpr sms::ui::Rect editorZoom{29.0f, 158.0f, 12.0f, 222.0f};
inline constexpr sms::ui::Rect editorScroll{46.0f, 382.0f, 890.0f, 10.0f};
inline constexpr sms::ui::Rect envelopeGraph{46.0f, 448.0f, 370.0f, 104.0f};
inline constexpr sms::ui::Rect editorPadBounds{994.0f, 204.0f, 222.0f, 406.0f};
inline constexpr sms::ui::Rect editorPlayStop{994.0f, 628.0f, 106.0f, 34.0f};
inline constexpr sms::ui::Rect playOnSelect{1106.0f, 628.0f, 110.0f, 34.0f};
inline constexpr sms::ui::Rect openEditor{994.0f, 108.0f, 222.0f, 28.0f};
inline constexpr sms::ui::Rect closeEditor{994.0f, 112.0f, 222.0f, 32.0f};
inline constexpr sms::ui::Rect chopWaveform{46.0f, 170.0f, 890.0f, 250.0f};
inline constexpr sms::ui::Rect chopZoom{29.0f, 170.0f, 12.0f, 250.0f};
inline constexpr sms::ui::Rect chopScroll{46.0f, 424.0f, 890.0f, 10.0f};
inline constexpr sms::ui::Rect chopApply{994.0f, 480.0f, 222.0f, 38.0f};
inline constexpr sms::ui::Rect chopPrevious{994.0f, 542.0f, 64.0f, 52.0f};
inline constexpr sms::ui::Rect chopExit{1074.0f, 534.0f, 64.0f, 64.0f};
inline constexpr sms::ui::Rect chopNext{1152.0f, 542.0f, 64.0f, 52.0f};

[[nodiscard]] constexpr bool chopExitContains(const sms::ui::Point point) noexcept
{
    const float centerX = chopExit.x + chopExit.width * 0.5f;
    const float centerY = chopExit.y + chopExit.height * 0.5f;
    const float dx = point.x - centerX;
    const float dy = point.y - centerY;
    const float radius = chopExit.width * 0.5f;
    return dx * dx + dy * dy <= radius * radius;
}

[[nodiscard]] constexpr float chopArrowTailX(const sms::ui::Rect bounds,
                                             const bool pointsRight) noexcept
{
    return bounds.x + bounds.width * 0.5f + (pointsRight ? -9.0f : 9.0f);
}

[[nodiscard]] constexpr float chopArrowTipX(const sms::ui::Rect bounds,
                                            const bool pointsRight) noexcept
{
    return bounds.x + bounds.width * 0.5f + (pointsRight ? 3.0f : -3.0f);
}
inline constexpr sms::ui::Rect playMode{994.0f, 145.0f, 108.0f, 42.0f};
inline constexpr sms::ui::Rect armMode{1108.0f, 145.0f, 108.0f, 42.0f};
inline constexpr sms::ui::Rect sequentialMode{994.0f, 220.0f, 106.0f, 38.0f};
inline constexpr sms::ui::Rect fixedMode{1108.0f, 220.0f, 108.0f, 38.0f};
inline constexpr sms::ui::Rect fixedLength{994.0f, 298.0f, 222.0f, 36.0f};
inline constexpr sms::ui::Rect oneShotMode{994.0f, 220.0f, 106.0f, 38.0f};
inline constexpr sms::ui::Rect gatedMode{1108.0f, 220.0f, 108.0f, 38.0f};
inline constexpr sms::ui::Rect voiceLimit{994.0f, 298.0f, 222.0f, 28.0f};
inline constexpr sms::ui::Rect mainPlayStop{1049.0f, 342.0f, 112.0f, 32.0f};

[[nodiscard]] constexpr sms::ui::Rect preRoll(const bool fixedCapture) noexcept
{
    return {994.0f, fixedCapture ? 352.0f : 298.0f, 222.0f, 28.0f};
}

inline constexpr sms::ui::Rect monitor{994.0f, 651.0f, 222.0f, 34.0f};
inline constexpr float globalMixerLabelY = 386.0f;
inline constexpr sms::ui::Rect padMixerSeparator{354.0f, 586.0f, 1.0f, 77.0f};
inline constexpr sms::ui::Rect finalizeAction{994.0f, 610.0f, 70.0f, 34.0f};
inline constexpr sms::ui::Rect undoAction{1070.0f, 610.0f, 70.0f, 34.0f};
inline constexpr sms::ui::Rect clearAction{1146.0f, 610.0f, 70.0f, 34.0f};

[[nodiscard]] constexpr sms::ui::Rect menuOption(const int index) noexcept
{
    return {1060.0f, 100.0f + static_cast<float>(index) * 31.0f, 168.0f, 27.0f};
}

[[nodiscard]] constexpr sms::ui::Rect midiBankModeOption(const int index) noexcept
{
    return {1060.0f, 226.0f + static_cast<float>(index) * 31.0f, 168.0f, 27.0f};
}

[[nodiscard]] constexpr sms::ui::Rect mainBank(const int index) noexcept
{
    return {80.0f + static_cast<float>(index) * 56.0f, 140.0f, 50.0f, 28.0f};
}

[[nodiscard]] constexpr sms::ui::Rect editorBank(const int index) noexcept
{
    return {994.0f + static_cast<float>(index) * 57.0f, 166.0f, 51.0f, 26.0f};
}

[[nodiscard]] constexpr sms::ui::Rect editorSlider(const int index) noexcept
{
    return {440.0f, 438.0f + static_cast<float>(index) * 29.0f, 472.0f, 24.0f};
}

[[nodiscard]] constexpr sms::ui::Rect mixerKnob(const int index) noexcept
{
    switch (index) {
    case 0: return {90.0f, 590.0f, 80.0f, 72.0f}; // Gain
    case 1: return {215.0f, 590.0f, 80.0f, 72.0f}; // Pan
    case 6: return {380.0f, 590.0f, 80.0f, 72.0f}; // Dirty
    case 2: return {500.0f, 590.0f, 80.0f, 72.0f}; // Tune
    case 3: return {630.0f, 590.0f, 80.0f, 72.0f}; // Lowpass
    case 4: return {760.0f, 590.0f, 80.0f, 72.0f}; // Highpass
    case 5: return {636.0f, 663.0f, 198.0f, 28.0f}; // Slope hit area
    default: return {};
    }
}

[[nodiscard]] constexpr sms::ui::Rect mixerValueLabel(const int index) noexcept
{
    const auto knob = mixerKnob(index);
    return {knob.x, knob.y + 52.0f, knob.width, 18.0f};
}

[[nodiscard]] constexpr sms::ui::Rect globalMixerKnob(const int index) noexcept
{
    switch (index) {
    case 0: return {994.0f, 408.0f, 68.0f, 72.0f}; // Volume
    case 1: return {1068.0f, 408.0f, 68.0f, 72.0f}; // Pan
    case 6: return {1142.0f, 408.0f, 68.0f, 72.0f}; // Dirty
    case 2: return {994.0f, 494.0f, 68.0f, 72.0f}; // Tune
    case 3: return {1068.0f, 494.0f, 68.0f, 72.0f}; // Lowpass
    case 4: return {1142.0f, 494.0f, 68.0f, 72.0f}; // Highpass
    case 5: return {1068.0f, 570.0f, 142.0f, 28.0f}; // Slope hit area
    default: return {};
    }
}

[[nodiscard]] constexpr sms::ui::Rect globalMixerValueLabel(const int index) noexcept
{
    const auto knob = globalMixerKnob(index);
    return {knob.x, knob.y + 52.0f, knob.width, 18.0f};
}

[[nodiscard]] constexpr sms::ui::Rect slopeTrack(const sms::ui::Rect bounds) noexcept
{
    return {bounds.x + 10.0f, bounds.y + 14.0f, bounds.width - 20.0f, 6.0f};
}

[[nodiscard]] constexpr sms::ui::Rect chopPadButton(const int index) noexcept
{
    return {46.0f + static_cast<float>(index) * 300.0f, 450.0f, 290.0f, 82.0f};
}

} // namespace midichopper::ui::layout
