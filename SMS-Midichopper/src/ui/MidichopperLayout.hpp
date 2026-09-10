#pragma once

#include "UI/Geometry.hpp"

namespace midichopper::ui::layout {

inline constexpr unsigned int canvasWidth = 1040U;
inline constexpr unsigned int canvasHeight = 680U;
inline constexpr unsigned int minimumWidth = 765U;
inline constexpr unsigned int minimumHeight = 500U;
inline constexpr float contentOffsetX = 40.0f;

inline constexpr sms::ui::Rect inputMeter{8.0f, 96.0f, 24.0f, 510.0f};
inline constexpr sms::ui::Rect outputMeter{1008.0f, 96.0f, 24.0f, 510.0f};

inline constexpr sms::ui::Rect mainPanel{24.0f, 96.0f, 630.0f, 516.0f};
inline constexpr sms::ui::Rect sidePanel{670.0f, 96.0f, 266.0f, 516.0f};
inline constexpr sms::ui::Rect menuButton{904.0f, 25.0f, 32.0f, 32.0f};
inline constexpr sms::ui::Rect menuPanel{744.0f, 68.0f, 192.0f, 228.0f};
inline constexpr sms::ui::Rect mainPadBounds{46.0f, 196.0f, 586.0f, 390.0f};
inline constexpr sms::ui::Rect overviewWaveform{46.0f, 176.0f, 586.0f, 10.0f};
inline constexpr sms::ui::Rect editorWaveform{46.0f, 158.0f, 586.0f, 222.0f};
inline constexpr sms::ui::Rect envelopeGraph{46.0f, 448.0f, 250.0f, 120.0f};
inline constexpr sms::ui::Rect editorPadBounds{690.0f, 204.0f, 222.0f, 326.0f};
inline constexpr sms::ui::Rect openEditor{690.0f, 108.0f, 222.0f, 28.0f};
inline constexpr sms::ui::Rect closeEditor{690.0f, 112.0f, 222.0f, 32.0f};
inline constexpr sms::ui::Rect playMode{690.0f, 145.0f, 108.0f, 42.0f};
inline constexpr sms::ui::Rect armMode{804.0f, 145.0f, 108.0f, 42.0f};
inline constexpr sms::ui::Rect sequentialMode{690.0f, 220.0f, 106.0f, 38.0f};
inline constexpr sms::ui::Rect fixedMode{804.0f, 220.0f, 108.0f, 38.0f};
inline constexpr sms::ui::Rect fixedLength{690.0f, 298.0f, 222.0f, 36.0f};
inline constexpr sms::ui::Rect oneShotMode{690.0f, 352.0f, 106.0f, 36.0f};
inline constexpr sms::ui::Rect gatedMode{804.0f, 352.0f, 108.0f, 36.0f};
inline constexpr sms::ui::Rect voiceLimit{690.0f, 410.0f, 222.0f, 28.0f};
inline constexpr sms::ui::Rect preRoll{690.0f, 448.0f, 222.0f, 28.0f};
inline constexpr sms::ui::Rect monitor{690.0f, 480.0f, 222.0f, 34.0f};
inline constexpr sms::ui::Rect finalizeAction{690.0f, 540.0f, 70.0f, 34.0f};
inline constexpr sms::ui::Rect undoAction{766.0f, 540.0f, 70.0f, 34.0f};
inline constexpr sms::ui::Rect clearAction{842.0f, 540.0f, 70.0f, 34.0f};

[[nodiscard]] constexpr sms::ui::Rect menuOption(const int index) noexcept
{
    return {756.0f, 100.0f + static_cast<float>(index) * 31.0f, 168.0f, 27.0f};
}

[[nodiscard]] constexpr sms::ui::Rect midiBankModeOption(const int index) noexcept
{
    return {756.0f, 226.0f + static_cast<float>(index) * 31.0f, 168.0f, 27.0f};
}

[[nodiscard]] constexpr sms::ui::Rect mainBank(const int index) noexcept
{
    return {80.0f + static_cast<float>(index) * 56.0f, 140.0f, 50.0f, 28.0f};
}

[[nodiscard]] constexpr sms::ui::Rect editorBank(const int index) noexcept
{
    return {690.0f + static_cast<float>(index) * 57.0f, 166.0f, 51.0f, 26.0f};
}

[[nodiscard]] constexpr sms::ui::Rect editorSlider(const int index) noexcept
{
    return {330.0f, 448.0f + static_cast<float>(index) * 32.0f, 280.0f, 24.0f};
}

} // namespace midichopper::ui::layout
