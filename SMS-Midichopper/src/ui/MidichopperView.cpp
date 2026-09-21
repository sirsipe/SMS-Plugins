#include "MidichopperView.hpp"

#include "Configuration.hpp"
#include "DPF/AnalogMaterials.hpp"
#include "DPF/Controls.hpp"
#include "DPF/ContextMenu.hpp"
#include "DPF/LevelMeter.hpp"
#include "DPF/Theme.hpp"
#include "DPF/WaveformRenderer.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "PadLayout.hpp"
#include "Parameters.hpp"

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
        sms::ui::dpf::drawScrew(canvas_, 20.0f, 20.0f);
        sms::ui::dpf::drawScrew(canvas_, 1020.0f, 20.0f);
        sms::ui::dpf::drawScrew(canvas_, 20.0f, 660.0f);
        sms::ui::dpf::drawScrew(canvas_, 1020.0f, 660.0f);
        sms::ui::dpf::drawStereoLedMeter(
            canvas_, uiLayout::inputMeter, state_.inputLevels[0], state_.inputLevels[1], "IN");
        sms::ui::dpf::drawStereoLedMeter(
            canvas_, uiLayout::outputMeter, state_.outputLevels[0], state_.outputLevels[1], "OUT");

        canvas_.save();
        canvas_.translate(uiLayout::contentOffsetX, 0.0f);
        drawHeader();
        if (state_.editorMode) {
            drawSampleEditor();
            drawEditorPadPanel();
        } else {
            drawPadPanel();
            drawControlPanel();
        }
        drawFooter();
        if (state_.menuOpen)
            drawMenuOverlay();
        if (state_.padContextMenuOpen)
            sms::ui::dpf::drawContextMenu(canvas_, state_.padContextMenu,
                state_.padContextMenuItems,
                hovered(InteractiveType::padContextItem)
                    ? state_.hoveredTarget.index : -1);
        canvas_.restore();
    }

private:
    [[nodiscard]] bool hovered(const InteractiveType type, const int index = -1) const noexcept
    {
        return isTarget(state_.hoveredTarget, type, index);
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
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(26.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentPrimary);
        canvas_.text(32.0f, 26.0f, "SMS-MIDICHOPPER", nullptr);
        canvas_.fontSize(12.0f);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(34.0f, 58.0f, "SEQUENTIAL CHOP  /  LIVE SAMPLE WORKSTATION", nullptr);

        const auto& modeColor = state_.armed ? colors.activityCapture : colors.activityPlayback;
        sms::ui::dpf::drawLed(canvas_, 842.0f, 43.0f, modeColor, true);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor(modeColor);
        canvas_.text(856.0f, 43.0f, state_.armed ? "ARMED" : "PLAY", nullptr);

        const bool menuHovered = hovered(InteractiveType::menuButton);
        sms::ui::dpf::drawRaisedControlSurface(canvas_, uiLayout::menuButton,
            colors.activityPlayback, {state_.menuOpen, menuHovered, false, true});
        for (int line = 0; line < 3; ++line) {
            canvas_.beginPath();
            canvas_.moveTo(912.0f, 34.0f + static_cast<float>(line) * 8.0f);
            canvas_.lineTo(928.0f, 34.0f + static_cast<float>(line) * 8.0f);
            canvas_.strokeColor(state_.menuOpen || menuHovered ? colors.activityPlayback
                                                : colors.contentSecondary);
            canvas_.strokeWidth(1.5f);
            canvas_.stroke();
        }
    }

    void drawMenuOverlay()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::menuPanel);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(10.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(756.0f, 81.0f, "PAD LAYOUT", nullptr);
        static constexpr const char* labels[] = {
            "16 PADS  ·  4×4", "12 PADS  ·  3×4", "8 PADS   ·  4×2",
        };
        for (int index = 0; index < static_cast<int>(kPadLayoutCount); ++index)
            sms::ui::dpf::drawSegment(canvas_, uiLayout::menuOption(index), labels[index],
                                      index == state_.layout, colors.selection,
                                      hovered(InteractiveType::menuLayout, index),
                                      state_.currentPad < 0);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(756.0f, 207.0f, "MIDI BANK MODE", nullptr);
        static constexpr const char* midiLabels[] = {
            "SELECTED  ·  SHARED", "ALL BANKS  ·  UNIQUE",
        };
        for (int index = 0; index < static_cast<int>(kMidiBankModeCount); ++index)
            sms::ui::dpf::drawSegment(canvas_, uiLayout::midiBankModeOption(index),
                                      midiLabels[index], index == state_.midiBankMode,
                                      colors.selection,
                                      hovered(InteractiveType::menuMidiBankMode, index));
    }

    void drawPadPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::mainPanel);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(13.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(46.0f, 120.0f, state_.armed ? "DESTINATION PADS" : "PAD BANK", nullptr);
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.text(632.0f, 120.0f,
                     state_.armed ? "CLICK A PAD TO SET START" : "CLICK TO PLAY", nullptr);
        canvas_.fontSize(10.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.text(46.0f, 154.0f, "BANK", nullptr);
        for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
            char label[4];
            std::snprintf(label, sizeof(label), "%c", 'A' + bank);
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
            sms::ui::dpf::drawRubberPad(canvas_, cell,
                {occupied, playing, recording, selected, padHovered,
                 state_.pressedPad == localPad, true});

            char padNumber[12];
            std::snprintf(padNumber, sizeof(padNumber), "%02d", localPad + 1);
            canvas_.fontSize(12.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(selected || padHovered ? colors.selection : colors.contentSecondary);
            canvas_.text(cell.x + 12.0f, cell.y + 10.0f, padNumber, nullptr);
            char note[16];
            midiName(mappedMidiNote(pad), note, sizeof(note));
            canvas_.fontSize(20.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(occupied || recording ? colors.contentPrimary
                                                     : colors.contentSecondary);
            canvas_.text(cell.x + cell.width * 0.5f, cell.y + cell.height * 0.54f,
                         note, nullptr);
            canvas_.fontSize(10.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_BOTTOM);
            canvas_.fillColor(recording ? colors.activityCapture :
                              (playing ? colors.activityPlayback : colors.contentSecondary));
            canvas_.text(cell.x + cell.width - 10.0f, cell.y + cell.height - 9.0f,
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
        std::snprintf(heading, sizeof(heading), "SAMPLE EDITOR  /  BANK %c  /  PAD %02d",
                      'A' + bankForGlobalPad(state_.selectedPad),
                      localPadForGlobalPad(state_.selectedPad) + 1);
        canvas_.text(46.0f, 120.0f, heading, nullptr);
        sms::ui::dpf::drawWaveformEditor(canvas_, uiLayout::editorWaveform,
            state_.waveform, state_.hasWaveform, state_.editorSettings,
            hovered(InteractiveType::regionHandle)
                ? static_cast<sms::ui::waveform::EditTarget>(state_.hoveredTarget.index)
                : sms::ui::waveform::EditTarget::none);

        char region[112];
        const auto startFrame = static_cast<std::uint32_t>(
            state_.editorSettings.start * state_.waveform.frames);
        const auto endFrame = static_cast<std::uint32_t>(
            state_.editorSettings.end * state_.waveform.frames);
        std::snprintf(region, sizeof(region),
                      "REGION  %u — %u frames     %.1f%% of source", startFrame, endFrame,
                      (state_.editorSettings.end - state_.editorSettings.start) * 100.0f);
        canvas_.fontSize(11.0f);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(46.0f, 392.0f, region, nullptr);
        canvas_.text(46.0f, 426.0f, "AMPLITUDE ENVELOPE", nullptr);
        canvas_.fontSize(9.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary.withAlpha(0.8f));
        canvas_.text(296.0f, 428.0f, "DRAG NODES", nullptr);
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
    }

    void drawEditorPadPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::sidePanel);
        sms::ui::dpf::drawSegment(canvas_, uiLayout::closeEditor, "MAIN VIEW", true,
                                  colors.activityPlayback,
                                  hovered(InteractiveType::closeEditor));
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(690.0f, 153.0f, "SELECT PAD", nullptr);
        for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
            char label[4];
            std::snprintf(label, sizeof(label), "%c", 'A' + bank);
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
            sms::ui::dpf::drawRubberPad(canvas_, cell,
                {occupied, active, false, selected, padHovered,
                 state_.pressedPad == localPad, true}, 6.0f);
            char label[12];
            std::snprintf(label, sizeof(label), "%02d", localPad + 1);
            canvas_.fontSize(11.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(selected || padHovered ? colors.selection : colors.contentPrimary);
            canvas_.text(cell.x + 9.0f, cell.y + cell.height * 0.5f, label, nullptr);
            char note[16];
            midiName(mappedMidiNote(pad), note, sizeof(note));
            canvas_.fontSize(10.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            canvas_.fillColor(occupied ? colors.activityPlayback : colors.contentSecondary);
            canvas_.text(cell.x + cell.width - 9.0f, cell.y + cell.height * 0.5f,
                         note, nullptr);
        }
        sms::ui::dpf::drawSegment(canvas_, uiLayout::playOnSelect, "PLAY ON SELECT",
                                  state_.playOnSelect, colors.activityPlayback,
                                  hovered(InteractiveType::playOnSelect));
    }

    void drawControlPanel()
    {
        const auto& colors = sms::ui::dpf::theme();
        sms::ui::dpf::drawPanel(canvas_, uiLayout::sidePanel);
        sms::ui::dpf::drawSegment(canvas_, uiLayout::openEditor,
                                  state_.armed ? "EDITOR · PLAY ONLY" : "SAMPLE EDITOR", false,
                                  colors.controlAccent, hovered(InteractiveType::openEditor),
                                  !state_.armed);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        sms::ui::dpf::drawSegment(canvas_, uiLayout::playMode, "PLAY", !state_.armed,
                                  colors.activityPlayback, hovered(InteractiveType::playMode));
        sms::ui::dpf::drawSegment(canvas_, uiLayout::armMode, "ARM", state_.armed,
                                  colors.activityCapture, hovered(InteractiveType::armMode));
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        const bool fixedCapture = state_.captureMode >= 0.5f;
        if (state_.armed) {
            canvas_.text(690.0f, 204.0f, "CAPTURE MODE", nullptr);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::sequentialMode, "SEQUENTIAL",
                !fixedCapture, colors.activityPlayback,
                hovered(InteractiveType::sequentialMode));
            sms::ui::dpf::drawSegment(canvas_, uiLayout::fixedMode, "FIXED",
                fixedCapture, colors.activityPlayback, hovered(InteractiveType::fixedMode));

            if (fixedCapture) {
                const float length = std::clamp(state_.fixedLengthSeconds,
                    plugin::parameterRanges::fixedLengthSeconds.minimum,
                    plugin::parameterRanges::fixedLengthSeconds.maximum);
                canvas_.text(690.0f, 278.0f, "FIXED LENGTH", nullptr);
                char lengthText[32];
                std::snprintf(lengthText, sizeof(lengthText), "%.2f s", length);
                canvas_.fontSize(12.0f);
                canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                                  DGL_NAMESPACE::NanoVG::ALIGN_TOP);
                canvas_.fillColor(colors.contentPrimary);
                canvas_.text(912.0f, 278.0f, lengthText, nullptr);
                sms::ui::dpf::drawSlider(canvas_, 690.0f, 298.0f, 222.0f,
                    length / plugin::parameterRanges::fixedLengthSeconds.maximum,
                    colors.activityPlayback, hovered(InteractiveType::fixedLength));
            }

            const auto preRollBounds = uiLayout::preRoll(fixedCapture);
            canvas_.fontSize(11.0f);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentSecondary);
            canvas_.text(preRollBounds.x, preRollBounds.y - 10.0f, "PRE-ROLL", nullptr);
            char preRoll[24];
            std::snprintf(preRoll, sizeof(preRoll), "%d ms",
                          static_cast<int>(std::lround(std::clamp(state_.preRollMs,
                              plugin::parameterRanges::preRollMs.minimum,
                              plugin::parameterRanges::preRollMs.maximum))));
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentPrimary);
            canvas_.text(preRollBounds.x + preRollBounds.width,
                         preRollBounds.y - 10.0f, preRoll, nullptr);
            sms::ui::dpf::drawSlider(canvas_, preRollBounds.x,
                preRollBounds.y + 9.0f, preRollBounds.width,
                std::clamp(state_.preRollMs / plugin::parameterRanges::preRollMs.maximum,
                           0.0f, 1.0f), colors.controlAccent,
                hovered(InteractiveType::preRoll));
        } else {
            canvas_.text(690.0f, 204.0f, "PLAYBACK", nullptr);
            sms::ui::dpf::drawSegment(canvas_, uiLayout::oneShotMode, "ONE SHOT",
                state_.playbackMode < 0.5f, colors.activityPlayback,
                hovered(InteractiveType::oneShotMode));
            sms::ui::dpf::drawSegment(canvas_, uiLayout::gatedMode, "GATE",
                state_.playbackMode >= 0.5f, colors.activityPlayback,
                hovered(InteractiveType::gatedMode));
            canvas_.text(690.0f, 278.0f, "MAX VOICES", nullptr);
            char voices[8];
            std::snprintf(voices, sizeof(voices), "%d", state_.maxVoices);
            canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                              DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            canvas_.fillColor(colors.contentPrimary);
            canvas_.text(912.0f, 278.0f, voices, nullptr);
            sms::ui::dpf::drawSlider(canvas_, 690.0f, 307.0f, 222.0f,
                static_cast<float>(state_.maxVoices - 1) /
                (plugin::parameterRanges::maxVoices.maximum - 1.0f),
                colors.activityPlayback, hovered(InteractiveType::voiceLimit));
        }

        const char* monitor = state_.monitorInput >= 0.5f ? "MONITOR  ON" : "MONITOR  OFF";
        sms::ui::dpf::drawSegment(canvas_, uiLayout::monitor, monitor,
            state_.monitorInput >= 0.5f, colors.activityPlayback,
            hovered(InteractiveType::monitor));
        canvas_.fontSize(11.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(690.0f, uiLayout::chopLabelY, "CHOP", nullptr);
        sms::ui::dpf::drawAction(canvas_, uiLayout::finalizeAction, "FINALIZE",
            colors.activityPlayback, false, hovered(InteractiveType::finalizeAction));
        sms::ui::dpf::drawAction(canvas_, uiLayout::undoAction, "UNDO",
            colors.controlAccent, false, hovered(InteractiveType::undoAction));
        sms::ui::dpf::drawAction(canvas_, uiLayout::clearAction,
                                 state_.clearArmed ? "CONFIRM" : "CLEAR",
            colors.intentDanger, state_.clearArmed, hovered(InteractiveType::clearAction));
    }

    void drawFooter()
    {
        const auto& colors = sms::ui::dpf::theme();
        char liveStatus[96];
        const char* status = nullptr;
        if (state_.editorMode && state_.status[0] != '\0') {
            status = state_.status;
        } else if (state_.editorMode) {
            std::snprintf(liveStatus, sizeof(liveStatus),
                          "Editing Bank %c Pad %02d — drag cut handles or envelope controls",
                          'A' + bankForGlobalPad(state_.selectedPad),
                          localPadForGlobalPad(state_.selectedPad) + 1);
            status = liveStatus;
        } else if (state_.currentPad >= 0) {
            std::snprintf(liveStatus, sizeof(liveStatus),
                          "Chopping to Bank %c Pad %02d — press any pad for next slice",
                          'A' + bankForGlobalPad(state_.currentPad),
                          localPadForGlobalPad(state_.currentPad) + 1);
            status = liveStatus;
        } else {
            bool bankFull = true;
            for (int pad = 0; pad < pads_.visiblePadCount(); ++pad)
                bankFull = bankFull && state_.padState[static_cast<std::size_t>(pad)] != '0' &&
                           state_.padState[static_cast<std::size_t>(pad)] != '.';
            status = bankFull ? "Bank full — finalize, undo, or clear to continue" :
                (state_.status[0] != '\0' ? state_.status :
                 (state_.armed ? "Press any pad to start" : "Ready to play"));
        }
        sms::ui::dpf::drawInsetSurface(canvas_, {24.0f, 626.0f, 912.0f, 32.0f}, 7.0f);
        canvas_.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas_.fontSize(12.0f);
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor(state_.editorMode ? colors.activityPlayback :
                          (state_.currentPad >= 0 ? colors.activityCapture
                                                  : colors.contentSecondary));
        canvas_.text(38.0f, 642.0f, status, nullptr);
        char details[96];
        if (state_.editorMode)
            std::snprintf(details, sizeof(details),
                          "REGION %.1f%% — %.1f%%   SUSTAIN %d%%",
                          state_.editorSettings.start * 100.0f,
                          state_.editorSettings.end * 100.0f,
                          static_cast<int>(std::lround(
                              state_.editorSettings.sustainLevel * 100.0f)));
        else
            std::snprintf(details, sizeof(details), "START %02d   PRE %d ms   GAIN %+.1f dB",
                          state_.startPad + 1, static_cast<int>(std::lround(state_.preRollMs)),
                          std::clamp(state_.outputGainDb,
                              plugin::parameterRanges::outputGainDb.minimum,
                              plugin::parameterRanges::outputGainDb.maximum));
        canvas_.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                          DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas_.fillColor(colors.contentSecondary);
        canvas_.text(922.0f, 642.0f, details, nullptr);
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
