#include "Audio/WaveformSummary.hpp"
#include "ContextMenu.hpp"
#include "ChopEditor.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "Interaction.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "LevelMeter.hpp"
#include "PadLayout.hpp"
#include "UI/Geometry.hpp"
#include "WaveformEditor.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void padLayouts()
{
    const sms::ui::BankedPadLayout sixteen(0);
    check(sixteen.visiblePadCount() == 16 && sixteen.columns() == 4 && sixteen.rows() == 4,
          "four-by-four arrangement");
    check(sixteen.visualIndex(0) == 12 && sixteen.localIndex(12) == 0,
          "pad numbering starts at the lower-left");

    const sms::ui::BankedPadLayout twelve(1);
    check(twelve.visiblePadCount() == 12 && twelve.columns() == 3 && twelve.rows() == 4,
          "three-by-four arrangement");
    check(twelve.visualIndex(11) == 2 && twelve.localIndex(2) == 11,
          "three-column arrangement is reversible");

    const sms::ui::BankedPadLayout eight(2);
    const auto grid = eight.grid({10.0f, 20.0f, 400.0f, 200.0f}, 10.0f);
    check(eight.visiblePadCount() == 8 && eight.columns() == 4 && eight.rows() == 2,
          "four-by-two arrangement");
    check(grid.hit({15.0f, 25.0f}) == 0 && eight.localIndex(0) == 4,
          "grid hit testing composes with pad mapping");
}

void hamburgerMenuGeometry()
{
    namespace menu = midichopper::ui::layout;
    const auto lastLayout = menu::menuOption(2);
    const auto firstMidiMode = menu::midiBankModeOption(0);
    const auto lastMidiMode = menu::midiBankModeOption(1);
    check(firstMidiMode.y > lastLayout.y + lastLayout.height,
          "MIDI bank choices follow pad-layout choices without overlap");
    check(menu::menuPanel.contains({firstMidiMode.x + firstMidiMode.width * 0.5f,
                                    firstMidiMode.y + firstMidiMode.height * 0.5f}) &&
          menu::menuPanel.contains({lastMidiMode.x + lastMidiMode.width * 0.5f,
                                    lastMidiMode.y + lastMidiMode.height * 0.5f}),
          "MIDI bank choices remain inside the hamburger panel");
}

void contextMenuGeometry()
{
    const sms::ui::Rect canvas{0.0f, 0.0f, 960.0f, 680.0f};
    const sms::ui::ContextMenuGeometry middle({200.0f, 300.0f}, 2, canvas);
    check(middle.bounds().x == 200.0f && middle.bounds().y == 300.0f,
          "context menu preserves an in-bounds anchor");
    check(middle.hit({middle.item(0).x + 1.0f, middle.item(0).y + 1.0f}) == 0 &&
          middle.hit({middle.item(1).x + 1.0f, middle.item(1).y + 1.0f}) == 1,
          "context menu items have stable hit identities");
    check(middle.hit({0.0f, 0.0f}) == -1,
          "context menu rejects points outside its items");

    const sms::ui::ContextMenuGeometry edge({955.0f, 675.0f}, 2, canvas);
    check(edge.bounds().x + edge.bounds().width <= canvas.x + canvas.width &&
          edge.bounds().y + edge.bounds().height <= canvas.y + canvas.height,
          "context menu clamps to the logical canvas");

    sms::ui::HoverState hover;
    const auto item = midichopper::ui::target(midichopper::ui::InteractiveType::pad, 0);
    check(hover.target() == sms::ui::kNoInteractiveTarget && hover.update(item) &&
          !hover.update(item) && hover.clear() && !hover.clear(),
          "hover state reports only target transitions");
}

sms::ui::Point center(const sms::ui::Rect bounds)
{
    return {bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f};
}

void interactionTargets()
{
    namespace interaction = midichopper::ui;
    namespace layout = midichopper::ui::layout;

    sms::dsp::SamplePlaybackSettings settings;
    sms::audio::WaveformSummary waveform;
    const auto envelope = sms::ui::waveform::envelopeGeometry(
        layout::envelopeGraph, waveform, settings);
    interaction::InteractionContext context;
    context.padLayout = 0;
    context.editorSettings = &settings;
    context.envelope = &envelope;

    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::menuButton), context),
              interaction::InteractiveType::menuButton),
          "hamburger button resolves to one hover target");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::oneShotMode), context),
              interaction::InteractiveType::oneShotMode) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::voiceLimit), context),
              interaction::InteractiveType::voiceLimit),
          "play mode exposes only its playback controls");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::monitor), context),
              interaction::InteractiveType::monitor) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(
                  center(layout::finalizeAction), context),
              interaction::InteractiveType::finalizeAction),
          "play mode resolves bottom-anchored monitor and actions");
    check(!interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::fixedLength), context),
              interaction::InteractiveType::fixedLength),
          "play mode does not expose fixed capture length");

    const sms::ui::BankedPadLayout pads(0);
    const int localPad = 0;
    const auto padCell = pads.grid(layout::mainPadBounds, 10.0f).cell(
        pads.visualIndex(localPad));
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(padCell), context),
              interaction::InteractiveType::pad, localPad),
          "main pad target preserves the local pad identity");
    context.captureActive = true;
    check(!interaction::interactiveTargetAt(center(padCell), context).valid(),
          "inactive capture pad does not advertise a click that has no effect");
    context.captureActive = false;

    context.menuOpen = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::menuOption(1)), context),
              interaction::InteractiveType::menuLayout, 1),
          "open hamburger menu exposes its option");
    check(!interaction::interactiveTargetAt(center(layout::fixedLength), context).valid(),
          "open hamburger menu blocks underlying controls");
    context.menuOpen = false;

    context.armed = true;
    check(!interaction::interactiveTargetAt(center(layout::openEditor), context).valid(),
          "disabled editor button is not hoverable");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::sequentialMode), context),
              interaction::InteractiveType::sequentialMode) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::preRoll(false)), context),
              interaction::InteractiveType::preRoll),
          "sequential capture exposes capture mode and compact pre-roll");
    check(!interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::oneShotMode), context),
              interaction::InteractiveType::oneShotMode) &&
          !interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::voiceLimit), context),
              interaction::InteractiveType::voiceLimit),
          "arm mode does not expose playback controls");
    context.fixedCapture = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::fixedLength), context),
              interaction::InteractiveType::fixedLength) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::preRoll(true)), context),
              interaction::InteractiveType::preRoll),
          "fixed capture inserts length before pre-roll");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::monitor), context),
              interaction::InteractiveType::monitor) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(
                  center(layout::clearAction), context),
              interaction::InteractiveType::clearAction),
          "fixed capture resolves bottom-anchored monitor and actions");
    check(layout::preRoll(false).y == layout::fixedLength.y &&
          layout::preRoll(true).y > layout::fixedLength.y + layout::fixedLength.height,
          "pre-roll occupies the fixed-length slot only when length is hidden");
    context.armed = false;
    context.fixedCapture = false;
    check(layout::sidePanel.contains(center(layout::playOnSelect)) &&
              !interaction::isTarget(
                  interaction::interactiveTargetAt(center(layout::playOnSelect), context),
                  interaction::InteractiveType::playOnSelect),
          "play-on-select stays inside the side panel and is hidden outside the editor");
    context.editorMode = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::playOnSelect), context),
              interaction::InteractiveType::playOnSelect),
          "sample editor exposes the play-on-select toggle");
    const auto waveformTarget = interaction::interactiveTargetAt(
        {layout::editorWaveform.x + 1.0f, layout::editorWaveform.y + 20.0f}, context);
    check(interaction::isTarget(waveformTarget, interaction::InteractiveType::regionHandle) &&
              waveformTarget.index == static_cast<int>(sms::ui::waveform::EditTarget::regionStart),
          "waveform hover identifies the nearest editable cut handle");

    context.editorMode = false;
    context.chopEditorMode = true;
    std::array<sms::audio::WaveformSummary, 3> chopWaveforms{};
    for (auto& waveform : chopWaveforms) {
        waveform.frames = 100U;
        waveform.sampleRate = 48000.0;
    }
    std::array<std::int64_t, 2> chopOffsets{};
    context.chopWaveforms = chopWaveforms;
    context.chopOffsets = chopOffsets;
    context.chopReady = true;
    const auto cut = interaction::interactiveTargetAt(
        center(interaction::chop::boundaryHandle(
            layout::chopWaveform, chopWaveforms, chopOffsets, 0)), context);
    check(interaction::isTarget(cut, interaction::InteractiveType::chopBoundary, 0) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopPadButton(1)), context),
              interaction::InteractiveType::chopPadPreview, 1),
          "cut-point editor exposes both handles and three-pad raw preview");
    chopWaveforms[0].frames = 0U;
    check(!interaction::interactiveTargetAt(center(layout::chopPadButton(0)), context).valid(),
          "an empty proposed slice has no preview target");
    chopOffsets[0] = 25;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopPadButton(0)), context),
              interaction::InteractiveType::chopPadPreview, 0),
          "moving an edge cut enables preview for the newly filled pad");
    context.chopEditorMode = false;

    const std::array<bool, 2> enabled{false, true};
    context.padContextMenuOpen = true;
    context.padContextMenu = sms::ui::ContextMenuGeometry(
        {200.0f, 200.0f}, 2, layout::contentBounds);
    context.padContextMenuEnabled = enabled;
    check(!interaction::interactiveTargetAt(center(context.padContextMenu.item(0)), context).valid(),
          "disabled context item is not hoverable");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(context.padContextMenu.item(1)), context),
              interaction::InteractiveType::padContextItem, 1),
          "enabled context item takes overlay hover priority");
}

void padPressTracking()
{
    midichopper::ui::PadPressTracker press;
    check(press.pad() == -1 && press.press(0, 36) == -1 && press.pad() == 0,
          "first UI pad press has no previous note to release");
    check(press.press(1, 37) == 36 && press.pad() == 1,
          "replacement UI pad press releases the previous MIDI note");
    check(press.release() == 37 && press.pad() == -1 && press.release() == -1,
          "UI pad release clears the active press exactly once");
}

void wheelAdjustment()
{
    check(std::abs(sms::ui::wheelAdjustedValue(1.0f, 1.0f, 0.1f, 0.01f, 30.0f) -
                   1.1f) < 1.0e-6f,
          "wheel up increases a continuous control by one step");
    check(std::abs(sms::ui::wheelAdjustedValue(1.0f, -0.25f, 0.1f, 0.01f, 30.0f) -
                   0.9f) < 1.0e-6f,
          "smooth wheel input still applies one predictable step");
    check(sms::ui::wheelAdjustedValue(16.0f, 1.0f, 1.0f, 1.0f, 16.0f, true) == 16.0f &&
              sms::ui::wheelAdjustedValue(1.0f, -1.0f, 1.0f, 1.0f, 16.0f, true) == 1.0f,
          "wheel adjustment clamps at both boundaries");
    check(sms::ui::wheelAdjustedValue(7.0f, 1.0f, 1.0f, 1.0f, 16.0f, true) == 8.0f,
          "integer wheel controls stay integral");
    check(sms::ui::wheelAdjustedValue(0.5f, 0.0f, 0.1f, 0.0f, 1.0f) == 0.5f,
          "zero wheel delta does not change a control");
}

void levelMeterGeometry()
{
    namespace meter = sms::ui::meter;
    check(meter::activeSegmentCount(0.0f) == 0U,
          "silence lights no meter segments");
    check(meter::activeSegmentCount(1.0f) == meter::defaultSegmentCount,
          "full scale lights every meter segment");
    check(meter::activeSegmentCount(2.0f) == meter::defaultSegmentCount &&
          meter::activeSegmentCount(INFINITY) == meter::defaultSegmentCount &&
          meter::activeSegmentCount(NAN) == 0U,
          "meter counts clamp overloads and reject non-finite values");
    check(std::abs(meter::amplitudeToDb(0.1f) + 20.0f) < 1.0e-5f,
          "meter uses logarithmic decibel mapping");
    check(!meter::visibleLevelChanged(0.40f, 0.41f) &&
          meter::visibleLevelChanged(0.40f, 0.80f),
          "meter repaints only when a visible LED boundary changes");

    const meter::StereoGeometry narrow({2.0f, 10.0f, 24.0f, 510.0f});
    const auto narrowLeft = narrow.channel(0U);
    const auto narrowRight = narrow.channel(1U);
    check(narrowLeft.width > 0.0f && narrowRight.x > narrowLeft.x + narrowLeft.width,
          "narrow stereo meter columns do not overlap");
    const auto bottom = narrow.segment(0U, 0U);
    const auto top = narrow.segment(0U, meter::defaultSegmentCount - 1U);
    check(top.y >= 10.0f && bottom.y + bottom.height <= 520.0f && top.y < bottom.y,
          "vertical meter segments stay inside tall bounds");

    const meter::StereoGeometry wide({5.0f, 7.0f, 80.0f, 120.0f}, 12U);
    const auto wideLeft = wide.channel(0U);
    const auto wideRight = wide.channel(1U);
    check(wideLeft.width > narrowLeft.width && wideRight.x + wideRight.width <= 85.0f,
          "meter geometry adapts to wider and shorter bounds");
    const meter::StereoGeometry tiny({0.0f, 0.0f, 8.0f, 10.0f});
    const auto tinyTop = tiny.segment(0U, meter::defaultSegmentCount - 1U);
    const auto tinyBottom = tiny.segment(0U, 0U);
    check(tinyTop.y >= 0.0f && tinyTop.height > 0.0f &&
          tinyBottom.y + tinyBottom.height <= 10.0f,
          "meter segments stay inside genuinely short bounds");
    check(meter::segmentZone(19U) == meter::Zone::green &&
          meter::segmentZone(20U) == meter::Zone::yellow &&
          meter::segmentZone(26U) == meter::Zone::red,
          "LED zones transition at minus 18 and minus 6 dB");
}

void waveformGeometry()
{
    sms::audio::WaveformSummary summary;
    summary.frames = 48000U;
    summary.sampleRate = 48000.0;
    summary.minimum[0] = -0.25f;
    summary.maximum[0] = 0.5f;
    sms::dsp::SamplePlaybackSettings settings;
    settings.start = 0.25f;
    settings.end = 0.75f;
    settings.attackSeconds = 0.1f;
    settings.decaySeconds = 0.1f;
    settings.sustainLevel = 0.5f;
    settings.releaseSeconds = 0.1f;

    check(std::abs(sms::ui::waveform::displayScale(summary) - 2.0f) < 1.0e-6f,
          "waveform display normalizes its largest peak");
    check(std::abs(sms::ui::waveform::regionDuration(summary, settings) - 0.5f) < 1.0e-6f,
          "selected region duration uses frame count and sample rate");

    const auto geometry = sms::ui::waveform::envelopeGeometry(
        {0.0f, 0.0f, 250.0f, 120.0f}, summary, settings);
    check(geometry.attackX < geometry.decayX && geometry.decayX < geometry.releaseX,
          "envelope stages remain ordered");
    check(sms::ui::waveform::hitEnvelopeHandle(
              {geometry.attackHandleX, geometry.top}, geometry) ==
              sms::ui::waveform::EditTarget::attackNode,
          "attack handle can be hit");
    check(sms::ui::waveform::envelopeGain(settings.start, summary, settings) == 0.0f,
          "envelope begins at zero");
    check(std::abs(sms::ui::waveform::envelopeGain(settings.end, summary, settings)) < 1.0e-6f,
          "scheduled release reaches zero at the cut end");

    sms::ui::waveform::updateRegion(
        settings, sms::ui::waveform::EditTarget::regionStart, 60.0f,
        {0.0f, 0.0f, 100.0f, 40.0f});
    check(std::abs(settings.start - 0.6f) < 1.0e-6f,
          "region dragging uses the supplied waveform bounds");

    const sms::ui::Rect slider{20.0f, 10.0f, 280.0f, 24.0f};
    const auto track = sms::ui::waveform::envelopeSliderTrack(slider);
    sms::ui::waveform::updateEnvelopeSlider(
        settings, sms::ui::waveform::EditTarget::attackSlider,
        track.x + track.width * 0.5f, slider);
    check(std::abs(settings.attackSeconds - 1.25f) < 1.0e-6f,
          "envelope slider drawing and interaction share one mapping");
}

void chopEditorGeometry()
{
    std::array<sms::audio::WaveformSummary, 3> waveforms{};
    for (std::size_t index = 0; index < waveforms.size(); ++index) {
        waveforms[index].pad = static_cast<std::uint32_t>(index);
        waveforms[index].frames = 100U;
        waveforms[index].sampleRate = 1000.0;
        waveforms[index].minimum.fill(-static_cast<float>(index + 1U) * 0.1f);
        waveforms[index].maximum.fill(static_cast<float>(index + 1U) * 0.1f);
    }
    const std::array<std::int64_t, 2> offsets{};
    const sms::ui::Rect bounds{0.0f, 0.0f, 300.0f, 100.0f};
    const auto first = midichopper::ui::chop::boundaryHandle(
        bounds, waveforms, offsets, 0);
    const auto second = midichopper::ui::chop::boundaryHandle(
        bounds, waveforms, offsets, 1);
    check(midichopper::ui::chop::boundaryAt(
              center(first), bounds, waveforms, offsets) == 0 &&
          midichopper::ui::chop::boundaryAt(
              center(second), bounds, waveforms, offsets) == 1,
          "both cut handles follow the original three-pad boundaries");
    check(midichopper::ui::chop::clampBoundaryOffset(waveforms, offsets, 0, -200) == -99 &&
          midichopper::ui::chop::clampBoundaryOffset(waveforms, offsets, 0, 200) == 99,
          "chop boundary retains at least one frame in each neighbor");

    auto moved = offsets;
    moved[0] = 50;
    check(midichopper::ui::chop::adjustedFrames(waveforms, moved, 0) == 150U &&
          midichopper::ui::chop::adjustedFrames(waveforms, moved, 1) == 50U,
          "rolling edit transfers duration between adjacent pads");
    check(midichopper::ui::chop::adjustedStartFrame(waveforms, moved, 1) == 150U,
          "middle raw preview starts at the pending first cut");
    const auto combined = midichopper::ui::chop::combinedWaveform(waveforms);
    check(combined.frames == 300U && combined.maximum.front() < 0.2f &&
          combined.maximum.back() > 0.29f,
          "three summaries form one ordered waveform");
    check(std::abs(midichopper::ui::chop::sourceFrameForPlayhead(
              waveforms, 1, 0.25f) - 125.0) < 1.0e-6,
          "raw playhead maps into the combined waveform");

    auto emptyLeft = waveforms;
    emptyLeft[0].frames = 0U;
    check(midichopper::ui::chop::ready(emptyLeft) &&
          midichopper::ui::chop::boundaryX(bounds, emptyLeft, offsets, 0) == bounds.x,
          "empty left neighbor is ready with its cut at the far left");
    std::array<std::int64_t, 2> fillLeft{{25, 0}};
    check(midichopper::ui::chop::adjustedFrames(emptyLeft, fillLeft, 0) == 25U &&
          midichopper::ui::chop::adjustedFrames(emptyLeft, fillLeft, 1) == 75U &&
          midichopper::ui::chop::combinedWaveform(emptyLeft).sampleRate == 1000.0,
          "moving the left edge cut creates a slice using the shared audio rate");

    auto emptyRight = waveforms;
    emptyRight[2].frames = 0U;
    check(midichopper::ui::chop::ready(emptyRight) &&
          midichopper::ui::chop::boundaryX(bounds, emptyRight, offsets, 1) ==
              bounds.x + bounds.width,
          "empty right neighbor is ready with its cut at the far right");
    std::array<std::int64_t, 2> fillRight{{0, -25}};
    check(midichopper::ui::chop::adjustedFrames(emptyRight, fillRight, 1) == 75U &&
          midichopper::ui::chop::adjustedFrames(emptyRight, fillRight, 2) == 25U,
          "moving the right edge cut creates a slice in the empty pad");

    emptyRight[2].sampleRate = 0.0;
    check(!midichopper::ui::chop::ready(emptyRight),
          "editor waits for an empty neighbor waveform response");
}

} // namespace

int main()
{
    padLayouts();
    hamburgerMenuGeometry();
    contextMenuGeometry();
    interactionTargets();
    padPressTracking();
    wheelAdjustment();
    levelMeterGeometry();
    waveformGeometry();
    chopEditorGeometry();
    std::cout << "UI geometry tests passed\n";
}
