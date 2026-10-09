#include "Audio/WaveformSummary.hpp"
#include "ContextMenu.hpp"
#include "ChopEditor.hpp"
#include "ChopEditorSession.hpp"
#include "ChopEditorController.hpp"
#include "EditorSnapshotSession.hpp"
#include "MixerValueEntry.hpp"
#include "PlaybackIndicator.hpp"
#include "WaveformDetailSession.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "Interaction.hpp"
#include "HelpLinks.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "LevelMeter.hpp"
#include "PadLayout.hpp"
#include "UI/Geometry.hpp"
#include "WaveformEditor.hpp"
#include "WaveformViewport.hpp"
#include "../src/plugin/DistrhoPluginInfo.h"

#include <cmath>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

void wideCanvasGeometry()
{
    namespace layout = midichopper::ui::layout;
    const sms::ui::Rect canvas{0.0f, 0.0f,
        static_cast<float>(layout::canvasWidth), static_cast<float>(layout::canvasHeight)};
    const auto inside = [](const sms::ui::Rect outer, const sms::ui::Rect inner) {
        return inner.x >= outer.x && inner.y >= outer.y &&
            inner.x + inner.width <= outer.x + outer.width &&
            inner.y + inner.height <= outer.y + outer.height;
    };
    check(layout::canvasWidth * 9U == layout::canvasHeight * 16U &&
          layout::minimumWidth * 9U == layout::minimumHeight * 16U,
          "logical and minimum window sizes use a 16:9 shape");
    check(DISTRHO_UI_DEFAULT_WIDTH == layout::canvasWidth &&
          DISTRHO_UI_DEFAULT_HEIGHT == layout::canvasHeight,
          "format metadata agrees with the logical canvas");
    check(inside(canvas, layout::inputMeter) && inside(canvas, layout::outputMeter) &&
          inside(layout::contentBounds, layout::mainPanel) &&
          inside(layout::contentBounds, layout::sidePanel) &&
          inside(layout::contentBounds, layout::footer) &&
          layout::mainPanel.x + layout::mainPanel.width < layout::sidePanel.x,
          "meters, panels, and footer fit without overlap");
    check(inside(layout::mainPanel, layout::mainPadBounds) &&
          inside(layout::mainPanel, layout::editorWaveform) &&
          inside(layout::mainPanel, layout::chopWaveform) &&
          inside(layout::mainPanel, layout::chopPadButton(2)) &&
          inside(layout::mainPanel, layout::editorSlider(3)),
          "expanded main-view controls remain inside the panel");
    check(inside(layout::sidePanel, layout::editorPadBounds) &&
          inside(layout::sidePanel, layout::monitor) &&
          inside(layout::sidePanel, layout::chopNext),
          "side-view controls remain inside the side panel");
    check(layout::inputMeter.y + layout::inputMeter.height * 0.5f ==
              layout::canvasHeight * 0.5f &&
          layout::outputMeter.y == layout::inputMeter.y &&
          layout::inputMeter.x == layout::canvasWidth -
              layout::outputMeter.x - layout::outputMeter.width,
          "input and output faders are centered vertically at symmetric edges");
    check(layout::openEditor.x == layout::closeEditor.x &&
          layout::openEditor.y == layout::closeEditor.y &&
          layout::openEditor.width == layout::closeEditor.width &&
          layout::openEditor.height == layout::closeEditor.height &&
          layout::modeToggle.width == layout::openEditor.width,
          "editor navigation shares exact geometry and ARM spans the control width");
}

void hamburgerMenuGeometry()
{
    namespace menu = midichopper::ui::layout;
    const auto lastLayout = menu::menuOption(2);
    const auto firstMidiMode = menu::midiBankModeOption(0);
    const auto lastMidiMode = menu::midiBankModeOption(1);
    const auto firstLink = menu::menuLinkOption(0);
    const auto lastLink = menu::menuLinkOption(1);
    check(firstMidiMode.y > lastLayout.y + lastLayout.height,
          "MIDI bank choices follow pad-layout choices without overlap");
    check(menu::menuPanel.contains({firstMidiMode.x + firstMidiMode.width * 0.5f,
                                    firstMidiMode.y + firstMidiMode.height * 0.5f}) &&
          menu::menuPanel.contains({lastMidiMode.x + lastMidiMode.width * 0.5f,
                                    lastMidiMode.y + lastMidiMode.height * 0.5f}),
          "MIDI bank choices remain inside the hamburger panel");
    check(firstLink.y > lastMidiMode.y + lastMidiMode.height &&
          menu::menuPanel.contains({firstLink.x + firstLink.width * 0.5f,
                                    firstLink.y + firstLink.height * 0.5f}) &&
          menu::menuPanel.contains({lastLink.x + lastLink.width * 0.5f,
                                    lastLink.y + lastLink.height * 0.5f}),
          "browser links follow bank choices inside the hamburger panel");
    check(midichopper::ui::onlineHelpUrl("main") ==
              "https://github.com/sirsipe/SMS-Plugins/tree/main/SMS-AnvilSampler" &&
          midichopper::ui::onlineHelpUrl("SMS-AnvilSampler-v1.2.3") ==
              "https://github.com/sirsipe/SMS-Plugins/tree/SMS-AnvilSampler-v1.2.3/SMS-AnvilSampler" &&
          midichopper::ui::kIssueUrl ==
              "https://github.com/sirsipe/SMS-Plugins/issues",
          "hamburger links target the project README and issue tracker");
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
    const sms::ui::ContextMenuGeometry expanded({955.0f, 675.0f}, 9, canvas);
    check(expanded.hit({expanded.item(8).x + 1.0f, expanded.item(8).y + 1.0f}) == 8 &&
          expanded.bounds().x + expanded.bounds().width <= canvas.x + canvas.width &&
          expanded.bounds().y + expanded.bounds().height <= canvas.y + canvas.height,
          "expanded pad menu remains fully clamped to the canvas");

    constexpr std::array groupedKinds{
        sms::ui::ContextMenuItemKind::action,
        sms::ui::ContextMenuItemKind::separator,
        sms::ui::ContextMenuItemKind::action,
    };
    const sms::ui::ContextMenuGeometry grouped(
        {200.0f, 200.0f}, groupedKinds, canvas);
    const auto separator = grouped.item(1);
    const auto followingAction = grouped.item(2);
    check(grouped.item(1).height < grouped.item(0).height &&
          grouped.hit({separator.x + separator.width * 0.5f,
                       separator.y + separator.height * 0.5f}) == -1 &&
          grouped.hit({followingAction.x + followingAction.width * 0.5f,
                       followingAction.y + followingAction.height * 0.5f}) == 2,
          "context-menu separators are compact, inert, and preserve row identities");

    std::array<sms::ui::ContextMenuItemKind, 15> padMenuKinds{};
    padMenuKinds.fill(sms::ui::ContextMenuItemKind::action);
    for (const std::size_t separatorRow : {2U, 6U, 10U, 13U})
        padMenuKinds[separatorRow] = sms::ui::ContextMenuItemKind::separator;
    const sms::ui::ContextMenuGeometry padMenu(
        {955.0f, 675.0f}, padMenuKinds, canvas);
    const auto colorSubmenu = sms::ui::ContextMenuGeometry::submenu(
        padMenu, 1, 7, canvas);
    check(colorSubmenu.bounds().x + colorSubmenu.bounds().width <= padMenu.bounds().x + 2.0f &&
          colorSubmenu.bounds().y >= canvas.y &&
          colorSubmenu.bounds().y + colorSubmenu.bounds().height <= canvas.y + canvas.height &&
          colorSubmenu.hit({colorSubmenu.item(6).x + 1.0f,
                            colorSubmenu.item(6).y + 1.0f}) == 6,
          "color submenu opens left at the canvas edge with every color clickable");
    const sms::ui::ContextMenuGeometry innerMenu(
        {200.0f, 200.0f}, padMenuKinds, canvas);
    const auto rightSubmenu = sms::ui::ContextMenuGeometry::submenu(
        innerMenu, 1, 7, canvas);
    check(rightSubmenu.bounds().x >= innerMenu.bounds().x + innerMenu.bounds().width - 2.0f &&
          rightSubmenu.item(0).y == innerMenu.item(1).y,
          "color submenu opens right and aligns with its parent row");

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

void spaceKeyTracking()
{
    using midichopper::ui::SpaceAction;
    midichopper::ui::SpaceKeyTracker key;
    check(key.update(true, false, false) == SpaceAction::playStop &&
          key.update(true, false, false) == SpaceAction::none &&
          key.update(false, false, false) == SpaceAction::none,
          "Space plays or stops once per press and release has no action");
    check(key.update(true, false, true) == SpaceAction::chop &&
          key.update(true, false, true) == SpaceAction::none,
          "armed Space chops once without repeating while held");
    check(key.update(true, false, false) == SpaceAction::none,
          "mode changes while Space is held do not retrigger");
    key.reset();
    check(key.update(true, true, false) == SpaceAction::none &&
          key.update(true, false, false) == SpaceAction::none,
          "Space blocked by an overlay stays blocked until release");
    static_cast<void>(key.update(false, false, false));
    check(key.update(true, false, false) == SpaceAction::playStop,
          "Space works again after the blocked press is released");
    key.reset();
    check(key.update(true, false, true) == SpaceAction::chop,
          "focus loss clears a held Space even if its release was missed");
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
              interaction::interactiveTargetAt(center(layout::modeToggle), context),
              interaction::InteractiveType::modeToggle) &&
          interaction::isTarget(interaction::interactiveTargetAt({1048.0f, 166.0f}, context),
              interaction::InteractiveType::modeToggle),
          "ARM uses a single full-width toggle");
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
          !interaction::interactiveTargetAt(center(layout::finalizeAction), context).valid(),
          "play mode keeps the bottom monitor and hides chop actions");
    for (int knob = 1; knob < 7; ++knob) {
        check(interaction::isTarget(
                  interaction::interactiveTargetAt(center(layout::globalMixerKnob(knob)), context),
                  interaction::InteractiveType::globalMixerKnob, knob),
              "main view exposes each global mixer knob");
        if (knob != 5) check(interaction::isTarget(
                  interaction::interactiveTargetAt(
                      center(layout::globalMixerValueLabel(knob)), context),
                  interaction::InteractiveType::globalMixerValueLabel, knob),
              "main view exposes each global mixer value label");
    }
    check(layout::globalMixerKnob(0).width == 0.0f &&
          layout::globalMixerKnob(1).y < layout::globalMixerKnob(2).y &&
          layout::globalMixerKnob(6).y == layout::globalMixerKnob(1).y &&
          layout::globalMixerKnob(5).y > layout::globalMixerKnob(4).y +
              layout::globalMixerKnob(4).height,
          "global controls use the requested two rows and lower slope slider");
    const auto globalSlopeTrack = layout::slopeTrack(layout::globalMixerKnob(5));
    check(globalSlopeTrack.x == layout::globalMixerKnob(3).x +
              layout::globalMixerKnob(3).width * 0.5f - 24.0f &&
          globalSlopeTrack.x + globalSlopeTrack.width ==
              layout::globalMixerKnob(4).x +
                  layout::globalMixerKnob(4).width * 0.5f + 24.0f &&
          globalSlopeTrack.y < layout::globalMixerKnob(4).y +
              layout::globalMixerKnob(4).height + 20.0f,
          "global slope track aligns with knob artwork and sits close below");
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
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::menuLinkOption(0)), context),
              interaction::InteractiveType::menuLink, 0) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::menuLinkOption(1)), context),
              interaction::InteractiveType::menuLink, 1),
          "open hamburger menu exposes both browser actions");
    check(!interaction::interactiveTargetAt(center(layout::fixedLength), context).valid(),
          "open hamburger menu blocks underlying controls");
    context.menuOpen = false;

    context.armed = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::modeToggle), context),
              interaction::InteractiveType::modeToggle),
          "same mode toggle remains available while armed");
    for (int knob = 1; knob < 7; ++knob) {
        const auto knobTarget = interaction::interactiveTargetAt(
            center(layout::globalMixerKnob(knob)), context);
        check(!knobTarget.valid(), "arm hides every global mixer control");
        if (knob != 5) {
            const auto labelTarget = interaction::interactiveTargetAt(
                center(layout::globalMixerValueLabel(knob)), context);
            check(!labelTarget.valid(),
                  "hidden arm mixer labels cannot be edited or wheeled");
        }
    }
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
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::mainPlayStop), context),
              interaction::InteractiveType::playStop),
          "main view exposes Play/Stop between max voices and mixer");
    check(layout::sidePanel.contains(center(layout::playOnSelect)) &&
              !interaction::isTarget(
                  interaction::interactiveTargetAt(center(layout::playOnSelect), context),
                  interaction::InteractiveType::playOnSelect),
          "play-on-select stays inside the side panel and is hidden outside the editor");
    context.editorMode = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::editorPlayStop), context),
              interaction::InteractiveType::playStop) &&
          layout::editorPlayStop.x + layout::editorPlayStop.width < layout::playOnSelect.x,
          "editor Play/Stop sits left of Play on Select");
    context.waveformViewport.reset(8000U);
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::editorZoom), context),
              interaction::InteractiveType::waveformZoom) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::editorScroll), context),
              interaction::InteractiveType::waveformScroll),
          "sample editor exposes zoom and scroll controls");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::playOnSelect), context),
              interaction::InteractiveType::playOnSelect),
          "sample editor exposes the play-on-select toggle");
    const auto waveformTarget = interaction::interactiveTargetAt(
        {layout::editorWaveform.x + 1.0f, layout::editorWaveform.y + 20.0f}, context);
    check(interaction::isTarget(waveformTarget, interaction::InteractiveType::regionHandle) &&
              waveformTarget.index == static_cast<int>(sms::ui::waveform::EditTarget::regionStart),
          "waveform hover identifies the nearest editable cut handle");
    for (int slider = 0; slider < 7; ++slider) {
        check(interaction::isTarget(
                  interaction::interactiveTargetAt(center(layout::mixerKnob(slider)), context),
                  interaction::InteractiveType::mixerKnob, slider),
              "sample editor exposes each mixer knob");
        if (slider != 5) check(interaction::isTarget(
                  interaction::interactiveTargetAt(
                      center(layout::mixerValueLabel(slider)), context),
                  interaction::InteractiveType::mixerValueLabel, slider),
              "sample editor exposes each mixer value label");
    }
    check(layout::mixerKnob(0).y == layout::mixerKnob(1).y &&
          layout::mixerKnob(1).y == layout::mixerKnob(6).y &&
          layout::mixerKnob(6).y == layout::mixerKnob(2).y &&
          layout::mixerKnob(2).y == layout::mixerKnob(3).y &&
          layout::mixerKnob(3).y == layout::mixerKnob(4).y &&
          layout::mixerKnob(1).x + layout::mixerKnob(1).width <
              layout::padMixerSeparator.x &&
          layout::padMixerSeparator.x < layout::mixerKnob(6).x &&
          layout::mixerKnob(5).y > layout::mixerKnob(4).y +
              layout::mixerKnob(4).height &&
          layout::mixerKnob(5).y + layout::mixerKnob(5).height <=
              layout::mainPanel.y + layout::mainPanel.height,
          "pad controls share a row with a vertical divider and lower slope slider");
    check(!layout::envelopeGraph.contains(center(layout::mixerKnob(0))) &&
          !layout::editorSlider(0).contains(center(layout::mixerKnob(2))),
          "mixer controls remain separate from ADSR controls");

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
    context.waveformViewport.reset(300U);
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopZoom), context),
              interaction::InteractiveType::waveformZoom) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopScroll), context),
              interaction::InteractiveType::waveformScroll),
          "cut editor exposes zoom and scroll controls");
    const auto cut = interaction::interactiveTargetAt(
        center(interaction::chop::boundaryHandle(
            layout::chopWaveform, chopWaveforms, chopOffsets, 0)), context);
    check(interaction::isTarget(cut, interaction::InteractiveType::chopBoundary, 0) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopPadButton(1)), context),
              interaction::InteractiveType::chopPadPreview, 1),
          "cut-point editor exposes both handles and three-pad raw preview");
    context.chopSplitMode = true;
    check(!interaction::interactiveTargetAt(
              center(interaction::chop::boundaryHandle(
                  layout::chopWaveform, chopWaveforms, chopOffsets, 1)), context).valid() &&
          !interaction::interactiveTargetAt(
              center(layout::chopPadButton(2)), context).valid() &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopPadButton(1)), context),
              interaction::InteractiveType::chopPadPreview, 1),
          "split editor exposes one boundary and two raw previews");
    context.chopSplitMode = false;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopExit), context),
              interaction::InteractiveType::chopExit) &&
          !interaction::interactiveTargetAt(
              {layout::chopExit.x + 1.0f, layout::chopExit.y + 1.0f}, context).valid() &&
          !interaction::interactiveTargetAt(center(layout::chopApply), context).valid() &&
          !interaction::interactiveTargetAt(center(layout::chopPrevious), context).valid() &&
          !interaction::interactiveTargetAt(center(layout::chopNext), context).valid(),
          "cut editor exposes Exit while disabled Apply and navigation are inert");
    check(layout::chopArrowTipX(layout::chopPrevious, false) <
              layout::chopArrowTailX(layout::chopPrevious, false) &&
          layout::chopArrowTipX(layout::chopNext, true) >
              layout::chopArrowTailX(layout::chopNext, true),
          "cut editor arrow glyphs point toward their navigation direction");
    context.chopApplyEnabled = true;
    context.chopPreviousEnabled = true;
    context.chopNextEnabled = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopApply), context),
              interaction::InteractiveType::chopApply) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopPrevious), context),
              interaction::InteractiveType::chopPrevious) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopNext), context),
              interaction::InteractiveType::chopNext),
          "enabled Apply and previous/next buttons resolve independently");
    context.chopApplying = true;
    context.chopApplyEnabled = false;
    context.chopPreviousEnabled = false;
    context.chopExitEnabled = false;
    context.chopNextEnabled = false;
    check(!interaction::interactiveTargetAt(center(layout::chopPadButton(1)), context).valid() &&
          !interaction::interactiveTargetAt(center(interaction::chop::boundaryHandle(
              layout::chopWaveform, chopWaveforms, chopOffsets, 0)), context).valid(),
          "cut handles and previews are inert while Apply is in flight");
    context.chopApplying = false;
    context.chopExitEnabled = true;
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
    context.padColorMenuOpen = true;
    context.padColorMenu = sms::ui::ContextMenuGeometry::submenu(
        context.padContextMenu, 1, 7, layout::contentBounds);
    context.padColorMenuItemCount = 7;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(context.padColorMenu.item(6)), context),
              interaction::InteractiveType::padColorItem, 6),
          "submenu choice takes overlay priority and keeps its own identity");
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

    check(!midichopper::ui::editorPadSelectionChanged(7, 7) &&
              midichopper::ui::editorPadSelectionChanged(7, 8),
          "sample editor reloads data only when pad selection changes");
}

void editorSnapshotCollection()
{
    sms::audio::WaveformSummary waveform;
    waveform.pad = 7U;
    waveform.frames = 48000U;
    sms::dsp::SamplePlaybackSettings playback;
    playback.start = 0.25f;
    sms::dsp::SampleMixerSettings mixer;
    mixer.pan = 0.5f;

    midichopper::ui::EditorSnapshotSession session;
    session.begin(7);
    const auto firstRequest = session.request();
    check(session.request().sequence == firstRequest.sequence,
          "sample snapshot retries retain their request identity");
    midichopper::plugin::EditorSnapshotReply reply;
    reply.request = firstRequest;
    reply.waveform = waveform;
    reply.playback = playback;
    reply.mixer = mixer;
    session.begin(7);
    const auto refreshed = session.request();
    check(refreshed.sequence != firstRequest.sequence && !session.accept(reply) && !session.readyFor(7),
          "same-pad refresh rejects coherent replies from its earlier baseline");
    reply.request = refreshed;
    reply.waveform.frames = 96000U;
    reply.playback.start = 0.5f;
    reply.mixer.pan = -0.25f;
    check(session.accept(reply) && session.readyFor(7) &&
          session.waveform().frames == 96000U && session.playback().start == 0.5f &&
          session.mixer().pan == -0.25f && !session.accept(reply),
          "matching integrated reply commits all controls once");
    session.complete();
    check(!session.pending() && !session.accept(reply), "completed sample requests ignore late retry replies");
    session.begin(8);
    check(!session.accept(reply) && !session.readyFor(8), "pad navigation rejects an earlier integrated reply");
    midichopper::ui::EditorSnapshotSession reopened;
    reopened.begin(8);
    check(reopened.request().sequence != session.request().sequence,
          "sample snapshot identities differ across UI instances");

    std::uint64_t sequence = 0U;
    auto detail = midichopper::ui::waveformDetailRequest({}, sequence, 7U, 1U, 100U, 200U);
    const auto retry = midichopper::ui::waveformDetailRequest(detail, sequence, 7U, 1U, 100U, 200U);
    check(retry.sequence == detail.sequence, "delayed detail retries preserve sequence");
    detail = midichopper::ui::waveformDetailRequest(retry, sequence, 7U, 1U, 200U, 300U);
    check(detail.sequence != retry.sequence, "changed detail frame range advances sequence");
    const auto newPad = midichopper::ui::waveformDetailRequest(detail, sequence, 8U, 1U, 200U, 300U);
    check(newPad.sequence != detail.sequence, "changed detail source advances sequence");
    ++sequence; // editor reset invalidates an otherwise identical range
    check(midichopper::ui::waveformDetailRequest(newPad, sequence, 8U, 1U, 200U, 300U).sequence != newPad.sequence,
          "editor refresh invalidates an identical detail range");
    std::uint64_t reopenedSequence = 0U;
    check(midichopper::ui::waveformDetailRequest({}, reopenedSequence, 8U, 1U, 200U, 300U).sequence != sequence,
          "reopened detail session cannot reuse a previous UI request identity");
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

void mainSliderDragging()
{
    namespace interaction = midichopper::ui;
    namespace layout = midichopper::ui::layout;
    namespace ranges = midichopper::plugin::parameterRanges;
    check(interaction::mainSliderValueAtX(layout::voiceLimit.x,
              layout::voiceLimit, ranges::maxVoices, true) == 1.0f &&
          interaction::mainSliderValueAtX(layout::voiceLimit.x +
              layout::voiceLimit.width, layout::voiceLimit,
              ranges::maxVoices, true) == 16.0f,
          "max voices drag spans its full integer range");
    check(interaction::mainSliderValueAtX(-100.0f, layout::fixedLength,
              ranges::fixedLengthSeconds) == ranges::fixedLengthSeconds.minimum &&
          interaction::mainSliderValueAtX(2000.0f, layout::fixedLength,
              ranges::fixedLengthSeconds) == ranges::fixedLengthSeconds.maximum,
          "fixed length drag clamps outside the slider");
    check(interaction::mainSliderValueAtX(layout::preRoll(true).x +
              layout::preRoll(true).width * 0.5f, layout::preRoll(true),
              ranges::preRollMs, true) == 50.0f,
          "pre-roll drag follows the fixed capture slider");
    const auto track = layout::levelFaderTrack(layout::levelFader(0));
    check(interaction::levelFaderValueAtY(track.y - 100.0f, layout::levelFader(0),
              ranges::monitorGainDb) == 12.0f &&
          interaction::levelFaderValueAtY(track.y + track.height + 100.0f,
              layout::levelFader(0), ranges::monitorGainDb) == -24.0f &&
          std::abs(interaction::levelFaderValueAtY(track.y + track.height / 3.0f,
              layout::levelFader(0), ranges::monitorGainDb)) < 1.0e-5f,
          "vertical level fader is louder upwards, clamps, and maps unity correctly");
}

void levelFaderTargets()
{
    namespace interaction = midichopper::ui;
    namespace layout = midichopper::ui::layout;
    interaction::InteractionContext context;
    for (int view = 0; view < 4; ++view) {
        context.armed = view == 1;
        context.editorMode = view == 2;
        context.chopEditorMode = view == 3;
        for (int level = 0; level < 2; ++level) {
            check(interaction::isTarget(interaction::interactiveTargetAt(
                      center(layout::levelFader(level)), context),
                      interaction::InteractiveType::levelFader, level) &&
                  interaction::isTarget(interaction::interactiveTargetAt(
                      center(layout::levelValueLabel(level)), context),
                      interaction::InteractiveType::levelValueLabel, level),
                  "both level faders and value entries work in every view");
        }
    }
    context.menuOpen = true;
    check(!interaction::interactiveTargetAt(center(layout::levelFader(0)), context).valid(),
          "menu overlays block edge fader edits");
    const sms::ui::meter::StereoGeometry meter(layout::inputMeter,
        sms::ui::meter::defaultSegmentCount, 18.0f);
    const auto left = meter.channel(0U);
    const auto right = meter.channel(1U);
    const float capLeft = layout::inputMeter.x + layout::inputMeter.width * 0.5f - 9.0f;
    check(left.width > 0.0f && right.width > 0.0f &&
          left.x + left.width <= capLeft && right.x >= capLeft + 18.0f,
          "fader cap travels between visible stereo LED columns");
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
    namespace layout = midichopper::ui::layout;
    check(layout::editorWaveform.x >= layout::editorZoom.x +
              layout::editorZoom.width + 10.0f &&
          layout::editorScroll.y >= layout::editorWaveform.y +
              layout::editorWaveform.height + 10.0f &&
          layout::editorScroll.x == layout::editorWaveform.x &&
          layout::editorScroll.width == layout::editorWaveform.width,
          "sample waveform reserves gutters for both scroll controls");
    check(layout::chopWaveform.x >= layout::chopZoom.x +
              layout::chopZoom.width + 10.0f &&
          layout::chopScroll.y >= layout::chopWaveform.y +
              layout::chopWaveform.height + 10.0f &&
          layout::chopScroll.x == layout::chopWaveform.x &&
          layout::chopScroll.width == layout::chopWaveform.width,
          "cut waveform reserves matching scroll gutters");
    sms::ui::waveform::Viewport viewport;
    const sms::ui::Rect viewBounds{0.0f, 0.0f, 800.0f, 100.0f};
    viewport.reset(8000U);
    check(viewport.zoom(1.0f, 200.0f, viewBounds) &&
          viewport.start == 1000U && viewport.end == 5000U &&
          std::abs(viewport.frameAt(200.0f, viewBounds) - 2000.0) < 1.0,
          "zoom retains the audio frame under the pointer");
    check(viewport.pan(-1.0f) && viewport.start == 1800U &&
          viewport.xForFrame(1800.0, viewBounds) == viewBounds.x,
          "shift wheel pans by part of the visible duration");
    check(viewport.setZoomPosition(1.0f) && viewport.end - viewport.start == 16U &&
          std::abs(viewport.zoomPosition() - 1.0f) < 0.001f,
          "zoom slider reaches the minimum window");
    check(viewport.setScrollPosition(1.0f) && viewport.end == viewport.total &&
          std::abs(viewport.scrollPosition() - 1.0f) < 0.001f,
          "scroll slider reaches the final source frame");
    const auto thumb = viewport.scrollThumb(viewBounds);
    check(thumb.x + thumb.width == viewBounds.x + viewBounds.width &&
          thumb.width < viewBounds.width,
          "scroll thumb shows the visible source window");
    viewport.reset(20000000U);
    static_cast<void>(viewport.setZoomPosition(1.0f));
    check(viewport.scrollThumb(viewBounds).width >= 12.0f,
          "deep zoom keeps the scroll window visible");
    viewport.reset(8000U);
    check(!viewport.zoomed() && viewport.start == 0U && viewport.end == 8000U,
          "new pad or editor entry restores the full waveform");
    viewport = {20000000U, 12000000U, 12000016U};
    check(std::abs(viewport.frameAt(viewport.xForFrame(12000008.0, viewBounds),
        viewBounds) - 12000008.0) < 0.1,
        "deep zoom maps long-sample frames without virtual-canvas precision loss");
    sms::dsp::SamplePlaybackSettings precise;
    precise.start = 0.5f;
    precise.end = 0.8f;
    sms::ui::waveform::updateRegion(precise, sms::ui::waveform::EditTarget::regionStart,
        400.0f, viewBounds, viewport);
    check(std::abs(static_cast<double>(precise.start) * viewport.total - 12000008.0) < 2.0,
        "zoomed region drag targets the visible source frame");
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
    check(std::abs(sms::ui::waveform::advancePlaybackFraction(
              0.25f, 0.25, 48000U, 48000.0, 1.0f, 0.75f) - 0.5f) < 1.0e-6f &&
          sms::ui::waveform::advancePlaybackFraction(
              0.70f, 0.25, 48000U, 48000.0, 2.0f, 0.75f) == 0.75f,
          "UI playhead clock advances by source rate and tune while respecting End");

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

    const float previousEnd = settings.end;
    sms::ui::waveform::adjustRegionByWheel(
        settings, sms::ui::waveform::EditTarget::regionEnd, -1.0f,
        summary.frames, {0.0f, 0.0f, 100.0f, 40.0f});
    check(settings.end < previousEnd,
          "wheel editing moves the selected sample-region handle");
    settings.attackSeconds = 2.0f;
    settings.decaySeconds = 2.0f;
    settings.sustainLevel = 0.2f;
    settings.releaseSeconds = 2.0f;
    for (int slider = 0; slider < 4; ++slider)
        sms::ui::waveform::resetEnvelopeSlider(settings, slider);
    check(settings.attackSeconds == 0.0f && settings.decaySeconds == 0.0f &&
          settings.sustainLevel == 1.0f && settings.releaseSeconds == 0.0f,
          "each ADSR slider resets to its default");

    sms::dsp::SampleMixerSettings mixer;
    mixer.gainDecibels = -12.0f;
    mixer.pan = 0.5f;
    mixer.tuneSemitones = 7.0f;
    for (int slider = 0; slider < 3; ++slider)
        midichopper::ui::resetMixerKnob(mixer, slider);
    check(mixer.gainDecibels == 0.0f && mixer.pan == 0.0f &&
          mixer.tuneSemitones == 0.0f,
          "each mixer knob resets to its default");

    midichopper::ui::DoubleClickTracker clicks;
    const auto target = midichopper::ui::target(
        midichopper::ui::InteractiveType::mixerKnob, 0);
    check(!clicks.press(target, {10.0f, 10.0f}, 1000U) &&
          clicks.press(target, {12.0f, 11.0f}, 1250U),
          "two nearby slider presses within the interval form a double click");
    check(!clicks.press(target, {10.0f, 10.0f}, 2000U) &&
          !clicks.press(target, {30.0f, 10.0f}, 2100U),
          "distant presses do not reset a slider");
    check(std::abs(midichopper::ui::knobDragNormalized(0.5f, 100.0f, 88.0f) - 0.6f) <
              1.0e-6f &&
          midichopper::ui::knobDragNormalized(0.95f, 100.0f, 0.0f) == 1.0f,
          "mixer knobs use bounded upward drag adjustment");
    using midichopper::ui::KnobAdjustment;
    check(std::abs(midichopper::ui::knobDraggedValue(
              0.0f, 100.0f, 88.0f, -24.0f, 24.0f, 1.0f,
              KnobAdjustment::normal) - 4.8f) < 1.0e-5f &&
          std::abs(midichopper::ui::knobDraggedValue(
              0.0f, 100.0f, 88.0f, -24.0f, 24.0f, 1.0f,
              KnobAdjustment::fine) - 0.48f) < 1.0e-5f,
          "Shift fine adjustment makes mixer knob dragging ten times slower");
    check(midichopper::ui::knobDraggedValue(
              0.0f, 100.0f, 88.0f, -24.0f, 24.0f, 1.0f,
              KnobAdjustment::stepped) == 5.0f &&
          std::abs(midichopper::ui::knobDraggedValue(
              0.0f, 100.0f, 88.0f, -1.0f, 1.0f, 0.1f,
              KnobAdjustment::stepped) - 0.2f) < 1.0e-6f,
          "Control stepped adjustment snaps mixer knob drags to each control's grid");
    check(std::abs(midichopper::ui::knobWheelAdjustedValue(
              0.0f, 1.0f, 0.25f, 1.0f, -24.0f, 24.0f,
              KnobAdjustment::fine) - 0.025f) < 1.0e-6f &&
          midichopper::ui::knobWheelAdjustedValue(
              0.25f, 1.0f, 0.25f, 1.0f, -24.0f, 24.0f,
              KnobAdjustment::stepped) == 1.0f &&
          midichopper::ui::knobWheelAdjustedValue(
              0.25f, -1.0f, 0.25f, 1.0f, -24.0f, 24.0f,
              KnobAdjustment::stepped) == 0.0f,
          "wheel fine and stepped adjustments work from on-grid and off-grid values");
    check(midichopper::ui::bipolarKnobPosition(0.0f, -60.0f, 12.0f) == 0.5f &&
          midichopper::ui::bipolarKnobPosition(-60.0f, -60.0f, 12.0f) == 0.0f &&
          midichopper::ui::bipolarKnobPosition(12.0f, -60.0f, 12.0f) == 1.0f,
          "asymmetric gain draws zero at twelve o'clock");
    check(midichopper::ui::bipolarKnobPosition(0.0f, 0.0f, 1.0f) == 0.0f &&
          midichopper::ui::bipolarKnobPosition(0.5f, 0.0f, 1.0f) == 0.5f &&
          midichopper::ui::bipolarKnobPosition(1.0f, 0.0f, 1.0f) == 1.0f,
          "filter knob marker follows its full unipolar sweep");
    const auto slope = midichopper::ui::layout::mixerKnob(5);
    const auto slopeTrack = midichopper::ui::layout::slopeTrack(slope);
    check(slopeTrack.x == midichopper::ui::layout::mixerKnob(3).x +
              midichopper::ui::layout::mixerKnob(3).width * 0.5f - 24.0f &&
          slopeTrack.x + slopeTrack.width ==
              midichopper::ui::layout::mixerKnob(4).x +
                  midichopper::ui::layout::mixerKnob(4).width * 0.5f + 24.0f &&
          slope.contains({slopeTrack.x, slopeTrack.y + 3.0f}) &&
          slope.contains({slopeTrack.x + slopeTrack.width, slopeTrack.y + 3.0f}),
          "pad slope track aligns with knob artwork and both ends stay clickable");
    check(midichopper::ui::slopeValueAtX(slopeTrack.x, slope) == 0.0f &&
          midichopper::ui::slopeValueAtX(slopeTrack.x + slopeTrack.width * 0.5f, slope) == 1.0f &&
          midichopper::ui::slopeValueAtX(slopeTrack.x + slopeTrack.width, slope) == 2.0f,
          "slope slider selects 6, 12, and 24 dB positions");
    check(midichopper::ui::mixerValueFromText("-12.5", 1.0f, -60.0f, 12.0f) ==
              -12.5f &&
          midichopper::ui::mixerValueFromText("25", 0.01f, -1.0f, 1.0f) == 0.25f &&
          midichopper::ui::mixerValueFromText("200", 0.01f, -1.0f, 1.0f) == 1.0f,
          "typed mixer values use displayed units and clamp to control ranges");
    check(!midichopper::ui::mixerValueFromText("", 1.0f, -24.0f, 24.0f) &&
          !midichopper::ui::mixerValueFromText("1.2.3", 1.0f, -24.0f, 24.0f) &&
          !midichopper::ui::mixerValueFromText("12st", 1.0f, -24.0f, 24.0f),
          "typed mixer values reject incomplete and non-numeric text");
}

void chopEditorSession()
{
    namespace plugin = midichopper::plugin;
    midichopper::ui::ChopEditorSession session;
    const auto now = midichopper::ui::ChopEditorSession::Clock::now();
    session.begin(8U);
    check(!session.ready() && session.requestDue(now), "cut session starts muted and requests a baseline");
    const auto request = session.request(now);
    check(!session.requestDue(now) &&
          session.requestDue(now + std::chrono::milliseconds(250)) &&
          session.request(now + std::chrono::milliseconds(250)).sequence == request.sequence,
          "baseline retry retains identity and waits for its deadline");
    plugin::ChopSnapshotReply reply;
    reply.request = request;
    reply.generations = {11U, 12U, 13U};
    for (std::size_t index = 0; index < reply.waveforms.size(); ++index) {
        reply.waveforms[index].pad = 8U + static_cast<std::uint32_t>(index);
        reply.waveforms[index].frames = 100U;
    }
    auto stale = reply;
    --stale.request.sequence;
    check(!session.accept(stale) && !session.ready(), "superseded baseline reply cannot enable Apply");
    auto wrongPad = reply;
    wrongPad.waveforms[1].pad = 12U;
    check(!session.accept(wrongPad) && !session.ready(), "baseline rejects incorrectly associated waveform");
    check(session.accept(reply) && session.ready() && !session.accept(reply),
          "coherent baseline becomes ready once and ignores late retry replies");
    session.offsets = {20, -10};
    const auto apply = session.applyRequest();
    check(apply.revisionChecked && apply.sequence == request.sequence &&
          apply.firstPad == 8U && apply.padCount == 3U &&
          apply.expectedGenerations == reply.generations &&
          apply.boundaryOffsets[0] == 20 && apply.boundaryOffsets[1] == -10,
          "Apply carries exactly the displayed baseline generations and pending offsets");
    check(!session.acceptStatus({request.sequence + 1U, true, {}}) &&
          session.acceptStatus({request.sequence, false, "Samples changed"}) &&
          !session.acceptStatus({request.sequence, true, {}}),
          "only the matching outstanding Apply result is consumed");
    session.begin(8U);
    const auto fresh = session.request(now);
    check(fresh.sequence != request.sequence && !session.ready() &&
          session.offsets[0] == 0 && !session.accept(reply) &&
          !session.acceptsError(request.sequence) && session.acceptsError(fresh.sequence),
          "same-pad refresh invalidates old baseline, offsets and errors");
    session.cancel();
    check(!session.requestDue(now) && !session.acceptsError(fresh.sequence) && !session.ready(),
          "leaving the cut editor cancels outstanding baseline work");
    midichopper::ui::ChopEditorSession reopened;
    reopened.begin(8U);
    check(reopened.request(now).sequence != fresh.sequence,
          "reopened editor cannot reuse an earlier request identity");
}

void chopEditorGeometry()
{
    check(midichopper::ui::chop::navigationTarget(1, -1, 8) == -1 &&
          midichopper::ui::chop::navigationTarget(1, 1, 8) == 2 &&
          midichopper::ui::chop::navigationTarget(6, 1, 8) == -1 &&
          midichopper::ui::chop::navigationTarget(6, -1, 8) == 5,
          "cut editor navigation keeps a neighbor on both sides");
    check(midichopper::ui::chop::postSplitEditorTarget(0, 8) == 1 &&
          midichopper::ui::chop::postSplitEditorTarget(3, 8) == 4 &&
          midichopper::ui::chop::postSplitEditorTarget(6, 8) == 6 &&
          midichopper::ui::chop::postSplitEditorTarget(7, 8) == -1,
          "split Apply opens a three-pad window around both new halves");
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
    check(midichopper::ui::chop::clampBoundaryOffset(waveforms, offsets, 0, -200) == -100 &&
          midichopper::ui::chop::clampBoundaryOffset(waveforms, offsets, 0, 200) == 100,
          "chop boundary permits either neighbor to reach zero frames");
    check(midichopper::ui::chop::wheelAdjustedBoundaryOffset(
              waveforms, offsets, 0, 1.0f, bounds) == 1 &&
          midichopper::ui::chop::wheelAdjustedBoundaryOffset(
              waveforms, offsets, 0, -1.0f, bounds) == -1,
          "wheel editing moves a cut point by one displayed-pixel step");

    auto moved = offsets;
    moved[0] = 50;
    check(midichopper::ui::chop::adjustedFrames(waveforms, moved, 0) == 150U &&
          midichopper::ui::chop::adjustedFrames(waveforms, moved, 1) == 50U,
          "rolling edit transfers duration between adjacent pads");
    check(midichopper::ui::chop::adjustedStartFrame(waveforms, moved, 1) == 150U,
          "middle raw preview starts at the pending first cut");
    moved[0] = 100;
    check(midichopper::ui::chop::adjustedFrames(waveforms, moved, 1) == 0U,
          "coincident boundaries produce a zero-length middle pad");
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

class ChopHost final : public midichopper::ui::ChopEditorHost {
public:
    void sendChopState(const char* key, const std::string& value) override
    { messages.emplace_back(key, value); }
    void selectChopPad(const int pad) override { selectedPad = pad; }
    void leaveChopEditor() override { ++exits; }
    void resetChopViewport(const std::uint64_t frames) override { viewportFrames = frames; }
    void setChopStatus(const char* message) override { status = message; }
    void setChopStructureBusy(const bool value) override { busy = value; }
    void repaintChopEditor() override { ++repaints; }
    const std::string& last(const std::string_view key) const
    {
        for (auto it = messages.rbegin(); it != messages.rend(); ++it)
            if (it->first == key)
                return it->second;
        check(false, "expected a workflow transport message");
        std::abort();
    }
    std::vector<std::pair<std::string, std::string>> messages;
    int selectedPad = -1;
    int exits = 0;
    int repaints = 0;
    std::uint64_t viewportFrames = 0U;
    std::string status;
    bool busy = false;
};

void chopEditorWorkflow()
{
    using namespace std::chrono_literals;
    namespace plugin = midichopper::plugin;
    ChopHost host;
    midichopper::ui::ChopEditorController editor(host);
    const auto now = midichopper::ui::ChopEditorController::Clock::time_point{} + 1s;
    editor.open(0, 0, 16);
    check(!editor.active(), "ordinary cut editing requires both visible neighbors");
    editor.open(5, 0, 16);
    plugin::ChopMidiPreviewRequest midi;
    check(editor.active() && !editor.split() && !editor.ready() && host.selectedPad == 5 &&
          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
          midi.active && midi.previewPadCount == 0U,
          "opening the cut workflow selects its center and exclusively mutes MIDI while loading");
    editor.idle(now);
    plugin::ChopSnapshotReply reply;
    check(plugin::decodeChopSnapshotRequest(host.last("chop_snapshot_request"), reply.request),
          "the workflow requests a coherent three-pad baseline");
    for (std::size_t pad = 0U; pad < reply.waveforms.size(); ++pad) {
        reply.waveforms[pad].pad = reply.request.firstPad + static_cast<std::uint32_t>(pad);
        reply.waveforms[pad].frames = 100U;
        reply.waveforms[pad].sampleRate = 1000.0;
        reply.generations[pad] = pad + 1U;
    }
    const auto baseline = plugin::encodeChopSnapshotReply(reply);
    check(editor.acceptState("chop_snapshot_data", baseline.c_str()) && editor.ready() &&
          host.viewportFrames == 300U &&
          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
          midi.previewPadCount == 3U,
          "a coherent baseline enables the three proposed MIDI slices");
    sms::ui::waveform::Viewport viewport;
    viewport.reset(300U);
    editor.beginBoundaryDrag(0, 0.0f);
    editor.dragBoundary(25.0f, 300.0f, viewport);
    editor.endBoundaryDrag();
    check(editor.dirty() && editor.offsets()[0] == 25 &&
          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
          midi.sourceEndFrames[0] == 125U && midi.sourceFrames[1] == 125U,
          "dragging a cut updates the owned baseline and audition map together");
    editor.preview(1);
    plugin::ChopPreviewRequest preview;
    check(plugin::decodeChopPreviewRequest(host.last("chop_preview_request"), preview) &&
          preview.play && preview.sourceFrame == 125U && preview.sourceEndFrame == 200U,
          "pad preview uses the proposed raw source range");
    editor.acceptPreviewPosition(6.5f);
    check(editor.previewPad() == 1 && editor.previewPosition() == 6.5f,
          "raw host playheads map into the proposed slice");
    editor.apply();
    plugin::ChopApplyRequest apply;
    check(editor.applying() &&
          plugin::decodeChopApplyRequest(host.last("chop_apply_request"), apply) &&
          apply.sequence == reply.request.sequence && apply.boundaryOffsets[0] == 25 &&
          apply.expectedGenerations[0] == 1U &&
          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
          midi.active && midi.previewPadCount == 0U,
          "Apply sends the guarded baseline and exclusively mutes preview MIDI");
    editor.navigate(-1, 0, 16);
    check(host.selectedPad == 5, "navigation waits for Apply completion");
    plugin::ChopApplyStatus status;
    status.sequence = apply.sequence + 1U;
    status.success = true;
    auto encodedStatus = plugin::encodeChopStatus(status.sequence, status.success, status.message);
    check(!editor.acceptState("chop_status", encodedStatus.c_str()) && editor.applying(),
          "an earlier Apply result cannot finish the current edit");
    status.sequence = apply.sequence;
    encodedStatus = plugin::encodeChopStatus(status.sequence, status.success, status.message);
    check(editor.acceptState("chop_status", encodedStatus.c_str()) && !editor.applying() &&
          !editor.ready() && !editor.dirty() && host.viewportFrames == 0U &&
          !editor.acceptState("chop_snapshot_data", baseline.c_str()),
          "Apply completion clears offsets and waits for a fresh baseline");
    editor.idle(now + 1s);
    plugin::ChopSnapshotRequest refreshed;
    check(plugin::decodeChopSnapshotRequest(host.last("chop_snapshot_request"), refreshed) &&
          refreshed.sequence != reply.request.sequence,
          "post-Apply refresh advances the baseline identity");
    editor.navigate(-1, 0, 16);
    check(host.selectedPad == 4 && editor.firstPad() == 3,
          "ordinary navigation loads the neighboring center");
    editor.cancel();
    check(!editor.active() && host.exits == 1 &&
          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) && !midi.active,
          "Exit releases exclusive MIDI and returns to the sample-selection workflow");
}

void splitEditorWorkflow()
{
    namespace plugin = midichopper::plugin;
    ChopHost host;
    midichopper::ui::ChopEditorController editor(host);
    plugin::SplitPlanReady plan;
    plan.planId = 42U;
    plan.targetPad = 4U;
    plan.emptyPad = 7U;
    plan.waveform.pad = 4U;
    plan.waveform.frames = 100U;
    plan.waveform.sampleRate = 1000.0;
    editor.openSplit(plan);
    plugin::ChopMidiPreviewRequest midi;
    check(editor.active() && editor.split() && editor.ready() && editor.dirty() &&
          editor.offsets()[0] == -50 && editor.splitPlanId() == 42U &&
          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
          midi.sourcePadCount == 1U && midi.previewPadCount == 2U &&
          midi.sourceFrames[1] == 50U && midi.sourceEndFrames[1] == 100U,
          "split plans own two virtual slices from a single physical source");
    editor.preview(1);
    plugin::ChopPreviewRequest preview;
    check(plugin::decodeChopPreviewRequest(host.last("chop_preview_request"), preview) &&
          preview.padCount == 1U && preview.sourceFrame == 50U && preview.sourceEndFrame == 100U,
          "split suffix preview never includes the physical neighbor");
    sms::ui::waveform::Viewport viewport;
    viewport.reset(100U);
    editor.beginBoundaryDrag(0, 0.0f);
    editor.dragBoundary(-100.0f, 100.0f, viewport);
    check(editor.offsets()[0] == -99, "split edits retain at least one prefix frame");
    editor.dragBoundary(100.0f, 100.0f, viewport);
    editor.endBoundaryDrag();
    check(editor.offsets()[0] == -1, "split edits retain at least one suffix frame");
    editor.apply();
    plugin::PadStructureRequest apply;
    check(editor.applying() && host.busy &&
          plugin::decodePadStructureRequest(host.last("pad_structure_request"), apply) &&
          apply.action == plugin::PadStructureAction::applySplit &&
          apply.planId == 42U && apply.splitFrame == 99U,
          "split Apply retains its owned plan and adjusted cut");
    check(!editor.acceptSplitStatus("PS1;O;41;S", 0, 16) &&
          !editor.acceptSplitStatus("PS1;E;41;stale", 0, 16) && editor.applying(),
          "unrelated split statuses cannot change the active plan");
    check(editor.acceptSplitStatus("PS1;O;42;S", 0, 16) && editor.active() && !editor.split() &&
          !editor.applying() && !host.busy && host.selectedPad == 5 && !editor.ready(),
          "split completion opens a fresh ordinary editor around the resulting halves");
    editor.openSplit(plan);
    editor.navigate(-1, 0, 16);
    check(plugin::decodePadStructureRequest(host.last("pad_structure_request"), apply) &&
          apply.action == plugin::PadStructureAction::cancelSplit && apply.planId == 42U &&
          !editor.split() && host.selectedPad == 3,
          "split navigation cancels the owned plan before loading ordinary cuts");
    editor.openSplit(plan);
    const auto count = host.messages.size();
    check(editor.acceptSplitStatus("PS1;E;42;Sample changed", 0, 16) &&
          !editor.active() && editor.splitPlanId() == 0U && host.status == "Sample changed",
          "a matching split error releases its plan and exits the workflow");
    for (std::size_t index = count; index < host.messages.size(); ++index)
        check(host.messages[index].first != "pad_structure_request",
              "failed split plans are not cancelled again after the DSP discarded them");
}

void waveformDetailSession()
{
    using namespace std::chrono_literals;
    midichopper::ui::WaveformDetailSession session;
    const auto now = midichopper::ui::WaveformDetailSession::Time{} + 1s;
    const sms::ui::Rect bounds{0.0f, 0.0f, 100.0f, 100.0f};
    session.reset(1000U);
    check(session.zoom(1.0f, 50.0f, bounds, now) &&
          !session.requestDue(now + 49ms) && session.requestDue(now + 50ms),
          "viewport changes debounce detail requests for 50 milliseconds");
    const auto request = session.request(7, 3U, true, now + 50ms);
    check(request && request->start == session.viewport().start &&
          request->end == session.viewport().end && request->padCount == 3U &&
          !session.requestDue(now + 299ms) && session.requestDue(now + 300ms),
          "detail requests track the visible range and retry after 250 milliseconds");
    const auto retry = session.request(7, 3U, true, now + 300ms);
    check(retry && retry->sequence == request->sequence,
          "detail retries retain their request identity");
    midichopper::plugin::WaveformDetailReply reply{*request, {}};
    ++reply.request.firstPad;
    check(!session.accept(reply), "detail replies from a different pad are ignored");
    reply.request = *request;
    check(session.accept(reply) && session.detail() != nullptr &&
          !session.requestDue(now + 1s),
          "matching detail replies stop retries and become visible");
    check(!session.pan(0.0f, now) && session.detail() != nullptr,
          "an unchanged viewport retains its matching detail");
    check(session.pan(-1.0f, now + 1s) && session.detail() == nullptr &&
          !session.accept(reply),
          "panning hides old detail and rejects the previous range");
    const auto moved = session.request(7, 3U, true, now + 1050ms);
    check(moved && moved->sequence != request->sequence,
          "a changed viewport creates a new detail identity");
    session.reset(1000U);
    check(session.detail() == nullptr && !session.accept({*moved, {}}) &&
          !session.requestDue(now + 2s),
          "source resets invalidate previous replies and pending retries");
    check(session.setZoomPosition(0.5f, now + 2s) &&
          !session.request(7, 1U, false, now + 2050ms) &&
          !session.requestDue(now + 3s),
          "hidden editors cancel detail retries");
}

void playbackIndicator()
{
    using namespace std::chrono_literals;
    midichopper::ui::PlaybackIndicator indicator;
    const auto now = midichopper::ui::PlaybackIndicator::Time{} + 1s;
    sms::audio::WaveformSummary waveform;
    waveform.frames = 1000U;
    waveform.sampleRate = 1000.0;
    sms::dsp::SamplePlaybackSettings playback;
    playback.start = 0.25f;
    playback.end = 0.75f;
    sms::dsp::SampleMixerSettings mixer;
    check(indicator.start(7, -1, playback, now) && indicator.position() == 8.0f,
          "audition starts at zero until coherent editor settings arrive");
    check(!indicator.update(now + 1s, 7, true, waveform, playback, mixer, 0.0f) &&
          indicator.position() == 8.0f,
          "playhead waits for its settings without accumulating elapsed time");
    indicator.acceptSettings(6, playback, now + 1s);
    check(indicator.position() == 8.0f, "another pad's settings cannot move the playhead");
    indicator.acceptSettings(7, playback, now + 1s);
    check(indicator.position() == 8.25f &&
          indicator.update(now + 1100ms, 7, true, waveform, playback, mixer, 0.0f) &&
          std::abs(indicator.position() - 8.35f) < 1.0e-5f,
          "matching settings restart the elapsed-time origin at the saved region start");
    check(indicator.start(7, 7, playback, now + 2s) &&
          !indicator.start(7, 7, playback, now + 2100ms),
          "nearby duplicate playback notifications do not restart the indicator");
    check(indicator.update(now + 2200ms, 7, true, waveform, playback, mixer, 12.0f) &&
          std::abs(indicator.position() - 8.65f) < 1.0e-5f,
          "playhead interpolation follows global varispeed tuning");
    check(indicator.update(now + 2300ms, 7, true, waveform, playback, mixer, 12.0f) &&
          indicator.position() == 8.75f &&
          !indicator.update(now + 2399ms, 7, true, waveform, playback, mixer, 12.0f) &&
          indicator.update(now + 2400ms, 7, true, waveform, playback, mixer, 12.0f) &&
          indicator.position() == 0.0f,
          "the final playhead position is held for 100 milliseconds before clearing");
    indicator.acceptHostPosition(4.5f, now + 3s);
    check(indicator.pad() == 3 && indicator.position() == 4.5f,
          "host positions replace the local estimate");
    indicator.stop(false, now + 3s);
    check(indicator.pad() == -1 && indicator.position() == 0.0f,
          "explicit Stop clears the indicator immediately");
}

void mixerValueEntry()
{
    midichopper::ui::MixerValueEntry entry;
    const sms::ui::InteractiveTarget target{
        static_cast<int>(midichopper::ui::InteractiveType::mixerValueLabel), 1};
    entry.begin(target, 7, 25.0f);
    check(entry.target() == target && entry.pad() == 7 &&
          std::string_view(entry.text()) == "25",
          "numeric entry retains its target pad and initial display value");
    check(!entry.type('x') && std::string_view(entry.text()) == "25" &&
          entry.type('-') && entry.type('1') && entry.type('.') && entry.type('5') &&
          !entry.type('.') && !entry.type('+') && !entry.type(0x100U) &&
          entry.value(0.01f, -1.0f, 1.0f) == -0.015f,
          "first numeric character replaces the old value and later input follows decimal syntax");
    entry.backspace();
    check(std::string_view(entry.text()) == "-1.", "backspace removes one typed character");
    entry.begin(target, 7, 25.0f);
    entry.backspace();
    check(std::string_view(entry.text()).empty() && !entry.value(1.0f, -1.0f, 1.0f),
          "first backspace clears the selected initial value");
    for (int index = 0; index < 23; ++index)
        check(entry.type('1'), "numeric input fits its bounded text buffer");
    check(!entry.type('1') && std::string_view(entry.text()).size() == 23U,
          "numeric input reserves its terminating null byte");
    entry.cancel();
    check(!entry.target().valid() && entry.pad() == -1 && std::string_view(entry.text()).empty(),
          "cancellation resets numeric entry state");
}

} // namespace

int main()
{
    padLayouts();
    wideCanvasGeometry();
    hamburgerMenuGeometry();
    contextMenuGeometry();
    interactionTargets();
    spaceKeyTracking();
    padPressTracking();
    editorSnapshotCollection();
    chopEditorWorkflow();
    splitEditorWorkflow();
    waveformDetailSession();
    playbackIndicator();
    mixerValueEntry();
    wheelAdjustment();
    mainSliderDragging();
    levelFaderTargets();
    levelMeterGeometry();
    waveformGeometry();
    chopEditorGeometry();
    chopEditorSession();
    std::cout << "UI geometry tests passed\n";
}
