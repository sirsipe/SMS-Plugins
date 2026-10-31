#include "ViewRenderers.hpp"
#include "ProductControlsRenderer.hpp"
#include "PadViewMapping.hpp"
#include "DPF/AnalogMaterials.hpp"
#include "DPF/Controls.hpp"
#include "DPF/WaveformRenderer.hpp"
#include "LogoGlow.hpp"
#include "Parameters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace midichopper::ui::rendering {

namespace uiLayout = layout;

namespace {

void drawPadPanel(const ViewRenderContext& ctx)
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    const auto& colors = sms::ui::dpf::theme();
    {
        const sms::ui::dpf::ScopedCanvasState panelState(ctx.canvas);
        ctx.canvas.globalAlpha(colors.padPanelAlpha);
        sms::ui::dpf::drawPanel(ctx.canvas, uiLayout::mainPanel);
    }
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(13.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 46.0f, 120.0f, ctx.state.armed ? "DESTINATION PADS" : "PAD BANK", nullptr);
    ctx.canvas.fontSize(11.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    text(ctx, 936.0f, 120.0f, ctx.state.armed ? "CLICK A PAD TO SET START" : "CLICK TO PLAY",
         nullptr);
    ctx.canvas.fontSize(10.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    text(ctx, 46.0f, 154.0f, "BANK", nullptr);
    for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
        char label[4];
        std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("%c"), 'A' + bank);
        sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::mainBank(bank), label,
                                  bank == ctx.state.bank, colors.activityPlayback,
                                  hovered(ctx, InteractiveType::bank, bank),
                                  ctx.state.currentPad < 0);
    }
    sms::ui::dpf::drawWaveform(
        ctx.canvas, uiLayout::overviewWaveform, ctx.state.waveform, ctx.state.hasWaveform,
        ctx.state.currentPad >= 0 ? colors.activityCapture : colors.activityPlayback);

    const auto grid = mainGrid(ctx);
    for (int localPad = 0; localPad < pads.visiblePadCount(); ++localPad) {
        const int pad = globalPad(ctx, localPad);
        const auto cell = grid.cell(pads.visualIndex(localPad));
        const bool occupied = ctx.state.padState[static_cast<std::size_t>(localPad)] != '0' &&
                              ctx.state.padState[static_cast<std::size_t>(localPad)] != '.';
        const bool active = ctx.state.padActivity[static_cast<std::size_t>(localPad)] != '0';
        const bool recording = (ctx.state.armed && active) || ctx.state.currentPad == pad;
        const bool playing = (!ctx.state.armed && active) || ctx.state.pressedPad == localPad;
        const bool selected = ctx.state.selectedPad == pad;
        const bool padHovered = hovered(ctx, InteractiveType::pad, localPad);
        const int color = ctx.state.padColors[static_cast<std::size_t>(pad)];
        const auto* tint = color > 0 && color <= static_cast<int>(colors.padColors.size())
                               ? &colors.padColors[static_cast<std::size_t>(color - 1)].tint
                               : nullptr;
        sms::ui::dpf::drawRubberPad(ctx.canvas, cell,
                                    {occupied, playing, recording, selected, padHovered,
                                     ctx.state.pressedPad == localPad, true, tint});

        char padNumber[12];
        std::snprintf(padNumber, sizeof(padNumber), sms::ui::dpf::themeFormat("%02d"),
                      localPad + 1);
        ctx.canvas.fontSize(12.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        ctx.canvas.fillColor(selected || padHovered ? colors.selection : colors.contentSecondary);
        text(ctx, cell.x + 12.0f, cell.y + 10.0f, padNumber, nullptr);
        char note[16];
        midiName(mappedMidiNote(ctx, pad), note, sizeof(note));
        ctx.canvas.fontSize(20.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_CENTER |
                             DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        ctx.canvas.fillColor(occupied || recording ? colors.contentPrimary
                                                   : colors.contentSecondary);
        text(ctx, cell.x + cell.width * 0.5f, cell.y + cell.height * 0.54f, note, nullptr);
        ctx.canvas.fontSize(10.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                             DGL_NAMESPACE::NanoVG::ALIGN_BOTTOM);
        ctx.canvas.fillColor(recording
                                 ? colors.activityCapture
                                 : (playing ? colors.activityPlayback : colors.contentSecondary));
        text(ctx, cell.x + cell.width - 10.0f, cell.y + cell.height - 9.0f,
             recording ? "RECORDING" : (playing ? "PLAYING" : (occupied ? "READY" : "EMPTY")),
             nullptr);
    }
}

void drawControlPanel(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    drawControlPanelBacking(ctx);
    sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::openEditor, "SAMPLE EDITOR", false,
                              colors.controlAccent, hovered(ctx, InteractiveType::openEditor),
                              !ctx.state.armed);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    sms::ui::dpf::drawSymbolButton(
        ctx.canvas, uiLayout::modeToggle, "ARM", sms::ui::dpf::ControlSymbol::record,
        ctx.state.armed, colors.activityCapture, hovered(ctx, InteractiveType::modeToggle));
    ctx.canvas.fontSize(11.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    const bool fixedCapture = ctx.state.captureMode >= 0.5f;
    if (ctx.state.armed) {
        text(ctx, 994.0f, 204.0f, "CAPTURE MODE", nullptr);
        sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::sequentialMode, "SEQUENTIAL", !fixedCapture,
                                  colors.activityPlayback,
                                  hovered(ctx, InteractiveType::sequentialMode));
        sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::fixedMode, "FIXED", fixedCapture,
                                  colors.activityPlayback,
                                  hovered(ctx, InteractiveType::fixedMode));

        if (fixedCapture) {
            const float length = std::clamp(ctx.state.fixedLengthSeconds,
                                            plugin::parameterRanges::fixedLengthSeconds.minimum,
                                            plugin::parameterRanges::fixedLengthSeconds.maximum);
            text(ctx, 994.0f, 278.0f, "FIXED LENGTH", nullptr);
            char lengthText[32];
            std::snprintf(lengthText, sizeof(lengthText), sms::ui::dpf::themeFormat("%.2f s"),
                          length);
            ctx.canvas.fontSize(12.0f);
            ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                                 DGL_NAMESPACE::NanoVG::ALIGN_TOP);
            ctx.canvas.fillColor(colors.contentPrimary);
            text(ctx, 1216.0f, 278.0f, lengthText, nullptr);
            sms::ui::dpf::drawSlider(ctx.canvas, 994.0f, 298.0f, 222.0f,
                                     length / plugin::parameterRanges::fixedLengthSeconds.maximum,
                                     colors.activityPlayback,
                                     hovered(ctx, InteractiveType::fixedLength));
        }

        const auto preRollBounds = uiLayout::preRoll(fixedCapture);
        ctx.canvas.fontSize(11.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        ctx.canvas.fillColor(colors.contentSecondary);
        text(ctx, preRollBounds.x, preRollBounds.y - 10.0f, "PRE-ROLL", nullptr);
        char preRoll[24];
        std::snprintf(preRoll, sizeof(preRoll), sms::ui::dpf::themeFormat("%d ms"),
                      static_cast<int>(std::lround(std::clamp(
                          ctx.state.preRollMs, plugin::parameterRanges::preRollMs.minimum,
                          plugin::parameterRanges::preRollMs.maximum))));
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        ctx.canvas.fillColor(colors.contentPrimary);
        text(ctx, preRollBounds.x + preRollBounds.width, preRollBounds.y - 10.0f, preRoll, nullptr);
        sms::ui::dpf::drawSlider(
            ctx.canvas, preRollBounds.x, preRollBounds.y + 9.0f, preRollBounds.width,
            std::clamp(ctx.state.preRollMs / plugin::parameterRanges::preRollMs.maximum, 0.0f,
                       1.0f),
            colors.controlAccent, hovered(ctx, InteractiveType::preRoll));
    } else {
        text(ctx, 994.0f, 204.0f, "PLAYBACK", nullptr);
        sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::oneShotMode, "ONE SHOT",
                                  ctx.state.playbackMode < 0.5f, colors.controlAccent,
                                  hovered(ctx, InteractiveType::oneShotMode));
        sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::holdMode, "HOLD",
                                  ctx.state.playbackMode >= 0.5f, colors.controlAccent,
                                  hovered(ctx, InteractiveType::holdMode));
        text(ctx, 994.0f, 278.0f, "MAX VOICES", nullptr);
        char voices[8];
        std::snprintf(voices, sizeof(voices), sms::ui::dpf::themeFormat("%d"), ctx.state.maxVoices);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        ctx.canvas.fillColor(colors.contentPrimary);
        text(ctx, 1216.0f, 278.0f, voices, nullptr);
        sms::ui::dpf::drawSlider(ctx.canvas, 994.0f, 307.0f, 222.0f,
                                 static_cast<float>(ctx.state.maxVoices - 1) /
                                     (plugin::parameterRanges::maxVoices.maximum - 1.0f),
                                 colors.activityPlayback,
                                 hovered(ctx, InteractiveType::voiceLimit));
        sms::ui::dpf::drawSymbolButton(
            ctx.canvas, uiLayout::mainPlayStop, ctx.state.anyPlaybackActive ? "STOP" : "PLAY",
            ctx.state.anyPlaybackActive ? sms::ui::dpf::ControlSymbol::stop
                                        : sms::ui::dpf::ControlSymbol::play,
            ctx.state.anyPlaybackActive, colors.controlAccent,
            hovered(ctx, InteractiveType::playStop));
    }

    drawGlobalMixer(ctx);
    const char* monitor = ctx.state.monitorInput >= 1.5f   ? "MONITOR  AUTO"
                          : ctx.state.monitorInput >= 0.5f ? "MONITOR  ON"
                                                           : "MONITOR  OFF";
    sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::monitor, monitor,
                              plugin::inputMonitorEnabled(ctx.state.monitorInput, ctx.state.armed),
                              colors.activityPlayback, hovered(ctx, InteractiveType::monitor));
    if (ctx.state.armed) {
        ctx.canvas.fontSize(11.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
        ctx.canvas.fillColor(colors.contentSecondary);
        sms::ui::dpf::drawAction(ctx.canvas, uiLayout::finalizeAction, "FINALIZE",
                                 colors.activityPlayback, false,
                                 hovered(ctx, InteractiveType::finalizeAction));
        sms::ui::dpf::drawAction(ctx.canvas, uiLayout::undoAction, "UNDO", colors.controlAccent,
                                 false, hovered(ctx, InteractiveType::undoAction));
        sms::ui::dpf::drawAction(ctx.canvas, uiLayout::clearAction,
                                 ctx.state.clearArmed ? "CONFIRM" : "CLEAR", colors.intentDanger,
                                 ctx.state.clearArmed, hovered(ctx, InteractiveType::clearAction));
    }
}

} // namespace

void drawMainHeader(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    DGL_NAMESPACE::Rectangle<float> textBounds;
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(26.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentPrimary);
    text(ctx, 32.0f, 26.0f, "SMS-ANVILLE SAMPLER", nullptr);
    measureText(ctx, 32.0f, 26.0f, "SMS-ANVILLE SAMPLER", nullptr, textBounds);
    float textRight = textBounds.getX() + textBounds.getWidth();
    ctx.canvas.fontSize(12.0f);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 34.0f, 58.0f, "SEQUENTIAL CHOP  /  LIVE SAMPLE WORKSTATION", nullptr);
    measureText(ctx, 34.0f, 58.0f, "SEQUENTIAL CHOP  /  LIVE SAMPLE WORKSTATION", nullptr,
                textBounds);
    textRight = std::max(textRight, textBounds.getX() + textBounds.getWidth());
    ctx.canvas.fontSize(10.0f);
    text(ctx, 34.0f, 76.0f, "Version " ANVILLESAMPLER_VERSION_STRING, nullptr);
    measureText(ctx, 34.0f, 76.0f, "Version " ANVILLESAMPLER_VERSION_STRING, nullptr, textBounds);
    textRight = std::max(textRight, textBounds.getX() + textBounds.getWidth());

    if (ctx.state.logo != nullptr && ctx.state.logo->isValid()) {
        const float x = textRight + 14.0f;
        constexpr float y = 13.0f;
        constexpr float width = sms::ui::logoWidth;
        constexpr float height = sms::ui::logoHeight;
        const bool glow = ctx.state.logoGlow != nullptr && ctx.state.logoGlow->isValid();
        if (glow) {
            constexpr float padding = sms::ui::logoGlowPadding;
            constexpr float glowWidth = sms::ui::logoGlowWidth;
            constexpr float glowHeight = sms::ui::logoGlowHeight;
            auto paint = ctx.canvas.imagePattern(x - padding, y - padding, glowWidth, glowHeight,
                                                 0.0f, *ctx.state.logoGlow, 1.0f);
            paint.innerColor = paint.outerColor =
                colors.logoGlow.withAlpha(colors.logoGlow.alpha * ctx.state.logoGlowOpacity);
            ctx.canvas.beginPath();
            ctx.canvas.rect(x - padding, y - padding, glowWidth, glowHeight);
            ctx.canvas.fillPaint(paint);
            ctx.canvas.fill();
        }
        ctx.canvas.beginPath();
        ctx.canvas.rect(x, y, width, height);
        ctx.canvas.fillPaint(ctx.canvas.imagePattern(x, y, width, height, 0.0f, *ctx.state.logo,
                                                     glow ? 1.0f : 0.48f));
        ctx.canvas.fill();
    }

    const auto& modeColor = ctx.state.armed ? colors.intentDanger : colors.meterGreen;
    sms::ui::dpf::drawLed(ctx.canvas, 1146.0f, 43.0f, modeColor, true);
    ctx.canvas.fontSize(13.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    ctx.canvas.fillColor(modeColor);
    text(ctx, 1160.0f, 43.0f, ctx.state.armed ? "ARMED" : "PLAY", nullptr);

    const bool menuHovered = hovered(ctx, InteractiveType::menuButton);
    sms::ui::dpf::drawRaisedControlSurface(ctx.canvas, uiLayout::menuButton,
                                           colors.activityPlayback,
                                           {ctx.state.menuOpen, menuHovered, false, true});
    for (int line = 0; line < 3; ++line) {
        ctx.canvas.beginPath();
        ctx.canvas.moveTo(1216.0f, 34.0f + static_cast<float>(line) * 8.0f);
        ctx.canvas.lineTo(1232.0f, 34.0f + static_cast<float>(line) * 8.0f);
        ctx.canvas.strokeColor(ctx.state.menuOpen || menuHovered ? colors.activityPlayback
                                                                 : colors.contentSecondary);
        ctx.canvas.strokeWidth(1.5f);
        ctx.canvas.stroke();
    }
}

void drawMainFooter(const ViewRenderContext& ctx)
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    const auto& colors = sms::ui::dpf::theme();
    char liveStatus[96];
    const char* status = nullptr;
    if ((ctx.state.editorMode || ctx.state.chopEditorMode) && ctx.state.status[0] != '\0') {
        status = ctx.state.status;
    } else if (ctx.state.chopEditorMode) {
        status =
            ctx.state.chopSplitMode
                ? "Adjust the split, preview both halves, then Apply or Exit"
                : (ctx.state.chopDirty
                       ? "Cut points changed — Exit or arrows save; Cancel Changes restores cuts"
                       : "Drag a cut line, then click a pad button to preview its raw slice");
    } else if (ctx.state.editorMode) {
        std::snprintf(liveStatus, sizeof(liveStatus),
                      sms::ui::dpf::themeFormat(
                          "Editing Bank %c Pad %02d — drag region, mixer, or envelope controls"),
                      'A' + bankForGlobalPad(ctx, ctx.state.selectedPad),
                      localPadForGlobalPad(ctx, ctx.state.selectedPad) + 1);
        status = liveStatus;
    } else if (ctx.state.currentPad >= 0) {
        std::snprintf(liveStatus, sizeof(liveStatus),
                      sms::ui::dpf::themeFormat(
                          "Chopping to Bank %c Pad %02d — tap Space or MIDI for next slice"),
                      'A' + bankForGlobalPad(ctx, ctx.state.currentPad),
                      localPadForGlobalPad(ctx, ctx.state.currentPad) + 1);
        status = liveStatus;
    } else {
        bool bankFull = true;
        for (int pad = 0; pad < pads.visiblePadCount(); ++pad)
            bankFull = bankFull && ctx.state.padState[static_cast<std::size_t>(pad)] != '0' &&
                       ctx.state.padState[static_cast<std::size_t>(pad)] != '.';
        status = bankFull
                     ? (ctx.state.armed ? "Bank full — finalize, undo, or clear to continue"
                                        : "Bank full — right-click a pad to clear or replace it")
                     : (ctx.state.status[0] != '\0'
                            ? ctx.state.status
                            : (ctx.state.armed ? "Press Space or MIDI to start" : "Ready to play"));
    }
    sms::ui::dpf::drawInsetSurface(ctx.canvas, uiLayout::footer, 7.0f);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(12.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    ctx.canvas.fillColor(
        (ctx.state.editorMode || ctx.state.chopEditorMode)
            ? colors.activityPlayback
            : (ctx.state.currentPad >= 0 ? colors.activityCapture : colors.contentSecondary));
    text(ctx, 38.0f, 722.0f, status, nullptr);
    char details[96];
    if (ctx.state.chopEditorMode)
        std::snprintf(details, sizeof(details), sms::ui::dpf::themeFormat("%s   %s"),
                      ctx.state.chopSplitMode
                          ? sms::ui::dpf::themeText("UNAPPLIED SPLIT")
                          : (ctx.state.chopDirty ? sms::ui::dpf::themeText("UNSAVED CUTS")
                                                 : sms::ui::dpf::themeText("CUTS UNCHANGED")),
                      ctx.state.chopApplying
                          ? sms::ui::dpf::themeText("WORKING")
                          : (ctx.state.chopSplitMode
                                 ? sms::ui::dpf::themeText("SPLIT PREVIEW")
                                 : sms::ui::dpf::themeText("THREE-PAD PREVIEW")));
    else if (ctx.state.editorMode)
        std::snprintf(details, sizeof(details),
                      sms::ui::dpf::themeFormat("GAIN %+.1f dB   PAN %+.0f   TUNE %+.2f st"),
                      ctx.state.mixerSettings.gainDecibels, ctx.state.mixerSettings.pan * 100.0f,
                      ctx.state.mixerSettings.tuneSemitones);
    else if (ctx.state.armed)
        std::snprintf(details, sizeof(details), sms::ui::dpf::themeFormat("START %02d"),
                      ctx.state.startPad + 1);
    else
        std::snprintf(details, sizeof(details),
                      sms::ui::dpf::themeFormat("START %02d   PAN %+.0f   TUNE %+.2f st"),
                      ctx.state.startPad + 1, ctx.state.globalPan * 100.0f,
                      ctx.state.globalTuneSemitones);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 1226.0f, 722.0f, details, nullptr);
}

void drawMainView(const ViewRenderContext& ctx)
{
    drawPadPanel(ctx);
    drawControlPanel(ctx);
}

} // namespace midichopper::ui::rendering
