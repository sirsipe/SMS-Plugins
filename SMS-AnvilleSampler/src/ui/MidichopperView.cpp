#include "MidichopperView.hpp"

#include "Configuration.hpp"
#include "ChopEditor.hpp"
#include "DPF/AnalogMaterials.hpp"
#include "DPF/Controls.hpp"
#include "DPF/ContextMenu.hpp"
#include "DPF/LevelMeter.hpp"
#include "DPF/Theme.hpp"
#include "LogoGlow.hpp"
#include "DPF/WaveformRenderer.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "PadLayout.hpp"
#include "Parameters.hpp"
#include "SampleWaveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace midichopper::ui {
namespace {

namespace uiLayout = layout;

class Painter {
public:
    Painter(DGL_NAMESPACE::NanoVG& canvas, const ViewState& state)
        : canvas_(canvas), state_(state), pads_(state.layout) {}

    void draw()
    {
        sms::ui::dpf::drawChassis(canvas_, {0.0f, 0.0f,
            static_cast<float>(uiLayout::canvasWidth),
            static_cast<float>(uiLayout::canvasHeight)});
        if (state_.background != nullptr && state_.background->isValid()) {
            canvas_.beginPath();
            canvas_.roundedRect(0.0f, 0.0f, uiLayout::canvasWidth,
                                uiLayout::canvasHeight, 18.0f);
            canvas_.fillPaint(canvas_.imagePattern(0.0f, 0.0f,
                uiLayout::canvasWidth, uiLayout::canvasHeight,
                0.0f, *state_.background, sms::ui::dpf::theme().backgroundAlpha));
            canvas_.fill();
        }
        sms::ui::dpf::drawScrew(canvas_, 20.0f, 20.0f);
        sms::ui::dpf::drawScrew(canvas_, uiLayout::canvasWidth - 20.0f, 20.0f);
        sms::ui::dpf::drawScrew(canvas_, 20.0f, uiLayout::canvasHeight - 20.0f);
        sms::ui::dpf::drawScrew(canvas_, uiLayout::canvasWidth - 20.0f,
                               uiLayout::canvasHeight - 20.0f);
        drawLevelFader(0, uiLayout::inputMeter, state_.inputLevels, state_.monitorGainDb, "IN");
        drawLevelFader(1, uiLayout::outputMeter, state_.outputLevels, state_.outputGainDb, "OUT");

        canvas_.save();
        canvas_.translate(uiLayout::contentOffsetX, 0.0f);
        drawHeader();
        if (state_.chopEditorMode) {
            drawChopEditor();
            drawChopEditorPanel();
        } else if (state_.editorMode) {
            drawSampleEditor();
            drawEditorPadPanel();
        } else {
            drawPadPanel();
            drawControlPanel();
        }
        drawFooter();
        if (state_.menuOpen)
            drawMenuOverlay();
        if (state_.padContextMenuOpen) {
            sms::ui::dpf::drawContextMenu(canvas_, state_.padContextMenu,
                state_.padContextMenuItems,
                hovered(InteractiveType::padContextItem)
                    ? state_.hoveredTarget.index : -1);
            if (state_.padColorMenuOpen)
                sms::ui::dpf::drawContextMenu(canvas_, state_.padColorMenu,
                    state_.padColorMenuItems,
                    hovered(InteractiveType::padColorItem)
                        ? state_.hoveredTarget.index : -1);
        }
        canvas_.restore();
    }

private:
    void text(float x, float y, const char* label, const char* end)
    {
        canvas_.text(x, y, sms::ui::dpf::themeText(label), end);
    }

    void measureText(float x, float y, const char* label, const char* end,
                    DGL_NAMESPACE::Rectangle<float>& bounds)
    {
        canvas_.textBounds(x, y, sms::ui::dpf::themeText(label), end, bounds);
    }

    [[nodiscard]] bool hovered(const InteractiveType type, const int index = -1) const noexcept
    {
        return isTarget(state_.hoveredTarget, type, index);
    }

    void drawLevelFader(const int index, const sms::ui::Rect bounds,
                        const std::array<float, 2>& levels, const float gainDb,
                        const char* const label)
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawStereoLedMeter(canvas_, bounds, levels[0], levels[1], label, 18.0f);
        const auto track = uiLayout::levelFaderTrack(bounds);
        canvas_.beginPath();
        canvas_.roundedRect(track.x, bounds.y + 2.0f, track.width,
                            bounds.height - 4.0f, 3.0f);
        canvas_.fillColor(colors.recessEdge);
        canvas_.fill();
        const auto range = plugin::parameterRanges::outputGainDb;
        for (int tick = 0; tick <= 12; ++tick) {
            const float y = track.y + track.height * static_cast<float>(tick) / 12.0f;
            const float cx = bounds.x + bounds.width * 0.5f;
            canvas_.beginPath();
            canvas_.moveTo(cx - 9.0f, y);
            canvas_.lineTo(cx - 5.0f, y);
            canvas_.moveTo(cx + 5.0f, y);
            canvas_.lineTo(cx + 9.0f, y);
            canvas_.strokeColor(colors.contentSecondary.withAlpha(tick == 4 ? 0.95f : 0.45f));
            canvas_.strokeWidth(tick == 4 ? 1.5f : 1.0f);
            canvas_.stroke();
        }
        const float normalized = std::clamp((gainDb - range.minimum) /
            (range.maximum - range.minimum), 0.0f, 1.0f);
        const float capY = track.y + (1.0f - normalized) * track.height;
        sms::ui::dpf::drawFaderCap(canvas_,
            {bounds.x + bounds.width * 0.5f - 9.0f, capY - 17.0f, 18.0f, 34.0f},
            true, colors.controlAccent, hovered(InteractiveType::levelFader, index));
        const auto valueBounds = uiLayout::levelValueLabel(index);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        char value[24];
        std::snprintf(value, sizeof(value), sms::ui::dpf::themeFormat("%+.1f dB"), gainDb);
        const bool entering = isTarget(state_.mixerValueEntryTarget,
                                       InteractiveType::levelValueLabel, index);
        if (entering || hovered(InteractiveType::levelValueLabel, index)) {
            canvas_.beginPath();
            canvas_.roundedRect(valueBounds.x + uiLayout::contentOffsetX, valueBounds.y,
                                valueBounds.width, valueBounds.height, 2.0f);
            canvas_.fillColor(colors.controlAccent.withAlpha(0.18f));
            canvas_.fill();
        }
        canvas_.fillColor(entering ? colors.selection : colors.contentPrimary);
        text(bounds.x + bounds.width * 0.5f,
            valueBounds.y + valueBounds.height * 0.5f,
            entering ? state_.mixerValueEntryText : value, nullptr);
    }

    void drawViewportControls(const sms::ui::Rect zoom,
                              const sms::ui::Rect scroll)
    {
        const auto& colors = sms::ui::dpf::theme();
        const auto& viewport = state_.waveformViewport;
        const float alpha = viewport.total != 0U ? 1.0f : colors.disabledAlpha;
        canvas_.beginPath();
        canvas_.roundedRect(zoom.x, zoom.y, zoom.width, zoom.height, 4.0f);
        canvas_.roundedRect(scroll.x, scroll.y, scroll.width, scroll.height, 4.0f);
        canvas_.fillColor(colors.recess.withAlpha(alpha));
        canvas_.fill();
        canvas_.strokeColor(colors.outline.withAlpha(alpha));
        canvas_.strokeWidth(1.0f);
        canvas_.stroke();

        const float centerY = zoom.y + (1.0f - viewport.zoomPosition()) *
            (zoom.height - 12.0f) + 6.0f;
        canvas_.beginPath();
        canvas_.roundedRect(zoom.x + 6.0f, zoom.y + 3.0f,
                            4.0f, zoom.height - 6.0f, 2.0f);
        canvas_.fillColor(colors.recessEdge.withAlpha(alpha));
        canvas_.fill();
        for (int tick = 0; tick <= 10; ++tick) {
            const float tickY = zoom.y + 6.0f +
                (zoom.height - 12.0f) * static_cast<float>(tick) / 10.0f;
            canvas_.beginPath();
            canvas_.moveTo(zoom.x + 1.0f, tickY);
            canvas_.lineTo(zoom.x + 4.0f, tickY);
            canvas_.moveTo(zoom.x + 12.0f, tickY);
            canvas_.lineTo(zoom.x + 15.0f, tickY);
            canvas_.strokeColor(colors.contentSecondary.withAlpha(0.5f * alpha));
            canvas_.strokeWidth(1.0f);
            canvas_.stroke();
        }
        sms::ui::dpf::drawFaderCap(canvas_,
            {zoom.x + 1.0f, centerY - 6.0f, zoom.width - 2.0f, 12.0f}, true,
            colors.selection, hovered(InteractiveType::waveformZoom), viewport.total != 0U);

        const auto window = viewport.scrollThumb(scroll);
        sms::ui::dpf::drawFaderCap(canvas_,
            {window.x, window.y + 1.0f, window.width, window.height - 2.0f}, false,
            colors.selection, hovered(InteractiveType::waveformScroll), viewport.total != 0U);
    }

    void drawMixerKnob(const sms::ui::Rect bounds, const InteractiveType type,
                       const int index, const char* const label, const float value,
                       const float minimum, const float maximum, const char* const display)
    {
        const auto& colors = sms::ui::dpf::theme();
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(bounds.x + bounds.width * 0.5f, bounds.y, label, nullptr);
        const auto valueType = type == InteractiveType::mixerKnob
            ? InteractiveType::mixerValueLabel : InteractiveType::globalMixerValueLabel;
        const bool editing = isTarget(state_.mixerValueEntryTarget, valueType, index);
        const auto valueBounds = type == InteractiveType::mixerKnob
            ? uiLayout::mixerValueLabel(index) : uiLayout::globalMixerValueLabel(index);
        if (editing) {
            canvas_.beginPath();
            canvas_.roundedRect(valueBounds.x + 2.0f, valueBounds.y,
                                valueBounds.width - 4.0f, valueBounds.height, 3.0f);
            canvas_.fillColor(colors.recess);
            canvas_.fill();
            canvas_.strokeColor(colors.selection);
            canvas_.strokeWidth(1.0f);
            canvas_.stroke();
        }
        char entry[32];
        std::snprintf(entry, sizeof(entry), sms::ui::dpf::themeFormat("%s%s"),
                      editing ? state_.mixerValueEntryText : display,
                      editing ? "_" : "");
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentPrimary);
        text(bounds.x + bounds.width * 0.5f, bounds.y + 56.0f, entry, nullptr);
        const float normalized = bipolarKnobPosition(value, minimum, maximum);
        sms::ui::dpf::drawKnob(canvas_, bounds.x + bounds.width * 0.5f,
            bounds.y + 31.0f, 16.0f, normalized, colors.controlAccent,
            hovered(type, index));
    }

    void drawDirtySwitch(const sms::ui::Rect bounds, const InteractiveType type,
                         const bool enabled)
    {
        const auto& colors = sms::ui::dpf::theme();
        const float centerX = bounds.x + bounds.width * 0.5f;
        const float slotY = bounds.y + 20.0f;
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(centerX, bounds.y, "DIRTY", nullptr);
        canvas_.beginPath();
        canvas_.roundedRect(centerX - 23.0f, slotY, 46.0f, 23.0f, 11.0f);
        canvas_.fillPaint(canvas_.linearGradient(centerX, slotY, centerX,
            slotY + 23.0f, colors.shadow, colors.recess));
        canvas_.fill();
        canvas_.strokeColor(colors.recessEdge);
        canvas_.strokeWidth(1.5f);
        canvas_.stroke();
        const float leverX = centerX + (enabled ? 11.0f : -11.0f);
        canvas_.beginPath();
        canvas_.circle(leverX, slotY + 11.5f, 9.5f);
        canvas_.fillPaint(canvas_.radialGradient(leverX - 3.0f, slotY + 7.0f,
            1.0f, 14.0f, enabled ? colors.controlAccent.plus(28) : colors.edgeHighlight,
            enabled ? colors.controlAccent.minus(45) : colors.controlBottom));
        canvas_.fill();
        canvas_.strokeColor(hovered(type, 6) ? colors.controlAccent : colors.outline);
        canvas_.strokeWidth(1.3f);
        canvas_.stroke();
        canvas_.fillColor(enabled ? colors.controlAccent : colors.contentSecondary);
        text(centerX, bounds.y + 56.0f, enabled ? "ON" : "OFF", nullptr);
    }

    void drawToggleFrame(const sms::ui::Rect bounds, const InteractiveType type)
    {
        const auto& colors = sms::ui::dpf::theme();
        canvas_.beginPath();
        canvas_.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 5.0f);
        canvas_.fillPaint(canvas_.linearGradient(bounds.x, bounds.y, bounds.x,
            bounds.y + bounds.height, colors.shadow, colors.recess));
        canvas_.fill();
        canvas_.strokeColor(hovered(type)
            ? colors.controlAccent : colors.recessEdge);
        canvas_.strokeWidth(1.5f);
        canvas_.stroke();
    }

    void drawDirectionToggle(const sms::ui::Rect bounds, const InteractiveType type,
                             const bool reverse)
    {
        const auto& colors = sms::ui::dpf::theme();
        drawToggleFrame(bounds, type);
        const float centerY = bounds.y + bounds.height * 0.5f;
        const float direction = reverse ? -1.0f : 1.0f;
        const float arrowWidth = bounds.width * 0.11f;
        const float arrowHeight = bounds.height * 0.18f;
        canvas_.fillColor(reverse
            ? colors.controlAccent : colors.contentSecondary.withAlpha(0.5f));
        for (int arrow = 0; arrow < 2; ++arrow) {
            const float centerX = bounds.x + bounds.width * (arrow == 0 ? 0.37f : 0.63f);
            canvas_.beginPath();
            canvas_.moveTo(centerX + direction * arrowWidth, centerY);
            canvas_.lineTo(centerX - direction * arrowWidth, centerY - arrowHeight);
            canvas_.lineTo(centerX - direction * arrowWidth, centerY + arrowHeight);
            canvas_.closePath();
            canvas_.fill();
        }
    }

    void drawGlobalMixerToggle()
    {
        const auto& colors = sms::ui::dpf::theme();
        const auto bounds = uiLayout::padGlobalMixer;
        const bool enabled = state_.mixerSettings.globalMixer >= 0.5f;
        drawToggleFrame(bounds, InteractiveType::padGlobalMixer);
        const float centerX = bounds.x + bounds.width * 0.5f;
        const float centerY = bounds.y + bounds.height * 0.5f;
        canvas_.strokeColor(enabled
            ? colors.controlAccent : colors.contentSecondary.withAlpha(0.5f));
        canvas_.strokeWidth(2.4f);
        canvas_.beginPath();
        canvas_.arc(centerX, centerY, 9.0f, -0.7853982f, 3.9269908f,
                    DGL_NAMESPACE::NanoVG::CW);
        canvas_.stroke();
        canvas_.beginPath();
        canvas_.moveTo(centerX, centerY - 11.0f);
        canvas_.lineTo(centerX, centerY);
        canvas_.stroke();
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(centerX, uiLayout::mixerKnob(4).y - 1.0f, "GLOBAL MIXER", nullptr);
    }

    void drawSlopeSlider(const sms::ui::Rect bounds, const InteractiveType type,
                         const float slope)
    {
        const auto& colors = sms::ui::dpf::theme();
        const auto track = uiLayout::slopeTrack(bounds);
        const int index = std::clamp(static_cast<int>(std::lround(slope)), 0, 2);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(bounds.x + bounds.width * 0.5f, bounds.y + 2.0f, "SLOPE", nullptr);
        char value[20];
        std::snprintf(value, sizeof(value), sms::ui::dpf::themeFormat("%d dB"), index == 0 ? 6 : index == 1 ? 12 : 24);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentPrimary);
        text(track.x + track.width, bounds.y + 5.0f, value, nullptr);
        sms::ui::dpf::drawSlider(canvas_, track.x, track.y, track.width,
            static_cast<float>(index) * 0.5f, colors.controlAccent,
            hovered(type, 5));
        for (int tick = 0; tick < 3; ++tick) {
            const float tickX = track.x + track.width * static_cast<float>(tick) * 0.5f;
            canvas_.beginPath();
            canvas_.moveTo(tickX, track.y - 4.0f);
            canvas_.lineTo(tickX, track.y - 1.0f);
            canvas_.strokeColor(colors.contentSecondary.withAlpha(0.65f));
            canvas_.strokeWidth(1.0f);
            canvas_.stroke();
        }
    }

    void drawGlobalMixer()
    {
        if (state_.armed)
            return;
        const auto& colors = sms::ui::dpf::theme();
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(994.0f, uiLayout::globalMixerLabelY, "GLOBAL MIXER", nullptr);
        drawDirectionToggle(uiLayout::globalDirection, InteractiveType::globalDirection,
                            state_.globalReverse);
        char pan[24];
        char tune[24];
        char low[16];
        char high[16];
        if (std::abs(state_.globalPan) < 0.005f)
            std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%s"), sms::ui::dpf::themeText("CENTER"));
        else
            std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%c%d"), state_.globalPan < 0.0f ? 'L' : 'R',
                static_cast<int>(std::lround(std::abs(state_.globalPan) * 100.0f)));
        std::snprintf(tune, sizeof(tune), sms::ui::dpf::themeFormat("%+.2f st"), state_.globalTuneSemitones);
        std::snprintf(low, sizeof(low), sms::ui::dpf::themeFormat("%.0f%%"), state_.globalLowpass * 100.0f);
        std::snprintf(high, sizeof(high), sms::ui::dpf::themeFormat("%.0f%%"), state_.globalHighpass * 100.0f);
        drawMixerKnob(uiLayout::globalMixerKnob(1), InteractiveType::globalMixerKnob,
            1, "PAN", state_.globalPan, plugin::parameterRanges::globalPan.minimum,
            plugin::parameterRanges::globalPan.maximum, pan);
        drawDirtySwitch(uiLayout::globalMixerKnob(6), InteractiveType::globalMixerKnob,
            state_.globalDirty >= 0.5f);
        drawMixerKnob(uiLayout::globalMixerKnob(2), InteractiveType::globalMixerKnob,
            2, "TUNE", state_.globalTuneSemitones,
            plugin::parameterRanges::globalTuneSemitones.minimum,
            plugin::parameterRanges::globalTuneSemitones.maximum, tune);
        drawMixerKnob(uiLayout::globalMixerKnob(3), InteractiveType::globalMixerKnob,
            3, "LOWPASS", state_.globalLowpass, 0.0f, 1.0f, low);
        drawMixerKnob(uiLayout::globalMixerKnob(4), InteractiveType::globalMixerKnob,
            4, "HIGHPASS", state_.globalHighpass, 0.0f, 1.0f, high);
        drawSlopeSlider(uiLayout::globalMixerKnob(5), InteractiveType::globalMixerKnob,
            state_.globalFilterSlope);
    }

    [[nodiscard]] int globalPad(const int localPad) const noexcept
    {
        return state_.bank * static_cast<int>(bankStride(
            static_cast<std::uint8_t>(pads_.visiblePadCount()), midiBankMode())) + localPad;
    }

    [[nodiscard]] MidiBankMode midiBankMode() const noexcept
    {
        return state_.midiBankMode == 0
            ? MidiBankMode::SelectedBank : MidiBankMode::AllBanks;
    }

    [[nodiscard]] int bankForGlobalPad(const int pad) const noexcept
    {
        return static_cast<int>(bankForPad(
            static_cast<std::uint32_t>(std::max(pad, 0)),
            static_cast<std::uint8_t>(pads_.visiblePadCount()), midiBankMode()));
    }

    [[nodiscard]] int localPadForGlobalPad(const int pad) const noexcept
    {
        return static_cast<int>(localPadInBank(
            static_cast<std::uint32_t>(std::max(pad, 0)),
            static_cast<std::uint8_t>(pads_.visiblePadCount()), midiBankMode()));
    }

    [[nodiscard]] int mappedMidiNote(const int pad) const noexcept
    {
        return static_cast<int>(midiNoteForPad(
            static_cast<std::uint32_t>(pad), static_cast<std::uint8_t>(state_.baseMidiNote),
            midiBankMode()));
    }

    [[nodiscard]] sms::ui::PadGridLayout mainGrid() const noexcept
    {
        return pads_.grid(uiLayout::mainPadBounds, 10.0f);
    }

    [[nodiscard]] sms::ui::PadGridLayout editorGrid() const noexcept
    {
        return pads_.grid(uiLayout::editorPadBounds, 6.0f);
    }

    static void midiName(const int note, char* const destination,
                         const std::size_t destinationSize)
    {
        static constexpr const char* names[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        if (note < 0 || note > 127) {
            std::snprintf(destination, destinationSize, "--");
            return;
        }
        std::snprintf(destination, destinationSize, "%s%d", names[note % 12], note / 12 - 1);
    }

    void drawHeader()
    {
        const auto& colors = sms::ui::dpf::theme();
        DGL_NAMESPACE::Rectangle<float> textBounds;
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(26.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentPrimary);
        text(32.0f, 26.0f, "SMS-ANVILLE SAMPLER", nullptr);
        measureText(32.0f, 26.0f, "SMS-ANVILLE SAMPLER", nullptr, textBounds);
        float textRight = textBounds.getX() + textBounds.getWidth();
        canvas_.fontSize(12.0f);
        canvas_.fillColor(colors.contentSecondary);
        text(34.0f, 58.0f, "SEQUENTIAL CHOP  /  LIVE SAMPLE WORKSTATION", nullptr);
        measureText(34.0f, 58.0f,
                           "SEQUENTIAL CHOP  /  LIVE SAMPLE WORKSTATION", nullptr, textBounds);
        textRight = std::max(textRight, textBounds.getX() + textBounds.getWidth());
        canvas_.fontSize(10.0f);
        text(34.0f, 76.0f, "Version " ANVILLESAMPLER_VERSION_STRING, nullptr);
        measureText(34.0f, 76.0f,
                           "Version " ANVILLESAMPLER_VERSION_STRING, nullptr, textBounds);
        textRight = std::max(textRight, textBounds.getX() + textBounds.getWidth());

        if (state_.logo != nullptr && state_.logo->isValid()) {
            const float x = textRight + 14.0f;
            constexpr float y = 13.0f;
            constexpr float width = sms::ui::logoWidth;
            constexpr float height = sms::ui::logoHeight;
            const bool glow = state_.logoGlow != nullptr && state_.logoGlow->isValid();
            if (glow) {
                constexpr float padding = sms::ui::logoGlowPadding;
                constexpr float glowWidth = sms::ui::logoGlowWidth;
                constexpr float glowHeight = sms::ui::logoGlowHeight;
                auto paint = canvas_.imagePattern(x - padding, y - padding,
                    glowWidth, glowHeight, 0.0f, *state_.logoGlow, 1.0f);
                paint.innerColor = paint.outerColor = colors.logoGlow.withAlpha(
                    colors.logoGlow.alpha * state_.logoGlowOpacity);
                canvas_.beginPath();
                canvas_.rect(x - padding, y - padding, glowWidth, glowHeight);
                canvas_.fillPaint(paint);
                canvas_.fill();
            }
            canvas_.beginPath();
            canvas_.rect(x, y, width, height);
            canvas_.fillPaint(canvas_.imagePattern(
                x, y, width, height, 0.0f, *state_.logo, glow ? 1.0f : 0.48f));
            canvas_.fill();
        }

        const auto& modeColor = state_.armed ? colors.intentDanger : colors.meterGreen;
        sms::ui::dpf::drawLed(canvas_, 1146.0f, 43.0f, modeColor, true);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor(modeColor);
        text(1160.0f, 43.0f, state_.armed ? "ARMED" : "PLAY", nullptr);

        const bool menuHovered = hovered(InteractiveType::menuButton);
        sms::ui::dpf::drawRaisedControlSurface(canvas_, uiLayout::menuButton,
            colors.activityPlayback, {state_.menuOpen, menuHovered, false, true});
        for (int line = 0; line < 3; ++line) {
            canvas_.beginPath();
            canvas_.moveTo(1216.0f, 34.0f + static_cast<float>(line) * 8.0f);
            canvas_.lineTo(1232.0f, 34.0f + static_cast<float>(line) * 8.0f);
            canvas_.strokeColor(state_.menuOpen || menuHovered ? colors.activityPlayback
                                                : colors.contentSecondary);
            canvas_.strokeWidth(1.5f);
            canvas_.stroke();
        }
    }

    void drawMenuOverlay()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::menuPanelBounds(state_.skinsAvailable));
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(10.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        const auto submenuButton = [&](const sms::ui::Rect bounds, const char* label,
                                       const uiLayout::MenuSubmenu submenu,
                                       const InteractiveType type) {
            sms::ui::dpf::drawSegment(canvas_, bounds, label,
                state_.menuSubmenu == submenu, colors.selection, hovered(type));
            const float x = bounds.x + 13.0f;
            const float y = bounds.y + bounds.height * 0.5f;
            canvas_.beginPath();
            canvas_.moveTo(x + 2.0f, y - 4.0f);
            canvas_.lineTo(x - 2.0f, y);
            canvas_.lineTo(x + 2.0f, y + 4.0f);
            canvas_.strokeColor(colors.contentSecondary);
            canvas_.strokeWidth(1.3f);
            canvas_.stroke();
        };
        submenuButton(uiLayout::layoutMenuButton, "PAD LAYOUT",
            uiLayout::MenuSubmenu::padLayout, InteractiveType::menuPadLayouts);
        submenuButton(uiLayout::bankModeMenuButton, "BANK MODE",
            uiLayout::MenuSubmenu::bankMode, InteractiveType::menuBankModes);
        if (state_.skinsAvailable)
            submenuButton(uiLayout::skinMenuButton, "SKIN",
                uiLayout::MenuSubmenu::skin, InteractiveType::menuSkins);
        sms::ui::dpf::drawSegment(canvas_, uiLayout::resetMenuButton(state_.skinsAvailable),
            state_.resetArmed ? "CONFIRM: RESET" : "RESET", state_.resetArmed,
            colors.selection, hovered(InteractiveType::menuReset), state_.resetEnabled);
        text(1060.0f, state_.skinsAvailable ? 235.0f : 199.0f, "LINKS", nullptr);
        static constexpr const char* linkLabels[] = {"ONLINE HELP", "REPORT ISSUE"};
        for (int index = 0; index < 2; ++index)
            sms::ui::dpf::drawSegment(canvas_,
                uiLayout::menuLinkOption(index, state_.skinsAvailable),
                linkLabels[index], false, colors.selection, hovered(InteractiveType::menuLink, index));
        if (state_.menuSubmenu == uiLayout::MenuSubmenu::padLayout) {
            const sms::ui::ContextMenuItemView items[] = {
                {"16 PADS  ·  4×4", state_.currentPad < 0, false,
                    sms::ui::ContextMenuItemKind::action, state_.layout == 0},
                {"12 PADS  ·  3×4", state_.currentPad < 0, false,
                    sms::ui::ContextMenuItemKind::action, state_.layout == 1},
                {"8 PADS   ·  4×2", state_.currentPad < 0, false,
                    sms::ui::ContextMenuItemKind::action, state_.layout == 2},
            };
            sms::ui::dpf::drawContextMenu(canvas_, uiLayout::layoutMenuGeometry(), items,
                hovered(InteractiveType::menuLayout) ? state_.hoveredTarget.index : -1);
        } else if (state_.menuSubmenu == uiLayout::MenuSubmenu::bankMode) {
            const sms::ui::ContextMenuItemView items[] = {
                {"SELECTED  ·  SHARED", true, false,
                    sms::ui::ContextMenuItemKind::action, state_.midiBankMode == 0},
                {"ALL BANKS  ·  UNIQUE", true, false,
                    sms::ui::ContextMenuItemKind::action, state_.midiBankMode == 1},
            };
            sms::ui::dpf::drawContextMenu(canvas_, uiLayout::bankModeMenuGeometry(), items,
                hovered(InteractiveType::menuMidiBankMode) ? state_.hoveredTarget.index : -1);
        } else if (state_.menuSubmenu == uiLayout::MenuSubmenu::skin) {
            sms::ui::dpf::drawContextMenu(canvas_, state_.skinMenu, state_.skinMenuItems,
                hovered(InteractiveType::skinItem) ? state_.hoveredTarget.index : -1);
            const int maximum = uiLayout::skinMenuMaxOffset(state_.skinMenuTotal);
            if (maximum > 0) {
                const auto bounds = state_.skinMenu.bounds();
                const float top = state_.skinMenu.item(1).y;
                const float height = bounds.y + bounds.height - 6.0f - top;
                const float thumb = std::max(10.0f, height *
                    static_cast<float>(state_.skinMenuItems.size() - 1U) /
                    static_cast<float>(state_.skinMenuTotal - 1));
                canvas_.beginPath();
                canvas_.roundedRect(bounds.x + bounds.width - 5.0f, top, 2.0f, height, 1.0f);
                canvas_.fillColor(colors.outline);
                canvas_.fill();
                canvas_.beginPath();
                canvas_.roundedRect(bounds.x + bounds.width - 5.0f,
                    top + (height - thumb) * static_cast<float>(state_.skinMenuOffset) /
                        static_cast<float>(maximum), 2.0f, thumb, 1.0f);
                canvas_.fillColor(colors.selection);
                canvas_.fill();
            }
        }
    }

    void drawPadPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        {
            const sms::ui::dpf::ScopedCanvasState panelState(canvas_);
            canvas_.globalAlpha(colors.padPanelAlpha);
            sms::ui::dpf::drawPanel(canvas_, uiLayout::mainPanel);
        }
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(46.0f, 120.0f, state_.armed ? "DESTINATION PADS" : "PAD BANK", nullptr);
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        text(936.0f, 120.0f,
                     state_.armed ? "CLICK A PAD TO SET START" : "CLICK TO PLAY", nullptr);
        canvas_.fontSize(10.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        text(46.0f, 154.0f, "BANK", nullptr);
        for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
            char label[4];
            std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("%c"), 'A' + bank);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::mainBank(bank), label,
                                      bank == state_.bank, colors.activityPlayback,
                                      hovered(InteractiveType::bank, bank),
                                      state_.currentPad < 0);
        }
        sms::ui::dpf::drawWaveform(
            canvas_, uiLayout::overviewWaveform, state_.waveform, state_.hasWaveform,
            state_.currentPad >= 0 ? colors.activityCapture : colors.activityPlayback);

        const auto grid = mainGrid();
        for (int localPad = 0; localPad < pads_.visiblePadCount(); ++localPad) {
            const int pad = globalPad(localPad);
            const auto cell = grid.cell(pads_.visualIndex(localPad));
            const bool occupied = state_.padState[static_cast<std::size_t>(localPad)] != '0' &&
                                  state_.padState[static_cast<std::size_t>(localPad)] != '.';
            const bool active = state_.padActivity[static_cast<std::size_t>(localPad)] != '0';
            const bool recording = (state_.armed && active) || state_.currentPad == pad;
            const bool playing = (!state_.armed && active) || state_.pressedPad == localPad;
            const bool selected = state_.selectedPad == pad;
            const bool padHovered = hovered(InteractiveType::pad, localPad);
            const int color = state_.padColors[static_cast<std::size_t>(pad)];
            const auto* tint = color > 0 &&
                color <= static_cast<int>(colors.padColors.size())
                ? &colors.padColors[static_cast<std::size_t>(color - 1)].tint : nullptr;
            sms::ui::dpf::drawRubberPad(canvas_, cell,
                {occupied, playing, recording, selected, padHovered,
                 state_.pressedPad == localPad, true, tint});

            char padNumber[12];
            std::snprintf(padNumber, sizeof(padNumber), sms::ui::dpf::themeFormat("%02d"), localPad + 1);
            canvas_.fontSize(12.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(selected || padHovered ? colors.selection : colors.contentSecondary);
            text(cell.x + 12.0f, cell.y + 10.0f, padNumber, nullptr);
            char note[16];
            midiName(mappedMidiNote(pad), note, sizeof(note));
            canvas_.fontSize(20.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(occupied || recording ? colors.contentPrimary
                                                     : colors.contentSecondary);
            text(cell.x + cell.width * 0.5f, cell.y + cell.height * 0.54f,
                         note, nullptr);
            canvas_.fontSize(10.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_BOTTOM);
            canvas_.fillColor(recording ? colors.activityCapture :
                              (playing ? colors.activityPlayback : colors.contentSecondary));
            text(cell.x + cell.width - 10.0f, cell.y + cell.height - 9.0f,
                         recording ? "RECORDING" :
                         (playing ? "PLAYING" : (occupied ? "READY" : "EMPTY")), nullptr);
        }
    }

    void drawSampleEditor()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::mainPanel);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        char heading[80];
        std::snprintf(heading, sizeof(heading), sms::ui::dpf::themeFormat("SAMPLE EDITOR  /  BANK %c  /  PAD %02d"),
                      'A' + bankForGlobalPad(state_.selectedPad),
                      localPadForGlobalPad(state_.selectedPad) + 1);
        text(46.0f, 120.0f, heading, nullptr);
        const bool reverse = sampleDisplayReversed(state_.mixerSettings.reverse >= 0.5f,
            sampleGlobalReverse(state_.globalReverse, state_.mixerSettings.globalMixer));
        const auto displayWaveform = sampleDisplaySummary(state_.waveform, reverse);
        const sms::ui::waveform::CachedDetail displayDetail{
            sampleVisibleSummary(state_.waveform, state_.waveformViewport,
                state_.waveformDetail, reverse),
            state_.waveformViewport.start, state_.waveformViewport.end};
        sms::ui::dpf::drawWaveformEditor(canvas_, uiLayout::editorWaveform,
            displayWaveform, state_.hasWaveform, state_.editorSettings,
            hovered(InteractiveType::regionHandle)
                ? static_cast<sms::ui::waveform::EditTarget>(state_.hoveredTarget.index)
                : sms::ui::waveform::EditTarget::none,
            &displayDetail, state_.waveformViewport);
        drawViewportControls(uiLayout::editorZoom, uiLayout::editorScroll);

        const int playbackPad = state_.playbackPosition > 0.0f
            ? static_cast<int>(std::floor(state_.playbackPosition)) - 1 : -1;
        if (playbackPad == state_.selectedPad) {
            const float fraction = state_.playbackPosition -
                                   std::floor(state_.playbackPosition);
            const float x = uiLayout::editorWaveform.x +
                uiLayout::editorWaveform.width * static_cast<float>(
                    state_.waveformViewport.zoomed()
                        ? state_.waveformViewport.fraction(
                            fraction * state_.waveformViewport.total) : fraction);
            if (x >= uiLayout::editorWaveform.x &&
                x <= uiLayout::editorWaveform.x + uiLayout::editorWaveform.width) {
                canvas_.beginPath();
                canvas_.moveTo(x, uiLayout::editorWaveform.y + 4.0f);
                canvas_.lineTo(x, uiLayout::editorWaveform.y +
                                uiLayout::editorWaveform.height - 4.0f);
                canvas_.strokeColor(colors.shadow.withAlpha(0.8f));
                canvas_.strokeWidth(4.0f);
                canvas_.stroke();
                canvas_.beginPath();
                canvas_.moveTo(x, uiLayout::editorWaveform.y + 4.0f);
                canvas_.lineTo(x, uiLayout::editorWaveform.y +
                                uiLayout::editorWaveform.height - 4.0f);
                canvas_.strokeColor(colors.selection);
                canvas_.strokeWidth(2.0f);
                canvas_.stroke();
                canvas_.beginPath();
                canvas_.moveTo(x - 5.0f, uiLayout::editorWaveform.y + 4.0f);
                canvas_.lineTo(x + 5.0f, uiLayout::editorWaveform.y + 4.0f);
                canvas_.lineTo(x, uiLayout::editorWaveform.y + 11.0f);
                canvas_.closePath();
                canvas_.fillColor(colors.selection);
                canvas_.fill();
            }
        }

        char region[112];
        const auto startFrame = static_cast<std::uint32_t>(
            state_.editorSettings.start * state_.waveform.frames);
        const auto endFrame = static_cast<std::uint32_t>(
            state_.editorSettings.end * state_.waveform.frames);
        std::snprintf(region, sizeof(region),
                      sms::ui::dpf::themeFormat("REGION  %u — %u frames     %.1f%% of source"), startFrame, endFrame,
                      (state_.editorSettings.end - state_.editorSettings.start) * 100.0f);
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(46.0f, 402.0f, region, nullptr);
        text(46.0f, 426.0f, "AMPLITUDE ENVELOPE", nullptr);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary.withAlpha(0.8f));
        text(416.0f, 428.0f, "DRAG NODES", nullptr);
        sms::ui::dpf::drawEnvelopeGraph(canvas_, uiLayout::envelopeGraph,
            state_.waveform, state_.editorSettings,
            hovered(InteractiveType::envelopeNode)
                ? static_cast<sms::ui::waveform::EditTarget>(state_.hoveredTarget.index)
                : sms::ui::waveform::EditTarget::none);
        sms::ui::dpf::drawEnvelopeSlider(canvas_, uiLayout::editorSlider(0),
            "ATTACK", state_.editorSettings.attackSeconds, false,
            hovered(InteractiveType::envelopeSlider, 0));
        sms::ui::dpf::drawEnvelopeSlider(canvas_, uiLayout::editorSlider(1),
            "DECAY", state_.editorSettings.decaySeconds, false,
            hovered(InteractiveType::envelopeSlider, 1));
        sms::ui::dpf::drawEnvelopeSlider(canvas_, uiLayout::editorSlider(2),
            "SUSTAIN", state_.editorSettings.sustainLevel, true,
            hovered(InteractiveType::envelopeSlider, 2));
        sms::ui::dpf::drawEnvelopeSlider(canvas_, uiLayout::editorSlider(3),
            "RELEASE", state_.editorSettings.releaseSeconds, false,
            hovered(InteractiveType::envelopeSlider, 3));

        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(46.0f, 570.0f, "PAD MIXER", nullptr);
        drawDirectionToggle(uiLayout::padDirection, InteractiveType::padDirection,
                            state_.mixerSettings.reverse >= 0.5f);
        char gain[24];
        char pan[24];
        char tune[24];
        char low[16];
        char high[16];
        std::snprintf(gain, sizeof(gain), sms::ui::dpf::themeFormat("%+.1f dB"), state_.mixerSettings.gainDecibels);
        if (std::abs(state_.mixerSettings.pan) < 0.005f)
            std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%s"), sms::ui::dpf::themeText("CENTER"));
        else
            std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%c%d"),
                state_.mixerSettings.pan < 0.0f ? 'L' : 'R',
                static_cast<int>(std::lround(std::abs(state_.mixerSettings.pan) * 100.0f)));
        std::snprintf(tune, sizeof(tune), sms::ui::dpf::themeFormat("%+.2f st"), state_.mixerSettings.tuneSemitones);
        std::snprintf(low, sizeof(low), sms::ui::dpf::themeFormat("%.0f%%"), state_.mixerSettings.lowpass * 100.0f);
        std::snprintf(high, sizeof(high), sms::ui::dpf::themeFormat("%.0f%%"), state_.mixerSettings.highpass * 100.0f);
        drawMixerKnob(uiLayout::mixerKnob(0), InteractiveType::mixerKnob,
            0, "GAIN", state_.mixerSettings.gainDecibels,
            sms::dsp::kMinimumSampleGainDecibels,
            sms::dsp::kMaximumSampleGainDecibels, gain);
        drawMixerKnob(uiLayout::mixerKnob(1), InteractiveType::mixerKnob,
            1, "PAN", state_.mixerSettings.pan,
            sms::dsp::kMinimumSamplePan, sms::dsp::kMaximumSamplePan, pan);
        canvas_.beginPath();
        canvas_.moveTo(uiLayout::padMixerSeparator.x, uiLayout::padMixerSeparator.y);
        canvas_.lineTo(uiLayout::padMixerSeparator.x,
                       uiLayout::padMixerSeparator.y + uiLayout::padMixerSeparator.height);
        canvas_.strokeColor(colors.recessEdge);
        canvas_.strokeWidth(1.5f);
        canvas_.stroke();
        drawDirtySwitch(uiLayout::mixerKnob(6), InteractiveType::mixerKnob,
            state_.mixerSettings.dirty >= 0.5f);
        drawMixerKnob(uiLayout::mixerKnob(2), InteractiveType::mixerKnob,
            2, "TUNE", state_.mixerSettings.tuneSemitones,
            sms::dsp::kMinimumTuneSemitones,
            sms::dsp::kMaximumTuneSemitones, tune);
        drawMixerKnob(uiLayout::mixerKnob(3), InteractiveType::mixerKnob,
            3, "LOWPASS", state_.mixerSettings.lowpass, 0.0f, 1.0f, low);
        drawMixerKnob(uiLayout::mixerKnob(4), InteractiveType::mixerKnob,
            4, "HIGHPASS", state_.mixerSettings.highpass, 0.0f, 1.0f, high);
        drawSlopeSlider(uiLayout::mixerKnob(5), InteractiveType::mixerKnob,
            state_.mixerSettings.filterSlope);
        canvas_.beginPath();
        canvas_.moveTo(uiLayout::padGlobalMixerSeparator.x,
                        uiLayout::padGlobalMixerSeparator.y);
        canvas_.lineTo(uiLayout::padGlobalMixerSeparator.x,
            uiLayout::padGlobalMixerSeparator.y + uiLayout::padGlobalMixerSeparator.height);
        canvas_.strokeColor(colors.recessEdge);
        canvas_.strokeWidth(1.5f);
        canvas_.stroke();
        drawGlobalMixerToggle();
    }

    void drawChopEditor()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::mainPanel);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        char heading[48];
        std::snprintf(heading, sizeof(heading), sms::ui::dpf::themeFormat("%s  /  PAD %02d"),
                      state_.chopSplitMode ? sms::ui::dpf::themeText("SPLIT SAMPLE") : sms::ui::dpf::themeText("ADJUST CUT POINTS"),
                      localPadForGlobalPad(state_.chopTargetPad) + 1);
        text(46.0f, 120.0f, heading, nullptr);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        char neighbors[64];
        if (state_.chopSplitMode) {
            std::snprintf(neighbors, sizeof(neighbors), sms::ui::dpf::themeFormat("PADS %02d + %02d  /  DRAG SPLIT LINE"),
                          localPadForGlobalPad(state_.chopFirstPad) + 1,
                          localPadForGlobalPad(state_.chopFirstPad + 1) + 1);
        } else {
            std::snprintf(neighbors, sizeof(neighbors),
                          sms::ui::dpf::themeFormat("PADS %02d + %02d + %02d  /  DRAG CUT LINES"),
                          localPadForGlobalPad(state_.chopFirstPad) + 1,
                          localPadForGlobalPad(state_.chopFirstPad + 1) + 1,
                          localPadForGlobalPad(state_.chopFirstPad + 2) + 1);
        }
        text(936.0f, 122.0f, neighbors, nullptr);

        const auto combined = chop::combinedWaveform(state_.chopWaveforms);
        const auto visible = sms::ui::waveform::visibleSummary(
            combined, state_.waveformViewport, state_.waveformDetail);
        sms::ui::dpf::drawWaveform(canvas_, uiLayout::chopWaveform, visible,
            state_.chopReady, colors.activityPlayback);
        sms::ui::dpf::drawWaveformViewportLabel(canvas_, uiLayout::chopWaveform,
            state_.waveformViewport, combined.sampleRate);
        drawViewportControls(uiLayout::chopZoom, uiLayout::chopScroll);
        if (state_.chopReady) {
            const float selectedStart = state_.chopSplitMode ? uiLayout::chopWaveform.x :
                chop::boundaryX(
                    uiLayout::chopWaveform, state_.chopWaveforms, state_.chopOffsets, 0,
                    state_.waveformViewport);
            const float selectedEnd = chop::boundaryX(
                uiLayout::chopWaveform, state_.chopWaveforms, state_.chopOffsets,
                state_.chopSplitMode ? 0 : 1, state_.waveformViewport);
            const float clippedStart = std::clamp(selectedStart,
                uiLayout::chopWaveform.x, uiLayout::chopWaveform.x + uiLayout::chopWaveform.width);
            const float clippedEnd = std::clamp(selectedEnd,
                uiLayout::chopWaveform.x, uiLayout::chopWaveform.x + uiLayout::chopWaveform.width);
            canvas_.beginPath();
            canvas_.rect(clippedStart, uiLayout::chopWaveform.y,
                         std::max(0.0f, clippedEnd - clippedStart), uiLayout::chopWaveform.height);
            canvas_.fillColor(colors.selection.withAlpha(0.08f));
            canvas_.fill();
            if (state_.chopSplitMode) {
                if (selectedEnd >= uiLayout::chopWaveform.x &&
                    selectedEnd <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                    sms::ui::dpf::drawCutHandle(canvas_, selectedEnd,
                        uiLayout::chopWaveform.y, uiLayout::chopWaveform.height, "SPLIT",
                        hovered(InteractiveType::chopBoundary, 0));
                }
            } else {
                if (selectedStart >= uiLayout::chopWaveform.x &&
                    selectedStart <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                    sms::ui::dpf::drawCutHandle(canvas_, selectedStart,
                        uiLayout::chopWaveform.y, uiLayout::chopWaveform.height, "CUT 1",
                        hovered(InteractiveType::chopBoundary, 0));
                }
                if (selectedEnd >= uiLayout::chopWaveform.x &&
                    selectedEnd <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                    sms::ui::dpf::drawCutHandle(canvas_, selectedEnd,
                        uiLayout::chopWaveform.y, uiLayout::chopWaveform.height, "CUT 2",
                        hovered(InteractiveType::chopBoundary, 1));
                }
            }
        } else {
            canvas_.fontSize(13.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(colors.contentSecondary);
            text(uiLayout::chopWaveform.x + uiLayout::chopWaveform.width * 0.5f,
                         uiLayout::chopWaveform.y + uiLayout::chopWaveform.height * 0.5f,
                         state_.chopSplitMode ? "LOADING RAW SAMPLE..." :
                                               "LOADING THREE COMPATIBLE RAW SAMPLES...", nullptr);
        }

        const int encodedPlayPad = state_.chopPreviewPosition > 0.0f
            ? static_cast<int>(std::floor(state_.chopPreviewPosition)) - 1 : -1;
        const float playFraction = state_.chopPreviewPosition > 0.0f
            ? state_.chopPreviewPosition - std::floor(state_.chopPreviewPosition) : 0.0f;
        const int originalPad = encodedPlayPad - state_.chopFirstPad;
        const double playSource = chop::sourceFrameForPlayhead(
            state_.chopWaveforms, originalPad, playFraction);
        const auto total = chop::totalFrames(state_.chopWaveforms);
        if (playSource >= 0.0 && total != 0U) {
            const float x = uiLayout::chopWaveform.x + uiLayout::chopWaveform.width *
                static_cast<float>(state_.waveformViewport.zoomed()
                    ? state_.waveformViewport.fraction(playSource) : playSource / total);
            if (x >= uiLayout::chopWaveform.x &&
                x <= uiLayout::chopWaveform.x + uiLayout::chopWaveform.width) {
                canvas_.beginPath();
                canvas_.moveTo(x, uiLayout::chopWaveform.y + 4.0f);
                canvas_.lineTo(x, uiLayout::chopWaveform.y + uiLayout::chopWaveform.height - 4.0f);
                canvas_.strokeColor(colors.selection);
                canvas_.strokeWidth(2.0f);
                canvas_.stroke();
            }
        }

        const int displayedPads = state_.chopSplitMode ? 2 : 3;
        for (int pad = 0; pad < displayedPads; ++pad) {
            char label[48];
            const auto frames = chop::adjustedFrames(
                state_.chopWaveforms, state_.chopOffsets, pad);
            const double rate = chop::sampleRate(state_.chopWaveforms);
            const double seconds = state_.chopReady && rate > 0.0 ? frames / rate : 0.0;
            const bool previewEnabled = state_.chopReady && frames != 0U &&
                !state_.chopApplying;
            std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("PAD %02d   %.2f s"),
                          localPadForGlobalPad(state_.chopFirstPad + pad) + 1, seconds);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::chopPadButton(pad), label,
                state_.chopPreviewPad == pad,
                pad == (state_.chopSplitMode ? 0 : 1)
                    ? colors.selection : colors.activityPlayback,
                hovered(InteractiveType::chopPadPreview, pad), previewEnabled);
        }
    }

    void drawControlPanelBacking()
    {
        const sms::ui::dpf::ScopedCanvasState panelState(canvas_);
        canvas_.globalAlpha(sms::ui::dpf::theme().controlPanelAlpha);
        sms::ui::dpf::drawPanel(canvas_, uiLayout::sidePanel);
    }

    void drawChopEditorPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        drawControlPanelBacking();
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentPrimary);
        text(994.0f, 120.0f,
                     state_.chopSplitMode ? "INSERT SAMPLE SPLIT" : "THREE-PAD CUT EDIT", nullptr);
        canvas_.fontSize(10.0f);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.textBox(994.0f, 164.0f, 222.0f,
            state_.chopSplitMode
                ? "The selected sample is provisionally split in half. Later occupied pads will shift right."
                : "The waveform combines the selected pad with its immediate left and right neighbors.",
            nullptr);
        canvas_.textBox(994.0f, 254.0f, 222.0f,
            state_.chopSplitMode
                ? "Drag SPLIT. Click either pad button below the waveform to hear its pending raw slice."
                : "Drag CUT 1 or CUT 2. Click a pad button below the waveform to hear that raw slice using the pending cuts.",
            nullptr);
        canvas_.textBox(994.0f, 382.0f, 222.0f,
            state_.chopSplitMode
                ? "Apply inserts both halves and shifts pads atomically. Exit cancels the entire operation."
                : "Apply saves cuts and stays here. A zero-length pad is cleared. Arrows discard unapplied cuts.",
            nullptr);
        sms::ui::dpf::drawSegment(canvas_, uiLayout::chopApply,
                                  state_.chopApplying ? "APPLYING..." : "APPLY",
                                  false, colors.selection,
                                  hovered(InteractiveType::chopApply),
                                  state_.chopReady && state_.chopDirty && !state_.chopApplying);
        drawChopArrow(uiLayout::chopPrevious, false,
            hovered(InteractiveType::chopPrevious),
            state_.chopPreviousEnabled && !state_.chopApplying);
        drawChopExit();
        drawChopArrow(uiLayout::chopNext, true,
            hovered(InteractiveType::chopNext),
            state_.chopNextEnabled && !state_.chopApplying);
    }

    void drawChopArrow(const sms::ui::Rect bounds, const bool pointsRight,
                       const bool isHovered, const bool enabled)
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawRaisedControlSurface(canvas_, bounds, colors.activityPlayback,
            {false, isHovered, false, enabled});
        const float centerY = bounds.y + bounds.height * 0.5f;
        canvas_.beginPath();
        canvas_.moveTo(uiLayout::chopArrowTailX(bounds, pointsRight), centerY - 10.0f);
        canvas_.lineTo(uiLayout::chopArrowTipX(bounds, pointsRight), centerY);
        canvas_.lineTo(uiLayout::chopArrowTailX(bounds, pointsRight), centerY + 10.0f);
        canvas_.strokeColor((isHovered ? colors.activityPlayback : colors.contentSecondary)
            .withAlpha(enabled ? 1.0f : colors.disabledAlpha));
        canvas_.strokeWidth(2.5f);
        canvas_.stroke();
    }

    void drawChopExit()
    {
        const auto& colors = sms::ui::dpf::theme();
        const auto bounds = uiLayout::chopExit;
        const float centerX = bounds.x + bounds.width * 0.5f;
        const float centerY = bounds.y + bounds.height * 0.5f;
        const float radius = bounds.width * 0.5f;
        const bool isHovered = hovered(InteractiveType::chopExit);
        const bool enabled = !state_.chopApplying;
        const float alpha = enabled ? 1.0f : colors.disabledAlpha;
        canvas_.beginPath();
        canvas_.circle(centerX + 1.0f, centerY + 2.0f, radius + 2.0f);
        canvas_.fillPaint(canvas_.radialGradient(centerX, centerY, radius * 0.35f,
            radius + 3.0f, colors.shadow.withAlpha(0.72f * alpha),
            colors.shadow.withAlpha(0.02f)));
        canvas_.fill();
        canvas_.beginPath();
        canvas_.circle(centerX, centerY, radius);
        canvas_.fillPaint(canvas_.linearGradient(centerX, bounds.y, centerX,
            bounds.y + bounds.height, colors.controlTop.withAlpha(alpha),
            colors.controlBottom.withAlpha(alpha)));
        canvas_.fill();
        canvas_.strokeColor(isHovered && enabled
            ? colors.controlAccent : colors.outline.withAlpha(0.8f * alpha));
        canvas_.strokeWidth(isHovered && enabled ? 2.0f : 1.0f);
        canvas_.stroke();
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(10.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor((isHovered ? colors.controlAccent : colors.contentSecondary)
            .withAlpha(alpha));
        text(centerX, centerY, "EXIT", nullptr);
    }

    void drawEditorPadPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        drawControlPanelBacking();
        sms::ui::dpf::drawSegment(canvas_, uiLayout::closeEditor, "MAIN VIEW", true,
                                  colors.activityPlayback,
                                  hovered(InteractiveType::closeEditor));
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        text(994.0f, 153.0f, "SELECT PAD", nullptr);
        for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
            char label[4];
            std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("%c"), 'A' + bank);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::editorBank(bank), label,
                                      bank == state_.bank, colors.activityPlayback,
                                      hovered(InteractiveType::bank, bank),
                                      state_.currentPad < 0);
        }
        const auto grid = editorGrid();
        for (int localPad = 0; localPad < pads_.visiblePadCount(); ++localPad) {
            const int pad = globalPad(localPad);
            const auto cell = grid.cell(pads_.visualIndex(localPad));
            const bool occupied = state_.padState[static_cast<std::size_t>(localPad)] != '0' &&
                                  state_.padState[static_cast<std::size_t>(localPad)] != '.';
            const bool active = state_.padActivity[static_cast<std::size_t>(localPad)] != '0';
            const bool selected = pad == state_.selectedPad;
            const bool padHovered = hovered(InteractiveType::pad, localPad);
            const int color = state_.padColors[static_cast<std::size_t>(pad)];
            const auto* tint = color > 0 &&
                color <= static_cast<int>(colors.padColors.size())
                ? &colors.padColors[static_cast<std::size_t>(color - 1)].tint : nullptr;
            sms::ui::dpf::drawRubberPad(canvas_, cell,
                {occupied, active, false, selected, padHovered,
                 state_.pressedPad == localPad, true, tint}, 6.0f);
            char label[12];
            std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("%02d"), localPad + 1);
            canvas_.fontSize(11.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(selected || padHovered ? colors.selection : colors.contentPrimary);
            text(cell.x + 9.0f, cell.y + cell.height * 0.5f, label, nullptr);
            char note[16];
            midiName(mappedMidiNote(pad), note, sizeof(note));
            canvas_.fontSize(10.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(occupied ? colors.activityPlayback : colors.contentSecondary);
            text(cell.x + cell.width - 9.0f, cell.y + cell.height * 0.5f,
                         note, nullptr);
        }
        sms::ui::dpf::drawSymbolButton(canvas_, uiLayout::editorPlayStop,
                                  state_.anyPlaybackActive ? "STOP" : "PLAY",
                                  state_.anyPlaybackActive ? sms::ui::dpf::ControlSymbol::stop
                                      : sms::ui::dpf::ControlSymbol::play,
                                  state_.anyPlaybackActive, colors.controlAccent,
                                  hovered(InteractiveType::playStop));
        sms::ui::dpf::drawSegment(canvas_, uiLayout::playOnSelect, editorAuditionLabel(state_.auditionMode),
                                  editorAuditionPressed(state_.auditionMode), colors.controlAccent,
                                  hovered(InteractiveType::playOnSelect));
    }

    void drawControlPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        drawControlPanelBacking();
        sms::ui::dpf::drawSegment(canvas_, uiLayout::openEditor,
                                  "SAMPLE EDITOR", false,
                                  colors.controlAccent, hovered(InteractiveType::openEditor),
                                  !state_.armed);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        sms::ui::dpf::drawSymbolButton(canvas_, uiLayout::modeToggle,
            "ARM", sms::ui::dpf::ControlSymbol::record,
            state_.armed, colors.activityCapture, hovered(InteractiveType::modeToggle));
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        const bool fixedCapture = state_.captureMode >= 0.5f;
        if (state_.armed) {
            text(994.0f, 204.0f, "CAPTURE MODE", nullptr);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::sequentialMode, "SEQUENTIAL",
                !fixedCapture, colors.activityPlayback,
                hovered(InteractiveType::sequentialMode));
            sms::ui::dpf::drawSegment(canvas_, uiLayout::fixedMode, "FIXED",
                fixedCapture, colors.activityPlayback, hovered(InteractiveType::fixedMode));

            if (fixedCapture) {
                const float length = std::clamp(state_.fixedLengthSeconds,
                    plugin::parameterRanges::fixedLengthSeconds.minimum,
                    plugin::parameterRanges::fixedLengthSeconds.maximum);
                text(994.0f, 278.0f, "FIXED LENGTH", nullptr);
                char lengthText[32];
                std::snprintf(lengthText, sizeof(lengthText), sms::ui::dpf::themeFormat("%.2f s"), length);
                canvas_.fontSize(12.0f);
                canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                                  DGL_NAMESPACE::NanoVG::ALIGN_TOP);
                canvas_.fillColor(colors.contentPrimary);
                text(1216.0f, 278.0f, lengthText, nullptr);
                sms::ui::dpf::drawSlider(canvas_, 994.0f, 298.0f, 222.0f,
                    length / plugin::parameterRanges::fixedLengthSeconds.maximum,
                    colors.activityPlayback, hovered(InteractiveType::fixedLength));
            }

            const auto preRollBounds = uiLayout::preRoll(fixedCapture);
            canvas_.fontSize(11.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentSecondary);
            text(preRollBounds.x, preRollBounds.y - 10.0f, "PRE-ROLL", nullptr);
            char preRoll[24];
            std::snprintf(preRoll, sizeof(preRoll), sms::ui::dpf::themeFormat("%d ms"),
                          static_cast<int>(std::lround(std::clamp(state_.preRollMs,
                              plugin::parameterRanges::preRollMs.minimum,
                              plugin::parameterRanges::preRollMs.maximum))));
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentPrimary);
            text(preRollBounds.x + preRollBounds.width,
                         preRollBounds.y - 10.0f, preRoll, nullptr);
            sms::ui::dpf::drawSlider(canvas_, preRollBounds.x,
                preRollBounds.y + 9.0f, preRollBounds.width,
                std::clamp(state_.preRollMs / plugin::parameterRanges::preRollMs.maximum,
                           0.0f, 1.0f), colors.controlAccent,
                hovered(InteractiveType::preRoll));
        } else {
            text(994.0f, 204.0f, "PLAYBACK", nullptr);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::oneShotMode, "ONE SHOT",
                state_.playbackMode < 0.5f, colors.controlAccent,
                hovered(InteractiveType::oneShotMode));
            sms::ui::dpf::drawSegment(canvas_, uiLayout::holdMode, "HOLD",
                state_.playbackMode >= 0.5f, colors.controlAccent,
                hovered(InteractiveType::holdMode));
            text(994.0f, 278.0f, "MAX VOICES", nullptr);
            char voices[8];
            std::snprintf(voices, sizeof(voices), sms::ui::dpf::themeFormat("%d"), state_.maxVoices);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentPrimary);
            text(1216.0f, 278.0f, voices, nullptr);
            sms::ui::dpf::drawSlider(canvas_, 994.0f, 307.0f, 222.0f,
                static_cast<float>(state_.maxVoices - 1) /
                (plugin::parameterRanges::maxVoices.maximum - 1.0f),
                colors.activityPlayback, hovered(InteractiveType::voiceLimit));
            sms::ui::dpf::drawSymbolButton(canvas_, uiLayout::mainPlayStop,
                state_.anyPlaybackActive ? "STOP" : "PLAY",
                state_.anyPlaybackActive ? sms::ui::dpf::ControlSymbol::stop
                    : sms::ui::dpf::ControlSymbol::play,
                state_.anyPlaybackActive, colors.controlAccent,
                hovered(InteractiveType::playStop));
        }

        drawGlobalMixer();
        const char* monitor = state_.monitorInput >= 1.5f ? "MONITOR  AUTO"
            : state_.monitorInput >= 0.5f ? "MONITOR  ON" : "MONITOR  OFF";
        sms::ui::dpf::drawSegment(canvas_, uiLayout::monitor, monitor,
            plugin::inputMonitorEnabled(state_.monitorInput, state_.armed),
            colors.activityPlayback,
            hovered(InteractiveType::monitor));
        if (state_.armed) {
            canvas_.fontSize(11.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentSecondary);
            sms::ui::dpf::drawAction(canvas_, uiLayout::finalizeAction, "FINALIZE",
                colors.activityPlayback, false, hovered(InteractiveType::finalizeAction));
            sms::ui::dpf::drawAction(canvas_, uiLayout::undoAction, "UNDO",
                colors.controlAccent, false, hovered(InteractiveType::undoAction));
            sms::ui::dpf::drawAction(canvas_, uiLayout::clearAction,
                                     state_.clearArmed ? "CONFIRM" : "CLEAR",
                colors.intentDanger, state_.clearArmed, hovered(InteractiveType::clearAction));
        }
    }

    void drawFooter()
    {
        const auto& colors = sms::ui::dpf::theme();
        char liveStatus[96];
        const char* status = nullptr;
        if ((state_.editorMode || state_.chopEditorMode) && state_.status[0] != '\0') {
            status = state_.status;
        } else if (state_.chopEditorMode) {
            status = state_.chopSplitMode
                ? "Adjust the split, preview both halves, then Apply or Exit"
                : (state_.chopDirty
                    ? "Cut points changed — Apply rewrites the three raw pad samples"
                    : "Drag a cut line, then click a pad button to preview its raw slice");
        } else if (state_.editorMode) {
            std::snprintf(liveStatus, sizeof(liveStatus),
                          sms::ui::dpf::themeFormat("Editing Bank %c Pad %02d — drag region, mixer, or envelope controls"),
                          'A' + bankForGlobalPad(state_.selectedPad),
                          localPadForGlobalPad(state_.selectedPad) + 1);
            status = liveStatus;
        } else if (state_.currentPad >= 0) {
            std::snprintf(liveStatus, sizeof(liveStatus),
                          sms::ui::dpf::themeFormat("Chopping to Bank %c Pad %02d — tap Space or MIDI for next slice"),
                          'A' + bankForGlobalPad(state_.currentPad),
                          localPadForGlobalPad(state_.currentPad) + 1);
            status = liveStatus;
        } else {
            bool bankFull = true;
            for (int pad = 0; pad < pads_.visiblePadCount(); ++pad)
                bankFull = bankFull && state_.padState[static_cast<std::size_t>(pad)] != '0' &&
                           state_.padState[static_cast<std::size_t>(pad)] != '.';
            status = bankFull
                ? (state_.armed ? "Bank full — finalize, undo, or clear to continue"
                                : "Bank full — right-click a pad to clear or replace it") :
                (state_.status[0] != '\0' ? state_.status :
                 (state_.armed ? "Press Space or MIDI to start" : "Ready to play"));
        }
        sms::ui::dpf::drawInsetSurface(canvas_, uiLayout::footer, 7.0f);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(12.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor((state_.editorMode || state_.chopEditorMode) ? colors.activityPlayback :
                          (state_.currentPad >= 0 ? colors.activityCapture
                                                  : colors.contentSecondary));
        text(38.0f, 722.0f, status, nullptr);
        char details[96];
        if (state_.chopEditorMode)
            std::snprintf(details, sizeof(details), sms::ui::dpf::themeFormat("%s   %s"),
                          state_.chopSplitMode ? sms::ui::dpf::themeText("UNAPPLIED SPLIT") :
                              (state_.chopDirty ? sms::ui::dpf::themeText("UNAPPLIED CUTS") : sms::ui::dpf::themeText("CUTS UNCHANGED")),
                          state_.chopApplying ? sms::ui::dpf::themeText("WORKING") :
                              (state_.chopSplitMode ? sms::ui::dpf::themeText("SPLIT PREVIEW") : sms::ui::dpf::themeText("THREE-PAD PREVIEW")));
        else if (state_.editorMode)
            std::snprintf(details, sizeof(details),
                          sms::ui::dpf::themeFormat("GAIN %+.1f dB   PAN %+.0f   TUNE %+.2f st"),
                          state_.mixerSettings.gainDecibels,
                          state_.mixerSettings.pan * 100.0f,
                          state_.mixerSettings.tuneSemitones);
        else if (state_.armed)
            std::snprintf(details, sizeof(details), sms::ui::dpf::themeFormat("START %02d"), state_.startPad + 1);
        else
            std::snprintf(details, sizeof(details),
                          sms::ui::dpf::themeFormat("START %02d   PAN %+.0f   TUNE %+.2f st"),
                          state_.startPad + 1,
                          state_.globalPan * 100.0f, state_.globalTuneSemitones);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor(colors.contentSecondary);
        text(1226.0f, 722.0f, details, nullptr);
    }

    DGL_NAMESPACE::NanoVG& canvas_;
    const ViewState& state_;
    sms::ui::BankedPadLayout pads_;
};

} // namespace

void draw(DGL_NAMESPACE::NanoVG& canvas, const ViewState& state)
{
    Painter(canvas, state).draw();
}

} // namespace midichopper::ui
