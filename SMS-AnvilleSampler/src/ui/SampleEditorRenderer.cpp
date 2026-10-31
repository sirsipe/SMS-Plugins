#include "ViewRenderers.hpp"
#include "ProductControlsRenderer.hpp"
#include "PadViewMapping.hpp"
#include "DPF/AnalogMaterials.hpp"
#include "DPF/Controls.hpp"
#include "DPF/WaveformRenderer.hpp"
#include "SampleWaveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace midichopper::ui::rendering {

namespace uiLayout = layout;

namespace {

void drawSampleEditor(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    sms::ui::dpf::drawPanel(ctx.canvas, uiLayout::mainPanel);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(13.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    char heading[80];
    std::snprintf(heading, sizeof(heading),
                  sms::ui::dpf::themeFormat("SAMPLE EDITOR  /  BANK %c  /  PAD %02d"),
                  'A' + bankForGlobalPad(ctx, ctx.state.selectedPad),
                  localPadForGlobalPad(ctx, ctx.state.selectedPad) + 1);
    text(ctx, 46.0f, 120.0f, heading, nullptr);
    const bool reverse = sampleDisplayReversed(
        ctx.state.mixerSettings.reverse >= 0.5f,
        sampleGlobalReverse(ctx.state.globalReverse, ctx.state.mixerSettings.globalMixer));
    const auto displayWaveform = sampleDisplaySummary(ctx.state.waveform, reverse);
    const sms::ui::waveform::CachedDetail displayDetail{
        sampleVisibleSummary(ctx.state.waveform, ctx.state.waveformViewport,
                             ctx.state.waveformDetail, reverse),
        ctx.state.waveformViewport.start, ctx.state.waveformViewport.end};
    sms::ui::dpf::drawWaveformEditor(
        ctx.canvas, uiLayout::editorWaveform, displayWaveform, ctx.state.hasWaveform,
        ctx.state.editorSettings,
        hovered(ctx, InteractiveType::regionHandle)
            ? static_cast<sms::ui::waveform::EditTarget>(ctx.state.hoveredTarget.index)
            : sms::ui::waveform::EditTarget::none,
        &displayDetail, ctx.state.waveformViewport);
    drawViewportControls(ctx, uiLayout::editorZoom, uiLayout::editorScroll);

    const int playbackPad = ctx.state.playbackPosition > 0.0f
                                ? static_cast<int>(std::floor(ctx.state.playbackPosition)) - 1
                                : -1;
    if (playbackPad == ctx.state.selectedPad) {
        const float fraction = ctx.state.playbackPosition - std::floor(ctx.state.playbackPosition);
        const float x =
            uiLayout::editorWaveform.x +
            uiLayout::editorWaveform.width *
                static_cast<float>(ctx.state.waveformViewport.zoomed()
                                       ? ctx.state.waveformViewport.fraction(
                                             fraction * ctx.state.waveformViewport.total)
                                       : fraction);
        if (x >= uiLayout::editorWaveform.x &&
            x <= uiLayout::editorWaveform.x + uiLayout::editorWaveform.width) {
            ctx.canvas.beginPath();
            ctx.canvas.moveTo(x, uiLayout::editorWaveform.y + 4.0f);
            ctx.canvas.lineTo(x,
                              uiLayout::editorWaveform.y + uiLayout::editorWaveform.height - 4.0f);
            ctx.canvas.strokeColor(colors.shadow.withAlpha(0.8f));
            ctx.canvas.strokeWidth(4.0f);
            ctx.canvas.stroke();
            ctx.canvas.beginPath();
            ctx.canvas.moveTo(x, uiLayout::editorWaveform.y + 4.0f);
            ctx.canvas.lineTo(x,
                              uiLayout::editorWaveform.y + uiLayout::editorWaveform.height - 4.0f);
            ctx.canvas.strokeColor(colors.selection);
            ctx.canvas.strokeWidth(2.0f);
            ctx.canvas.stroke();
            ctx.canvas.beginPath();
            ctx.canvas.moveTo(x - 5.0f, uiLayout::editorWaveform.y + 4.0f);
            ctx.canvas.lineTo(x + 5.0f, uiLayout::editorWaveform.y + 4.0f);
            ctx.canvas.lineTo(x, uiLayout::editorWaveform.y + 11.0f);
            ctx.canvas.closePath();
            ctx.canvas.fillColor(colors.selection);
            ctx.canvas.fill();
        }
    }

    char region[112];
    const auto startFrame =
        static_cast<std::uint32_t>(ctx.state.editorSettings.start * ctx.state.waveform.frames);
    const auto endFrame =
        static_cast<std::uint32_t>(ctx.state.editorSettings.end * ctx.state.waveform.frames);
    std::snprintf(region, sizeof(region),
                  sms::ui::dpf::themeFormat("REGION  %u — %u frames     %.1f%% of source"),
                  startFrame, endFrame,
                  (ctx.state.editorSettings.end - ctx.state.editorSettings.start) * 100.0f);
    ctx.canvas.fontSize(11.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 46.0f, 402.0f, region, nullptr);
    text(ctx, 46.0f, 426.0f, "AMPLITUDE ENVELOPE", nullptr);
    ctx.canvas.fontSize(9.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary.withAlpha(0.8f));
    text(ctx, 416.0f, 428.0f, "DRAG NODES", nullptr);
    sms::ui::dpf::drawEnvelopeGraph(
        ctx.canvas, uiLayout::envelopeGraph, ctx.state.waveform, ctx.state.editorSettings,
        hovered(ctx, InteractiveType::envelopeNode)
            ? static_cast<sms::ui::waveform::EditTarget>(ctx.state.hoveredTarget.index)
            : sms::ui::waveform::EditTarget::none);
    sms::ui::dpf::drawEnvelopeSlider(ctx.canvas, uiLayout::editorSlider(0), "ATTACK",
                                     ctx.state.editorSettings.attackSeconds, false,
                                     hovered(ctx, InteractiveType::envelopeSlider, 0));
    sms::ui::dpf::drawEnvelopeSlider(ctx.canvas, uiLayout::editorSlider(1), "DECAY",
                                     ctx.state.editorSettings.decaySeconds, false,
                                     hovered(ctx, InteractiveType::envelopeSlider, 1));
    sms::ui::dpf::drawEnvelopeSlider(ctx.canvas, uiLayout::editorSlider(2), "SUSTAIN",
                                     ctx.state.editorSettings.sustainLevel, true,
                                     hovered(ctx, InteractiveType::envelopeSlider, 2));
    sms::ui::dpf::drawEnvelopeSlider(ctx.canvas, uiLayout::editorSlider(3), "RELEASE",
                                     ctx.state.editorSettings.releaseSeconds, false,
                                     hovered(ctx, InteractiveType::envelopeSlider, 3));

    ctx.canvas.fontSize(11.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 46.0f, 570.0f, "PAD MIXER", nullptr);
    drawDirectionToggle(ctx, uiLayout::padDirection, InteractiveType::padDirection,
                        ctx.state.mixerSettings.reverse >= 0.5f);
    char gain[24];
    char pan[24];
    char tune[24];
    char low[16];
    char high[16];
    std::snprintf(gain, sizeof(gain), sms::ui::dpf::themeFormat("%+.1f dB"),
                  ctx.state.mixerSettings.gainDecibels);
    if (std::abs(ctx.state.mixerSettings.pan) < 0.005f)
        std::snprintf(pan, sizeof(pan), sms::ui::dpf::themeFormat("%s"),
                      sms::ui::dpf::themeText("CENTER"));
    else
        std::snprintf(
            pan, sizeof(pan), sms::ui::dpf::themeFormat("%c%d"),
            ctx.state.mixerSettings.pan < 0.0f ? 'L' : 'R',
            static_cast<int>(std::lround(std::abs(ctx.state.mixerSettings.pan) * 100.0f)));
    std::snprintf(tune, sizeof(tune), sms::ui::dpf::themeFormat("%+.2f st"),
                  ctx.state.mixerSettings.tuneSemitones);
    std::snprintf(low, sizeof(low), sms::ui::dpf::themeFormat("%.0f%%"),
                  ctx.state.mixerSettings.lowpass * 100.0f);
    std::snprintf(high, sizeof(high), sms::ui::dpf::themeFormat("%.0f%%"),
                  ctx.state.mixerSettings.highpass * 100.0f);
    drawMixerKnob(ctx, uiLayout::mixerKnob(0), InteractiveType::mixerKnob, 0, "GAIN",
                  ctx.state.mixerSettings.gainDecibels, sms::dsp::kMinimumSampleGainDecibels,
                  sms::dsp::kMaximumSampleGainDecibels, gain);
    drawMixerKnob(ctx, uiLayout::mixerKnob(1), InteractiveType::mixerKnob, 1, "PAN",
                  ctx.state.mixerSettings.pan, sms::dsp::kMinimumSamplePan,
                  sms::dsp::kMaximumSamplePan, pan);
    ctx.canvas.beginPath();
    ctx.canvas.moveTo(uiLayout::padMixerSeparator.x, uiLayout::padMixerSeparator.y);
    ctx.canvas.lineTo(uiLayout::padMixerSeparator.x,
                      uiLayout::padMixerSeparator.y + uiLayout::padMixerSeparator.height);
    ctx.canvas.strokeColor(colors.recessEdge);
    ctx.canvas.strokeWidth(1.5f);
    ctx.canvas.stroke();
    drawDirtySwitch(ctx, uiLayout::mixerKnob(6), InteractiveType::mixerKnob,
                    ctx.state.mixerSettings.dirty >= 0.5f);
    drawMixerKnob(ctx, uiLayout::mixerKnob(2), InteractiveType::mixerKnob, 2, "TUNE",
                  ctx.state.mixerSettings.tuneSemitones, sms::dsp::kMinimumTuneSemitones,
                  sms::dsp::kMaximumTuneSemitones, tune);
    drawMixerKnob(ctx, uiLayout::mixerKnob(3), InteractiveType::mixerKnob, 3, "LOWPASS",
                  ctx.state.mixerSettings.lowpass, 0.0f, 1.0f, low);
    drawMixerKnob(ctx, uiLayout::mixerKnob(4), InteractiveType::mixerKnob, 4, "HIGHPASS",
                  ctx.state.mixerSettings.highpass, 0.0f, 1.0f, high);
    drawSlopeSlider(ctx, uiLayout::mixerKnob(5), InteractiveType::mixerKnob,
                    ctx.state.mixerSettings.filterSlope);
    ctx.canvas.beginPath();
    ctx.canvas.moveTo(uiLayout::padGlobalMixerSeparator.x, uiLayout::padGlobalMixerSeparator.y);
    ctx.canvas.lineTo(uiLayout::padGlobalMixerSeparator.x,
                      uiLayout::padGlobalMixerSeparator.y +
                          uiLayout::padGlobalMixerSeparator.height);
    ctx.canvas.strokeColor(colors.recessEdge);
    ctx.canvas.strokeWidth(1.5f);
    ctx.canvas.stroke();
    drawGlobalMixerToggle(ctx);
}

void drawEditorPadPanel(const ViewRenderContext& ctx)
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    const auto& colors = sms::ui::dpf::theme();
    drawControlPanelBacking(ctx);
    sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::closeEditor, "MAIN VIEW", true,
                              colors.activityPlayback, hovered(ctx, InteractiveType::closeEditor));
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(11.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    text(ctx, 994.0f, 153.0f, "SELECT PAD", nullptr);
    for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
        char label[4];
        std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("%c"), 'A' + bank);
        sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::editorBank(bank), label,
                                  bank == ctx.state.bank, colors.activityPlayback,
                                  hovered(ctx, InteractiveType::bank, bank),
                                  ctx.state.currentPad < 0);
    }
    const auto grid = editorGrid(ctx);
    for (int localPad = 0; localPad < pads.visiblePadCount(); ++localPad) {
        const int pad = globalPad(ctx, localPad);
        const auto cell = grid.cell(pads.visualIndex(localPad));
        const bool occupied = ctx.state.padState[static_cast<std::size_t>(localPad)] != '0' &&
                              ctx.state.padState[static_cast<std::size_t>(localPad)] != '.';
        const bool active = ctx.state.padActivity[static_cast<std::size_t>(localPad)] != '0';
        const bool selected = pad == ctx.state.selectedPad;
        const bool padHovered = hovered(ctx, InteractiveType::pad, localPad);
        const int color = ctx.state.padColors[static_cast<std::size_t>(pad)];
        const auto* tint = color > 0 && color <= static_cast<int>(colors.padColors.size())
                               ? &colors.padColors[static_cast<std::size_t>(color - 1)].tint
                               : nullptr;
        sms::ui::dpf::drawRubberPad(ctx.canvas, cell,
                                    {occupied, active, false, selected, padHovered,
                                     ctx.state.pressedPad == localPad, true, tint},
                                    6.0f);
        char label[12];
        std::snprintf(label, sizeof(label), sms::ui::dpf::themeFormat("%02d"), localPad + 1);
        ctx.canvas.fontSize(11.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                             DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        ctx.canvas.fillColor(selected || padHovered ? colors.selection : colors.contentPrimary);
        text(ctx, cell.x + 9.0f, cell.y + cell.height * 0.5f, label, nullptr);
        char note[16];
        midiName(mappedMidiNote(ctx, pad), note, sizeof(note));
        ctx.canvas.fontSize(10.0f);
        ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                             DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        ctx.canvas.fillColor(occupied ? colors.activityPlayback : colors.contentSecondary);
        text(ctx, cell.x + cell.width - 9.0f, cell.y + cell.height * 0.5f, note, nullptr);
    }
    sms::ui::dpf::drawSymbolButton(
        ctx.canvas, uiLayout::editorPlayStop, ctx.state.anyPlaybackActive ? "STOP" : "PLAY",
        ctx.state.anyPlaybackActive ? sms::ui::dpf::ControlSymbol::stop
                                    : sms::ui::dpf::ControlSymbol::play,
        ctx.state.anyPlaybackActive, colors.controlAccent, hovered(ctx, InteractiveType::playStop));
    sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::playOnSelect,
                              editorAuditionLabel(ctx.state.auditionMode),
                              editorAuditionPressed(ctx.state.auditionMode), colors.controlAccent,
                              hovered(ctx, InteractiveType::playOnSelect));
}

} // namespace

void drawSampleEditorView(const ViewRenderContext& ctx)
{
    drawSampleEditor(ctx);
    drawEditorPadPanel(ctx);
}

} // namespace midichopper::ui::rendering
