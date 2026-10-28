#include "Audio/WaveformSummary.hpp"
#include "ContextMenu.hpp"
#include "ChopEditor.hpp"
#include "ChopEditorSession.hpp"
#include "ChopEditorController.hpp"
#include "EditorSnapshotSession.hpp"
#include "MixerValueEntry.hpp"
#include "MixerValueNavigation.hpp"
#include "PlaybackIndicator.hpp"
#include "SampleWaveform.hpp"
#include "EditorAuditionMode.hpp"
#include "WaveformDetailSession.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "Interaction.hpp"
#include "HelpLinks.hpp"
#include "MidichopperLayout.hpp"
#include "MidichopperInteraction.hpp"
#include "LevelMeter.hpp"
#include "LogoGlow.hpp"
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

void logoGlow()
{
    using namespace sms::ui;
    check(logoGlowOpacity(0.7f, 0.0f, 100.0) == 0.7f, "zero period keeps glow steady");
    check(logoGlowOpacity(0.0f, 3.0f, 1.0) == 0.0f, "zero alpha disables glow");
    check(std::abs(logoGlowOpacity(0.7f, 3.0f, 1.5) - 0.455f) < 0.00001f,
          "pulse retains a visible minimum brightness");
    check(std::abs(logoGlowOpacity(0.7f, 3.0f, 0.0) -
                   logoGlowOpacity(0.7f, 3.0f, 3000000.0)) < 0.00001f,
          "pulse repeats without accumulated time drift");
    std::vector<std::uint8_t> pixels(logoWidth * logoHeight * 4, 0);
    auto mask = makeLogoGlow(pixels, logoWidth, logoHeight, 10.0f);
    for (std::size_t index = 3; index < mask.size(); index += 4)
        check(mask[index] == 0, "transparent artwork creates no glow");
    const int cx = logoWidth / 2, cy = logoHeight / 2;
    pixels[(cy * logoWidth + cx) * 4 + 3] = 255;
    mask = makeLogoGlow(pixels, logoWidth, logoHeight, 10.0f);
    const auto alpha = [&](int x, int y) {
        return mask[((y + logoGlowPadding) * logoGlowWidth + x + logoGlowPadding) * 4 + 3];
    };
    check(alpha(cx, cy) > alpha(cx + 5, cy) && alpha(cx + 5, cy) > 0,
          "glow spreads softly around the actual alpha silhouette");
    check(alpha(cx + 15, cy) == 0 && mask[3] == 0,
          "finite blur support leaves distant pixels transparent");
    check(alpha(cx - 3, cy) == alpha(cx + 3, cy) && alpha(cx, cy - 3) == alpha(cx, cy + 3),
          "halo remains centered around the original logo");
    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        pixels[index] = 255;
        pixels[index + 1] = 120;
    }
    check(makeLogoGlow(pixels, logoWidth, logoHeight, 10.0f) == mask,
          "glow depends only on alpha, preserving logo colors");
    check(makeLogoGlow(pixels, logoWidth, logoHeight, 0.0f).empty() &&
          makeLogoGlow(pixels, logoWidth, logoHeight, 25.0f).empty() &&
          makeLogoGlow({}, 0, 0, 10.0f).empty() &&
          makeLogoGlow({}, 4097, 1, 10.0f).empty() &&
          makeLogoGlow({}, logoWidth, logoHeight, 10.0f).empty(),
          "disabled and invalid mask requests do not allocate output");
    std::vector<std::uint8_t> solid{0, 0, 0, 255};
    const auto stretched = makeLogoGlow(solid, 1, 1, 24.0f);
    check(stretched.size() == logoGlowWidth * logoGlowHeight * 4 &&
          stretched[((logoGlowPadding + cy) * logoGlowWidth + logoGlowPadding + cx) * 4 + 3] == 255,
          "arbitrary artwork dimensions stretch to the fixed header rectangle");
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

void pluginIdentity()
{
    check(DISTRHO_PLUGIN_IS_SYNTH == 1,
          "DPF classifies the sampler as an instrument, including LV2 export");
    check(std::string_view(DISTRHO_PLUGIN_VST3_CATEGORIES) == "Instrument|Sampler|Stereo",
          "VST3 advertises a stereo sampler instrument");
    constexpr std::array<std::string_view, 3> clapFeatures{DISTRHO_PLUGIN_CLAP_FEATURES};
    check(clapFeatures == std::array<std::string_view, 3>{"instrument", "sampler", "stereo"},
          "CLAP advertises a stereo sampler instrument");
    check(DISTRHO_PLUGIN_NUM_INPUTS == 2 && DISTRHO_PLUGIN_NUM_OUTPUTS == 2 &&
          DISTRHO_PLUGIN_WANT_MIDI_INPUT == 1,
          "instrument classification preserves stereo live capture and MIDI playback ports");
    check(std::string_view(DISTRHO_PLUGIN_NAME) == "SMS-Anville Sampler",
          "host-visible product name uses Anville");
    check(std::string_view(DISTRHO_PLUGIN_URI) ==
              "https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler" &&
          std::string_view(DISTRHO_PLUGIN_CLAP_ID) == "io.github.sirsipe.anvillesampler",
          "LV2 and CLAP identities use the renamed product");
}

void hamburgerMenuGeometry()
{
    namespace menu = midichopper::ui::layout;
    for (const bool skins : {false, true}) {
        const auto panel = menu::menuPanelBounds(skins);
        const auto firstLink = menu::menuLinkOption(0, skins);
        const auto lastLink = menu::menuLinkOption(1, skins);
        const auto reset = menu::resetMenuButton(skins);
        check(menu::layoutMenuButton.y + menu::layoutMenuButton.height < menu::bankModeMenuButton.y &&
              menu::bankModeMenuButton.y + menu::bankModeMenuButton.height < menu::skinMenuButton.y &&
              reset.y > (skins ? menu::skinMenuButton.y + menu::skinMenuButton.height
                                   : menu::bankModeMenuButton.y + menu::bankModeMenuButton.height) &&
              firstLink.y > reset.y + reset.height &&
              lastLink.y > firstLink.y + firstLink.height &&
              panel.contains({firstLink.x, firstLink.y}) && panel.contains({lastLink.x, lastLink.y}) &&
              lastLink.y + lastLink.height <= panel.y + panel.height,
              "hamburger orders layout, bank mode, optional skin, reset, then links without overlap");
    }
    check(midichopper::ui::onlineHelpUrl("main") ==
              "https://github.com/sirsipe/SMS-Plugins/tree/main/SMS-AnvilleSampler" &&
          midichopper::ui::onlineHelpUrl("SMS-AnvilleSampler-v1.2.3") ==
              "https://github.com/sirsipe/SMS-Plugins/tree/SMS-AnvilleSampler-v1.2.3/SMS-AnvilleSampler" &&
          midichopper::ui::kIssueUrl ==
              "https://github.com/sirsipe/SMS-Plugins/issues" &&
          midichopper::ui::kMoreSkinsUrl ==
              "https://github.com/sirsipe/SMS-Plugins/blob/main/SMS-AnvilleSampler/additional-skins/README.md",
          "hamburger links target help, issues and the latest additional-skins README");
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

void skinMenuGeometryAndTargets()
{
    namespace layout = midichopper::ui::layout;
    namespace interaction = midichopper::ui;
    using interaction::InteractiveType;
    const auto inside = [](const sms::ui::Rect outer, const sms::ui::Rect inner) {
        return inner.x >= outer.x && inner.y >= outer.y &&
            inner.x + inner.width <= outer.x + outer.width &&
            inner.y + inner.height <= outer.y + outer.height;
    };
    const auto basePanel = layout::menuPanelBounds(false);
    const auto skinPanel = layout::menuPanelBounds(true);
    check(skinPanel.height == layout::menuPanel.height &&
          skinPanel.height > basePanel.height &&
          inside(layout::contentBounds, skinPanel) &&
          inside(skinPanel, layout::skinMenuButton) &&
          inside(skinPanel, layout::menuLinkOption(0)) &&
          inside(skinPanel, layout::menuLinkOption(1)) &&
          layout::skinMenuButton.y + layout::skinMenuButton.height < layout::menuLinkOption(0).y,
          "skin selector precedes browser links inside the expanded menu");
    check(layout::skinMenuVisibleCount(-1) == 0 &&
          layout::skinMenuVisibleCount(0) == 0 &&
          layout::skinMenuMaxOffset(0) == 0,
          "empty skin choices have no visible rows or scrolling");
    for (const int total : {1, 2, 18, 19, 129}) {
        const int visible = layout::skinMenuVisibleCount(total);
        const int maxOffset = layout::skinMenuMaxOffset(total);
        const auto menu = layout::skinMenuGeometry(total);
        check(visible == std::min(total, 18) && maxOffset == total - visible &&
              inside(layout::contentBounds, menu.bounds()) &&
              menu.bounds().x < layout::menuPanel.x,
              "skin popup stays bounded to the left for short and long catalogs");
        for (int row = 0; row < visible; ++row) {
            check(inside(layout::contentBounds, menu.item(row)) &&
                  inside(menu.bounds(), menu.item(row)) &&
                  menu.hit(center(menu.item(row))) == row,
                  "every visible skin row fits and resolves its row identity");
            check(layout::skinMenuEntry(row, 0) == row &&
                  layout::skinMenuEntry(row, maxOffset) ==
                      (row == 0 ? 0 : row + maxOffset),
                  "scrolling retains Default and maps the remaining rows");
        }
        check(layout::skinMenuEntry(visible - 1, maxOffset) == total - 1,
              "maximum skin scroll offset reaches the final catalog choice");
        const auto separator = menu.item(visible);
        const auto getMore = menu.item(visible + 1);
        check(inside(menu.bounds(), separator) && inside(menu.bounds(), getMore) &&
              menu.hit(center(separator)) == -1 && menu.hit(center(getMore)) == visible + 1 &&
              separator.y >= menu.item(visible - 1).y + menu.item(visible - 1).height &&
              getMore.y >= separator.y + separator.height,
              "separator and download link fit below all visible skin choices");
        interaction::InteractionContext context;
        context.menuOpen = true;
        context.menuSubmenu = layout::MenuSubmenu::skin;
        context.skinMenu = menu;
        context.skinMenuItemCount = visible;
        check(interaction::isTarget(interaction::interactiveTargetAt(center(getMore), context),
                  InteractiveType::skinGetMore) &&
              !interaction::interactiveTargetAt(center(separator), context).valid(),
              "download link has a distinct action and the separator blocks underlying controls");
    }

    for (int view = 0; view < 4; ++view) {
        interaction::InteractionContext context;
        context.editorMode = view == 1;
        context.chopEditorMode = view == 2;
        context.armed = view == 3;
        context.skinMenu = layout::skinMenuGeometry(19);
        context.skinMenuItemCount = layout::skinMenuVisibleCount(19);
        const auto selector = center(layout::skinMenuButton);
        for (const bool menuOpen : {false, true}) {
            for (const bool available : {false, true}) {
                context.menuOpen = menuOpen;
                context.skinsAvailable = available;
                check(interaction::isTarget(
                          interaction::interactiveTargetAt(selector, context),
                          InteractiveType::menuSkins) == (menuOpen && available),
                      "skin selector requires an open menu and available skins in every view");
            }
        }
        context.menuOpen = true;
        context.skinsAvailable = true;
        context.menuSubmenu = layout::MenuSubmenu::skin;
        for (int row = 0; row < context.skinMenuItemCount; ++row) {
            check(interaction::isTarget(interaction::interactiveTargetAt(
                      center(context.skinMenu.item(row)), context),
                      InteractiveType::skinItem, row),
                  "skin submenu rows take priority over all underlying views");
        }
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  center(context.skinMenu.item(context.skinMenuItemCount + 1)), context),
                  InteractiveType::skinGetMore),
              "download action takes priority over all underlying views");
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  center(layout::menuButton), context), InteractiveType::menuButton),
              "hamburger remains available to close the full skin menu");

        // Place an overlay across a parent option to exercise overlapping targets
        // and padding independently of the current popup placement.
        const auto option = layout::layoutMenuButton;
        context.skinMenu = {{option.x, option.y}, 2, layout::contentBounds};
        context.skinMenuItemCount = 2;
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  center(context.skinMenu.item(0)), context), InteractiveType::skinItem, 0),
              "skin entries win when overlapping a parent menu option");
        const sms::ui::Point padding{option.x + 1.0f, option.y + 1.0f};
        check(option.contains(padding) && context.skinMenu.bounds().contains(padding) &&
              context.skinMenu.hit(padding) == -1 &&
              !interaction::interactiveTargetAt(padding, context).valid(),
              "skin submenu padding blocks parent menu fallthrough");
        context.menuSubmenu = layout::MenuSubmenu::none;
        check(interaction::isTarget(interaction::interactiveTargetAt(padding, context),
                  InteractiveType::menuPadLayouts),
              "a hidden skin submenu leaves its parent option clickable");
        context.menuOpen = false;
        context.menuSubmenu = layout::MenuSubmenu::skin;
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  center(layout::levelFader(0)), context), InteractiveType::levelFader, 0),
              "stale submenu state cannot capture controls while the parent menu is hidden");
    }

    interaction::InteractionContext context;
    const sms::ui::BankedPadLayout pads(0);
    const auto pad = pads.grid(layout::mainPadBounds, 10.0f).cell(pads.visualIndex(0));
    context.skinMenu = {{pad.x, pad.y}, 2, layout::contentBounds};
    context.skinMenuItemCount = 2;
    context.menuSubmenu = layout::MenuSubmenu::skin;
    const auto point = center(context.skinMenu.item(0));
    check(interaction::isTarget(interaction::interactiveTargetAt(point, context),
              InteractiveType::pad, 0),
          "a hidden skin menu does not capture the underlying pad");
    context.menuOpen = true;
    for (const bool skins : {false, true}) {
        context.skinsAvailable = skins;
        context.resetEnabled = true;
        check(interaction::isTarget(
                  interaction::interactiveTargetAt(center(layout::resetMenuButton(skins)), context),
                  interaction::InteractiveType::menuReset),
              "reset can be clicked with and without the skin selector");
        context.resetEnabled = false;
        check(!interaction::interactiveTargetAt(center(layout::resetMenuButton(skins)), context).valid(),
              "busy reset cannot click through to underlying controls");
    }
    context.resetEnabled = true;
    check(interaction::isTarget(interaction::interactiveTargetAt(point, context),
              InteractiveType::skinItem, 0),
          "an open skin submenu captures the underlying pad");
}

void layoutAndBankSubmenuTargets()
{
    namespace layout = midichopper::ui::layout;
    namespace interaction = midichopper::ui;
    using interaction::InteractiveType;
    for (int view = 0; view < 4; ++view) {
        interaction::InteractionContext context;
        context.editorMode = view == 1;
        context.chopEditorMode = view == 2;
        context.armed = view == 3;
        for (const bool skins : {false, true}) {
            context.skinsAvailable = skins;
            for (const bool open : {false, true}) {
                context.menuOpen = open;
                for (const auto submenu : {layout::MenuSubmenu::none, layout::MenuSubmenu::padLayout,
                                           layout::MenuSubmenu::bankMode, layout::MenuSubmenu::skin}) {
                    context.menuSubmenu = submenu;
                    for (const bool capture : {false, true}) {
                        context.captureActive = capture;
                        if (open) {
                            check(interaction::isTarget(interaction::interactiveTargetAt(
                                      center(layout::layoutMenuButton), context), InteractiveType::menuPadLayouts) &&
                                  interaction::isTarget(interaction::interactiveTargetAt(
                                      center(layout::bankModeMenuButton), context), InteractiveType::menuBankModes),
                                  "submenu parents stay reachable when switching between menus");
                        }
                        for (const bool pads : {false, true}) {
                            const auto geometry = pads ? layout::layoutMenuGeometry() : layout::bankModeMenuGeometry();
                            const auto type = pads ? InteractiveType::menuLayout : InteractiveType::menuMidiBankMode;
                            const auto active = pads ? layout::MenuSubmenu::padLayout : layout::MenuSubmenu::bankMode;
                            check(geometry.bounds().x + geometry.bounds().width <= layout::menuPanel.x + 2.0f &&
                                  geometry.bounds().y >= layout::contentBounds.y,
                                  "layout and bank submenus fit to the left of their parents");
                            for (int row = 0; row < (pads ? 3 : 2); ++row) {
                                check(interaction::isTarget(interaction::interactiveTargetAt(
                                          center(geometry.item(row)), context), type, row) ==
                                          (open && submenu == active && (!pads || !capture)),
                                      "choices require their open submenu; capture disables only layout changes");
                            }
                            if (open && submenu == active) {
                                const auto bounds = geometry.bounds();
                                check(!interaction::interactiveTargetAt({bounds.x + 1.0f, bounds.y + 1.0f}, context).valid(),
                                      "layout and bank submenu padding blocks underlying controls");
                            }
                        }
                    }
                }
            }
        }
    }
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
              interaction::interactiveTargetAt(center(layout::holdMode), context),
              interaction::InteractiveType::holdMode) &&
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
    for (const float fraction : {0.05f, 0.25f, 0.5f, 0.75f, 0.95f}) {
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  {layout::globalDirection.x + layout::globalDirection.width * fraction,
                   layout::globalDirection.y + layout::globalDirection.height * 0.5f}, context),
                  interaction::InteractiveType::globalDirection),
              "global direction arrows and margins share one toggle target");
    }
    check(layout::globalDirection.width == layout::padDirection.width &&
          layout::globalDirection.height == layout::padDirection.height &&
          layout::globalDirection.x >= layout::sidePanel.x &&
          layout::globalDirection.x + layout::globalDirection.width < layout::globalMixerKnob(1).x &&
          layout::globalMixerKnob(1).x + layout::globalMixerKnob(1).width <
              layout::globalMixerKnob(6).x &&
          layout::globalMixerKnob(6).x + layout::globalMixerKnob(6).width <=
              layout::sidePanel.x + layout::sidePanel.width &&
          layout::globalMixerKnob(2).x == 994.0f &&
          layout::globalMixerKnob(3).x == 1068.0f &&
          layout::globalMixerKnob(4).x == 1142.0f,
          "global mixer top row is Reverse, Pan, Dirty while the bottom row stays fixed");
    context.armed = true;
    check(!interaction::isTarget(interaction::interactiveTargetAt(
              center(layout::globalDirection), context), interaction::InteractiveType::globalDirection),
          "global direction follows the mixer visibility when capture is armed");
    context.armed = false;
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
              interaction::interactiveTargetAt(center(layout::layoutMenuButton), context),
              interaction::InteractiveType::menuPadLayouts),
          "open hamburger menu exposes its option");
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::menuLinkOption(0, context.skinsAvailable)), context),
              interaction::InteractiveType::menuLink, 0) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::menuLinkOption(1, context.skinsAvailable)), context),
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
    for (const float fraction : {0.05f, 0.25f, 0.5f, 0.75f, 0.95f}) {
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  {layout::padDirection.x + layout::padDirection.width * fraction,
                   layout::padDirection.y + layout::padDirection.height * 0.5f}, context),
                  interaction::InteractiveType::padDirection),
              "both arrows and margins belong to one direction toggle");
        check(interaction::isTarget(interaction::interactiveTargetAt(
                  {layout::padGlobalMixer.x + layout::padGlobalMixer.width * fraction,
                   layout::padGlobalMixer.y + layout::padGlobalMixer.height * 0.5f}, context),
                  interaction::InteractiveType::padGlobalMixer),
              "the power icon and margins belong to one Global Mixer toggle");
    }
    check(layout::mixerKnob(4).x + layout::mixerKnob(4).width <
              layout::padGlobalMixerSeparator.x &&
          layout::padGlobalMixerSeparator.x < layout::padGlobalMixer.x &&
          layout::padGlobalMixer.x + layout::padGlobalMixer.width <
              layout::mainPanel.x + layout::mainPanel.width &&
          layout::padGlobalMixer.width == layout::padDirection.width &&
          layout::padGlobalMixer.height == layout::padDirection.height &&
          layout::padGlobalMixer.y == layout::padDirection.y &&
          !interaction::interactiveTargetAt(center(layout::padGlobalMixerSeparator), context).valid(),
          "Global Mixer follows Highpass and an inert divider inside the panel");
    check(layout::padDirection.x >= layout::mainPanel.x &&
          layout::padDirection.width == 42.0f && layout::padDirection.height == 34.0f &&
          layout::padDirection.y >= layout::mixerKnob(0).y &&
          layout::padDirection.y + layout::padDirection.height <=
              layout::mixerKnob(0).y + layout::mixerKnob(0).height &&
          layout::padDirection.x + layout::padDirection.width < layout::mixerKnob(0).x &&
          layout::mixerKnob(0).x + layout::mixerKnob(0).width < layout::mixerKnob(1).x,
          "direction is the leftmost pad control without overlapping Gain or Pan");
    check(!interaction::isTarget(interaction::interactiveTargetAt(
              {layout::padDirection.x - 1.0f, layout::padDirection.y + 10.0f}, context),
              interaction::InteractiveType::padDirection) &&
          !interaction::isTarget(interaction::interactiveTargetAt(
              {layout::padDirection.x + 10.0f, layout::padDirection.y - 1.0f}, context),
              interaction::InteractiveType::padDirection),
          "direction hit testing rejects points outside the control");
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
    check(!interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::padGlobalMixer), context),
              interaction::InteractiveType::padGlobalMixer),
          "Global Mixer bypass belongs only to the sample editor");
    check(!interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::padDirection), context),
              interaction::InteractiveType::padDirection),
          "direction control belongs only to the sample editor");
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
          !interaction::interactiveTargetAt(center(layout::chopAction), context).valid() &&
          !interaction::interactiveTargetAt(center(layout::chopPrevious), context).valid() &&
          !interaction::interactiveTargetAt(center(layout::chopNext), context).valid(),
          "cut editor exposes Exit while disabled edit action and navigation are inert");
    check(layout::chopArrowTipX(layout::chopPrevious, false) <
              layout::chopArrowTailX(layout::chopPrevious, false) &&
          layout::chopArrowTipX(layout::chopNext, true) >
              layout::chopArrowTailX(layout::chopNext, true),
          "cut editor arrow glyphs point toward their navigation direction");
    context.chopActionEnabled = true;
    context.chopPreviousEnabled = true;
    context.chopNextEnabled = true;
    check(interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopAction), context),
              interaction::InteractiveType::chopAction) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopPrevious), context),
              interaction::InteractiveType::chopPrevious) &&
          interaction::isTarget(
              interaction::interactiveTargetAt(center(layout::chopNext), context),
              interaction::InteractiveType::chopNext),
          "enabled edit action and previous/next buttons resolve independently");
    context.chopApplying = true;
    context.chopActionEnabled = false;
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
    reply.mixer.reverse = 1.0f;
    check(session.accept(reply) && session.readyFor(7) &&
          session.waveform().frames == 96000U && session.playback().start == 0.5f &&
          session.mixer().pan == -0.25f && session.mixer().reverse == 1.0f &&
          !session.accept(reply),
          "matching integrated reply commits all controls once");
    session.complete();
    check(!session.pending() && !session.accept(reply), "completed sample requests ignore late retry replies");
    session.begin(8);
    check(!session.accept(reply) && !session.readyFor(8), "pad navigation rejects an earlier integrated reply");
    session.beginLocalEdit(7, playback, mixer);
    reply.request = session.request();
    reply.waveform = waveform;
    reply.playback = midichopper::ui::mirrorSampleRegion(playback);
    reply.mixer = mixer;
    check(!session.accept(reply) && session.pending() && !session.readyFor(7),
          "optimistic marker edits reject same-sequence snapshots made before audio commits");
    reply.playback = playback;
    reply.mixer.reverse = 1.0f;
    check(!session.accept(reply),
          "optimistic direction edits reject an old orientation despite matching markers");
    reply.mixer = mixer;
    reply.mixer.gainDecibels = 3.0f;
    check(!session.accept(reply),
          "a coherent direction reply must also preserve the desired effects settings");
    reply.mixer = mixer;
    reply.mixer.globalMixer = 0.0f;
    check(!session.accept(reply),
          "optimistic mixer edits reject a stale per-pad global bypass setting");
    reply.mixer = mixer;
    const auto previousLocal = reply;
    auto reverseMixer = mixer;
    reverseMixer.reverse = 1.0f;
    session.beginLocalEdit(7, midichopper::ui::mirrorSampleRegion(playback),
                           reverseMixer);
    check(!session.accept(previousLocal), "rapid direction clicks invalidate earlier snapshot sequences");
    reply.request = session.request();
    reply.playback = midichopper::ui::mirrorSampleRegion(playback);
    reply.mixer.reverse = 1.0f;
    check(session.accept(reply) && session.localEdit() && session.readyFor(7),
          "a coherent reply confirms the most recent optimistic direction and markers");
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

void globalValueModes()
{
    using namespace midichopper::ui;
    using namespace midichopper::plugin;
    for (int index = 1; index <= 4; ++index) {
        const auto knob = target(InteractiveType::globalMixerKnob, index);
        const auto label = target(InteractiveType::globalMixerValueLabel, index);
        const auto parameter = globalValueParameterForTarget(knob);
        check(parameter != kParameterCount && globalValueParameterForTarget(label) == parameter,
              "global continuous knobs and labels address the same private settings");
        check(globalValueParameterForTarget(target(InteractiveType::mixerKnob, index)) ==
                  kParameterCount, "pad knobs have no global settings");
    }
    check(globalValueParameterForTarget(target(InteractiveType::levelFader, 1)) ==
              kParameterOutputGainDb &&
          globalValueParameterForTarget(target(InteractiveType::levelValueLabel, 1)) ==
              kParameterOutputGainDb &&
          globalValueParameterForTarget(target(InteractiveType::levelFader, 0)) == kParameterCount &&
          globalValueParameterForTarget(target(InteractiveType::globalMixerKnob, 5)) == kParameterCount &&
          globalValueParameterForTarget(target(InteractiveType::globalMixerKnob, 6)) == kParameterCount,
          "OUT offers settings while IN and discrete mixer controls do not");
    InteractionContext context;
    context.globalControlMenu.open(kParameterGlobalTuneSemitones, {1240.0f, 730.0f});
    context.globalControlSettings = defaultGlobalValueSettings(kParameterGlobalTuneSemitones);
    const auto center = [](const sms::ui::Rect rect) {
        return sms::ui::Point{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    };
    const auto parent = context.globalControlMenu.menu;
    check(!interactiveTargetAt(center(parent.item(0)), context).valid() &&
          isTarget(interactiveTargetAt(center(parent.item(1)), context),
                   InteractiveType::globalSettingsGroup, 1) &&
          !interactiveTargetAt(center(layout::levelFader(1)), context).valid(),
          "header is not interactive, groups have distinct hits, overlay blocks underlying controls");
    context.globalControlMenu.select(GlobalSettingsSubmenu::valueMode);
    for (int row = 0; row < 3; ++row)
        check(isTarget(interactiveTargetAt(center(context.globalControlMenu.submenu.item(row)), context),
                       InteractiveType::valueModeItem, row),
              "Normal, Quantized and Zero Snap are independently selectable");
    context.globalControlSettings = {GlobalValueMode::normal, 1.0f, 6.0f};
    check(!interactiveTargetAt(center(context.globalControlMenu.submenu.item(2)), context).valid(),
          "Zero Snap is disabled for ranges excluding zero");
    context.globalControlSettings = {GlobalValueMode::normal, 0.1f, 0.2f};
    check(!interactiveTargetAt(center(context.globalControlMenu.submenu.item(1)), context).valid(),
          "Quantized is disabled when a range contains no semitone step");
    context.globalControlMenu.select(GlobalSettingsSubmenu::range);
    for (int row = 0; row < 2; ++row)
        check(isTarget(interactiveTargetAt(center(context.globalControlMenu.submenu.item(row)), context),
                       InteractiveType::globalRangeEntry, row),
              "Minimum and Maximum rows support distinct numeric entries");
    const auto separator = context.globalControlMenu.submenu.item(2);
    check(separator.height < context.globalControlMenu.submenu.item(1).height &&
          !interactiveTargetAt(center(separator), context).valid(),
          "Range separator is shorter than value rows and cannot be selected");
    context.globalControlSettings = defaultGlobalValueSettings(kParameterGlobalTuneSemitones);
    const auto resetPoint = center(context.globalControlMenu.submenu.item(3));
    check(!context.globalControlMenu.rangeResetEnabled(context.globalControlSettings) &&
          !interactiveTargetAt(resetPoint, context).valid(),
          "factory range disables Reset");
    context.globalControlSettings.mode = GlobalValueMode::zeroSnap;
    check(!context.globalControlMenu.rangeResetEnabled(context.globalControlSettings),
          "mode changes alone cannot enable range Reset");
    context.globalControlSettings.minimum = -6.0f;
    context.globalControlSettings.maximum = 6.0f;
    check(isTarget(interactiveTargetAt(resetPoint, context), InteractiveType::globalRangeReset),
          "a changed range enables the distinct Reset action");
    const auto minRestored = context.globalControlMenu.originalRangeSettings(context.globalControlSettings, 0);
    check(minRestored.minimum == -24.0f && minRestored.maximum == 6.0f &&
          minRestored.mode == GlobalValueMode::zeroSnap &&
          context.globalControlMenu.rangeResetEnabled(minRestored),
          "restoring Min preserves Max and value mode, leaving range Reset enabled");
    const auto maxRestored = context.globalControlMenu.originalRangeSettings(context.globalControlSettings, 1);
    check(maxRestored.minimum == -6.0f && maxRestored.maximum == 24.0f &&
          maxRestored.mode == GlobalValueMode::zeroSnap,
          "restoring Max preserves Min and value mode");
    const auto restored = context.globalControlMenu.originalRangeSettings(context.globalControlSettings);
    check(restored.minimum == -24.0f && restored.maximum == 24.0f &&
          restored.mode == GlobalValueMode::zeroSnap &&
          !context.globalControlMenu.rangeResetEnabled(restored),
          "range Reset restores both bounds while retaining value mode");
    context.globalControlMenu.rangeResetArmed = true;
    context.globalControlMenu.select(GlobalSettingsSubmenu::valueMode);
    check(!context.globalControlMenu.rangeResetArmed, "switching submenus cancels range confirmation");
    context.globalControlMenu.select(GlobalSettingsSubmenu::range);
    const auto submenu = context.globalControlMenu.submenu.bounds();
    check(submenu.x >= layout::contentBounds.x && submenu.y >= layout::contentBounds.y &&
          submenu.x + submenu.width <= layout::contentBounds.width &&
          submenu.y + submenu.height <= layout::contentBounds.height,
          "settings submenus remain visible near canvas edges");

    for (const auto& control : kGlobalValueControls) {
        GlobalControlMenu menu;
        menu.open(control.parameter, {});
        auto settings = defaultGlobalValueSettings(control.parameter);
        const auto original = parameterRange(control.parameter);
        settings.mode = GlobalValueMode::quantized;
        settings.minimum = original.minimum + (original.maximum - original.minimum) * 0.25f;
        check(menu.rangeResetEnabled(settings), "one modified bound enables range Reset for each control");
        const auto reset = menu.originalRangeSettings(settings);
        check(reset.minimum == original.minimum && reset.maximum == original.maximum &&
              reset.mode == settings.mode && !menu.rangeResetEnabled(reset),
              "each control restores its own original bounds and retains its mode");
        menu.rangeResetArmed = true;
        menu.close();
        check(!menu.rangeResetArmed, "dismissing the menu cancels range confirmation");
    }
    auto tune = defaultGlobalValueSettings(kParameterGlobalTuneSemitones);
    tune.mode = GlobalValueMode::quantized;
    auto lowpass = defaultGlobalValueSettings(kParameterGlobalLowpass);
    lowpass.mode = GlobalValueMode::quantized;
    for (const auto adjustment : {KnobAdjustment::normal, KnobAdjustment::fine,
                                 KnobAdjustment::stepped}) {
        check(globalKnobWheelAdjustedValue(kParameterGlobalTuneSemitones, 0.3f,
                  1.0f, 0.25f, adjustment, tune) == 1.0f &&
              globalKnobWheelAdjustedValue(kParameterGlobalTuneSemitones, 0.3f,
                  -1.0f, 0.25f, adjustment, tune) == -1.0f,
              "quantized wheel steps from audible tune regardless of modifiers");
        check(std::abs(globalKnobWheelAdjustedValue(kParameterGlobalLowpass, 0.16f,
                  1.0f, 0.025f, adjustment, lowpass) - 0.3f) < 1.0e-6f,
              "quantized filters use Ctrl+wheel tenths");
        tune.minimum = -6.0f; tune.maximum = 6.0f;
        const float wheel = globalKnobWheelAdjustedValue(kParameterGlobalTuneSemitones, 0.3f,
            1.0f, 0.25f, adjustment, tune);
        check(std::abs(wheel - 4.0f) < 1.0e-6f &&
              effectiveGlobalValue(kParameterGlobalTuneSemitones, wheel, tune) == 1.0f,
              "narrow range wheel edits invert to the original host range");
        const float drag = globalKnobDraggedValue(kParameterGlobalTuneSemitones,
            0.3f, 100.0f, 99.0f, 1.0f, adjustment, tune);
        check(std::abs(drag - 0.7f) < 1.0e-5f &&
              effectiveGlobalValue(kParameterGlobalTuneSemitones, drag, tune) == 0.0f,
              "quantized drag retains continuous raw precision and ignores modifiers");
        tune = defaultGlobalValueSettings(kParameterGlobalTuneSemitones);
        tune.mode = GlobalValueMode::quantized;
    }
    tune = defaultGlobalValueSettings(kParameterGlobalTuneSemitones);
    check(std::abs(globalKnobWheelAdjustedValue(kParameterGlobalTuneSemitones, 0.3f,
              1.0f, 0.25f, KnobAdjustment::fine, tune) - 0.325f) < 1.0e-6f &&
          globalKnobWheelAdjustedValue(kParameterGlobalTuneSemitones, 0.3f,
              1.0f, 0.25f, KnobAdjustment::stepped, tune) == 1.0f,
          "Normal preserves continuous fine and directional Ctrl+wheel behavior");
    for (const auto mode : {GlobalValueMode::normal, GlobalValueMode::quantized, GlobalValueMode::zeroSnap}) {
        const GlobalValueSettings settings{mode, -6.0f, 6.0f};
        const auto bounds = layout::levelFader(1);
        const auto track = layout::levelFaderTrack(bounds);
        for (const float raw : {-12.3f, -0.3f, 6.3f}) {
            const float displayed = effectiveGlobalValue(kParameterOutputGainDb, raw, settings);
            const float y = track.y + track.height *
                (1.0f - (displayed - settings.minimum) / (settings.maximum - settings.minimum));
            const float grab = globalLevelFaderGrabY(kParameterOutputGainDb, raw, y, bounds, settings);
            const float mapped = levelFaderValueAtY(y - grab, bounds,
                {0.0f, settings.minimum, settings.maximum});
            auto linear = settings; linear.mode = GlobalValueMode::normal;
            check(std::abs(rawGlobalValue(kParameterOutputGainDb, mapped, linear) - raw) < 1.0e-5f,
                  "grabbing the displayed OUT cap preserves raw and audible values without a jump");
        }
    }
    // Hosts can echo the native parameter through normalized floats. Each wheel
    // event must advance exactly one audible grid point despite both round trips.
    for (const auto& control : kGlobalValueControls) {
        const auto parameter = control.parameter;
        const auto native = parameterRange(parameter);
        const auto hostEcho = [native](const float raw) {
            const float normalized = (raw - native.minimum) / (native.maximum - native.minimum);
            return native.minimum + normalized * (native.maximum - native.minimum);
        };
        const bool unitStep = control.step == 1.0f;
        const float asymmetricMinimum = unitStep ? -7.25f : native.minimum < 0 ? -0.73f : 0.13f;
        const float asymmetricMaximum = unitStep ? 8.125f : 0.86f;
        const float narrowMinimum = unitStep ? -0.2f : native.minimum < 0 ? -0.12f : 0.17f;
        const float narrowMaximum = unitStep ? 2.2f : 0.32f;
        for (const auto mode : {GlobalValueMode::normal, GlobalValueMode::quantized,
                               GlobalValueMode::zeroSnap}) {
            for (const int rangeCase : {0, 1, 2, 3}) {
                const bool narrow = rangeCase == 1;
                const GlobalValueSettings settings{mode,
                    rangeCase == 2 ? native.minimum :
                    rangeCase == 3 ? (native.minimum < 0.0f ? native.minimum * 0.93f : 0.0f) :
                    mode == GlobalValueMode::zeroSnap && native.minimum == 0.0f ? 0.0f :
                        narrow ? narrowMinimum : asymmetricMinimum,
                    rangeCase == 2 ? native.maximum : rangeCase == 3 ? native.maximum * 0.89f :
                        narrow ? narrowMaximum : asymmetricMaximum};
                check(validGlobalValueSettings(parameter, settings), "wheel regression range is valid");
                const auto low = static_cast<int>(globalGridMinimum(settings, control.step));
                const auto high = static_cast<int>(globalGridMaximum(settings, control.step));
                const float tolerance = unitStep ? 0.00005f : 0.000002f;
                for (const auto adjustment : {KnobAdjustment::normal, KnobAdjustment::fine,
                                             KnobAdjustment::stepped}) {
                    if (mode != GlobalValueMode::quantized && adjustment != KnobAdjustment::stepped)
                        continue;
                    for (const bool echo : {false, true}) {
                        const auto wheel = [&](float raw, const float direction) {
                            raw = globalKnobWheelAdjustedValue(parameter, raw, direction,
                                control.step * 0.25f, adjustment, settings);
                            return echo ? hostEcho(raw) : raw;
                        };
                        // Begin at the lowest grid point, cross zero in both directions,
                        // and repeat to catch errors accumulating after each inversion.
                        float raw = rawGlobalValue(parameter, static_cast<float>(low) * control.step,
                                                   settings);
                        if (echo) raw = hostEcho(raw);
                        for (int pass = 0; pass < 3; ++pass) {
                            for (int grid = low + 1; grid <= high; ++grid) {
                                raw = wheel(raw, 1.0f);
                                check(std::abs(effectiveGlobalValue(parameter, raw, settings) -
                                               static_cast<float>(grid) * control.step) < tolerance,
                                      "custom range wheel advances one audible grid point without repeats or skips");
                            }
                            for (int grid = high - 1; grid >= low; --grid) {
                                raw = wheel(raw, -1.0f);
                                check(std::abs(effectiveGlobalValue(parameter, raw, settings) -
                                               static_cast<float>(grid) * control.step) < tolerance,
                                      "custom range wheel descends one audible grid point without repeats or skips");
                            }
                        }
                        // Stepped edits stop at the outermost in-range grid point,
                        // even when the configured endpoints are fractional.
                        for (const float direction : {-1.0f, 1.0f}) {
                            raw = direction < 0.0f ? native.minimum : native.maximum;
                            for (int event = 0; event < 3; ++event) raw = wheel(raw, direction);
                            const float expected = static_cast<float>(direction < 0.0f ? low : high) * control.step;
                            check(std::abs(effectiveGlobalValue(parameter, raw, settings) - expected) < tolerance,
                                  "repeated outward wheel remains at the outermost in-range grid point");
                            raw = wheel(raw, -direction);
                            const int grid = std::clamp(direction < 0.0f ? low + 1 : high - 1, low, high);
                            check(std::abs(effectiveGlobalValue(parameter, raw, settings) -
                                           static_cast<float>(grid) * control.step) < tolerance,
                                  "wheel reverses from the selected boundary without skipping the first grid point");
                        }
                    }
                }
            }
        }
    }
    for (const auto& control : kGlobalValueControls) {
        const GlobalValueSettings noGrid{GlobalValueMode::normal, control.step * 0.2f,
                                        control.step * 0.9f};
        check(validGlobalValueSettings(control.parameter, noGrid), "continuous range without a grid is valid");
        const auto native = parameterRange(control.parameter);
        for (const float raw : {native.minimum, native.defaultValue, native.maximum})
            for (const float direction : {-1.0f, 1.0f})
                check(globalKnobWheelAdjustedValue(control.parameter, raw, direction,
                          control.step * 0.25f, KnobAdjustment::stepped, noGrid) == raw,
                      "Ctrl wheel preserves raw exactly when the custom range contains no grid point");
    }
    const GlobalValueSettings fractionalOutput{GlobalValueMode::normal, -22.32f, 10.68f};
    const float outputGrid = rawGlobalValue(kParameterOutputGainDb, 10.0f, fractionalOutput);
    const float outputWheel = globalKnobWheelAdjustedValue(kParameterOutputGainDb, outputGrid,
        1.0f, 0.5f, KnobAdjustment::stepped, fractionalOutput);
    check(std::abs(effectiveGlobalValue(kParameterOutputGainDb, outputWheel, fractionalOutput) - 10.0f) < 0.00001f,
          "Ctrl wheel stops at 10 rather than the fractional 10.68 output endpoint");
    tune = {GlobalValueMode::zeroSnap, -6.0f, 6.0f};
    const float wheel = globalKnobWheelAdjustedValue(kParameterGlobalTuneSemitones,
        0.3f, 1.0f, 0.25f, KnobAdjustment::fine, tune);
    check(std::abs(effectiveGlobalValue(kParameterGlobalTuneSemitones, wheel, tune) - 0.025f) < 1.0e-6f,
          "Zero Snap wheel edits leave the plateau in audible units without dead wheel events");
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

void chopEditorAutomaticSave()
{
    using namespace std::chrono_literals;
    namespace plugin = midichopper::plugin;
    const auto now = midichopper::ui::ChopEditorController::Clock::time_point{} + 1s;
    // Exercise Exit, Prev and Next in a later bank, including 8/12/16-pad layouts.
    for (const int visible : {8, 12, 16}) {
        const int bankFirst = visible;
        for (const int direction : {0, -1, 1}) {
            for (const bool success : {true, false}) {
                ChopHost host;
                midichopper::ui::ChopEditorController editor(host);
                const int target = bankFirst + 3;
                editor.open(target, bankFirst, visible);
                const auto load = [&] {
                    editor.idle(now);
                    plugin::ChopSnapshotReply reply;
                    check(plugin::decodeChopSnapshotRequest(
                              host.last("chop_snapshot_request"), reply.request),
                          "automatic-save workflow requests a coherent baseline");
                    for (std::size_t pad = 0; pad < reply.waveforms.size(); ++pad) {
                        reply.waveforms[pad].pad = reply.request.firstPad + pad;
                        reply.waveforms[pad].frames = 100U;
                        reply.waveforms[pad].sampleRate = 1000.0;
                        reply.generations[pad] = 10U + pad;
                    }
                    const auto encoded = plugin::encodeChopSnapshotReply(reply);
                    check(editor.acceptState("chop_snapshot_data", encoded.c_str()) && editor.ready(),
                          "automatic-save workflow accepts its baseline");
                    return reply;
                };
                const auto baseline = load();
                sms::ui::waveform::Viewport viewport;
                viewport.reset(300U);
                const auto edit = [&] {
                    editor.beginBoundaryDrag(0, 0.0f);
                    editor.dragBoundary(25.0f, 300.0f, viewport);
                    editor.endBoundaryDrag();
                    editor.beginBoundaryDrag(1, 0.0f);
                    editor.dragBoundary(-10.0f, 300.0f, viewport);
                    editor.endBoundaryDrag();
                };
                const auto leave = [&] {
                    if (direction == 0)
                        editor.exit();
                    else
                        editor.navigate(direction, bankFirst, visible);
                };
                edit();
                editor.preview(1);
                const auto messages = host.messages.size();
                editor.cancelChanges();
                plugin::ChopMidiPreviewRequest midi;
                plugin::ChopPreviewRequest preview;
                check(editor.ready() && !editor.dirty() && editor.offsets()[0] == 0 &&
                      editor.offsets()[1] == 0 && editor.activeBoundary() == -1 &&
                      host.selectedPad == target && host.viewportFrames == 300U &&
                      plugin::decodeChopPreviewRequest(host.last("chop_preview_request"), preview) &&
                      !preview.play && editor.previewPad() == -1 &&
                      plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
                      midi.sourceEndFrames[0] == 100U && midi.sourceFrames[1] == 100U &&
                      midi.sourceEndFrames[1] == 200U,
                      "Cancel Changes restores both loaded cuts and raw preview without leaving");
                for (std::size_t i = messages; i < host.messages.size(); ++i)
                    check(host.messages[i].first != "chop_apply_request" &&
                          host.messages[i].first != "chop_snapshot_request",
                          "Cancel Changes neither rewrites audio nor reloads the baseline");
                const auto cleanMessages = host.messages.size();
                editor.cancelChanges();
                check(host.messages.size() == cleanMessages,
                      "Cancel Changes is inert when there are no edits");
                edit();
                leave();
                plugin::ChopApplyRequest apply;
                check(editor.applying() && editor.active() && host.selectedPad == target &&
                      host.exits == 0 &&
                      plugin::decodeChopApplyRequest(host.last("chop_apply_request"), apply) &&
                      apply.sequence == baseline.request.sequence &&
                      apply.firstPad == static_cast<unsigned>(target - 1) &&
                      apply.boundaryOffsets[0] == 25 && apply.boundaryOffsets[1] == -10 &&
                      apply.expectedGenerations[0] == 10U && apply.revisionChecked &&
                      plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
                      midi.active && midi.previewPadCount == 0U,
                      "Exit/Prev/Next save guarded cuts and wait with MIDI muted");
                const auto applyingMessages = host.messages.size();
                leave();
                editor.exit();
                editor.navigate(-1, bankFirst, visible);
                editor.cancelChanges();
                edit();
                check(host.messages.size() == applyingMessages && editor.offsets()[0] == 25 &&
                      editor.offsets()[1] == -10,
                      "in-flight saves ignore repeated actions and further boundary edits");
                auto status = plugin::encodeChopStatus(apply.sequence + 1U, true, {});
                check(!editor.acceptState("chop_status", status.c_str()) && editor.applying(),
                      "unmatched save results cannot execute a pending Exit or navigation");
                status = plugin::encodeChopStatus(apply.sequence, success, "Samples changed");
                check(editor.acceptState("chop_status", status.c_str()) && !editor.applying() &&
                      !editor.acceptState("chop_status", status.c_str()),
                      "only the matching save result completes a pending transition once");
                if (success && direction == 0) {
                    check(!editor.active() && host.exits == 1 &&
                          plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
                          !midi.active,
                          "successful Exit releases exclusive MIDI and closes the editor");
                    continue;
                }
                check(editor.active() && !editor.ready() && !editor.dirty() && host.exits == 0 &&
                      host.selectedPad == (success ? target + direction : target) &&
                      host.viewportFrames == 0U &&
                      plugin::decodeChopMidiPreviewRequest(host.last("chop_midi_preview"), midi) &&
                      midi.active && midi.previewPadCount == 0U,
                      "navigation loads its destination only on success; failure reloads the current pad");
                if (!success)
                    check(host.status == "Samples changed", "save failure displays the rejection reason");
                const auto refreshed = load();
                check(refreshed.request.sequence != baseline.request.sequence && !editor.dirty(),
                      "navigation or rejection obtains a fresh baseline identity");
                const auto beforeCleanLeave = host.messages.size();
                leave();
                check(!editor.applying() && (direction != 0 || !editor.active()),
                      "leaving without edits completes immediately, including after a rejected save");
                for (std::size_t i = beforeCleanLeave; i < host.messages.size(); ++i)
                    check(host.messages[i].first != "chop_apply_request",
                          "clean Exit/Prev/Next never rewrite audio");
                check(host.exits == (direction == 0 ? 1 : 0),
                      "a rejected save never leaves a delayed Exit behind");
                if (success && direction == 1 && visible == 16) {
                    (void)load();
                    edit();
                    editor.exit();
                    check(plugin::decodeChopApplyRequest(host.last("chop_apply_request"), apply),
                          "forced invalidation starts with a pending saved Exit");
                    editor.cancel();
                    editor.open(target, bankFirst, visible);
                    status = plugin::encodeChopStatus(apply.sequence, true, {});
                    check(!editor.acceptState("chop_status", status.c_str()) &&
                          editor.active() && host.selectedPad == target && host.exits == 1,
                          "forced cancellation prevents late completion from exiting a reopened editor");
                }
            }
        }
    }
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
    editor.exit();
    check(!editor.active() &&
          plugin::decodePadStructureRequest(host.last("pad_structure_request"), apply) &&
          apply.action == plugin::PadStructureAction::cancelSplit,
          "Split Exit still cancels insertion without applying it");
    editor.openSplit(plan);
    const auto count = host.messages.size();
    check(editor.acceptSplitStatus("PS1;E;42;Sample changed", 0, 16) &&
          !editor.active() && editor.splitPlanId() == 0U && host.status == "Sample changed",
          "a matching split error releases its plan and exits the workflow");
    for (std::size_t index = count; index < host.messages.size(); ++index)
        check(host.messages[index].first != "pad_structure_request",
              "failed split plans are not cancelled again after the DSP discarded them");
}

void waveformVisibleSummary()
{
    using sms::audio::WaveformSummary;
    using namespace sms::ui::waveform;
    WaveformSummary overview;
    overview.pad = 7U;
    overview.frames = 4096U;
    overview.sampleRate = 44100.0;
    overview.minimum[40] = -0.75f;
    overview.maximum[40] = 0.875f;
    const auto same = [](const WaveformSummary& left, const WaveformSummary& right) {
        return left.pad == right.pad && left.frames == right.frames &&
            left.sampleRate == right.sampleRate && left.minimum == right.minimum &&
            left.maximum == right.maximum;
    };
    CachedDetail cached{overview, 1024U, 2048U};
    cached.waveform.frames = 1024U;
    cached.waveform.minimum.fill(0.0f);
    cached.waveform.maximum.fill(0.0f);
    cached.waveform.minimum[32] = -0.5f;
    cached.waveform.maximum[32] = 0.625f;
    check(same(visibleSummary(overview, {4096U, 0U, 4096U}, &cached), overview) &&
          same(visibleSummary(overview, {}, &cached), overview),
          "full and uninitialized viewports preserve the whole-source summary");
    check(same(visibleSummary(overview, {4096U, 1024U, 2048U}, &cached),
               cached.waveform),
          "a matching detail range preserves its summary exactly");

    const auto fallback = visibleSummary(overview, {4096U, 1024U, 2048U});
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const bool impulse = bin >= 32U && bin < 36U;
        check(fallback.minimum[bin] == (impulse ? -0.75f : 0.0f) &&
              fallback.maximum[bin] == (impulse ? 0.875f : 0.0f),
              "overview zoom relocates the impulse using source-frame coverage");
    }
    auto mismatchedFrames = overview;
    mismatchedFrames.frames = 1024U;
    const auto concatenated = visibleSummary(mismatchedFrames, {4096U, 1024U, 2048U});
    check(concatenated.minimum == fallback.minimum &&
          concatenated.maximum == fallback.maximum,
          "overview bin coverage follows viewport total for concatenated sources");

    overview.minimum.fill(0.0f);
    overview.maximum.fill(0.0f);
    overview.minimum[70] = -0.875f;
    overview.maximum[70] = 0.75f;
    cached.waveform.minimum.fill(0.0f);
    cached.waveform.maximum.fill(0.0f);
    cached.waveform.minimum[80] = -0.625f;
    cached.waveform.maximum[80] = 0.5f;
    const auto overlap = visibleSummary(overview, {4096U, 1536U, 2560U}, &cached);
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const float low = bin == 16U ? -0.625f :
            (bin >= 88U && bin < 92U ? -0.875f : 0.0f);
        const float high = bin == 16U ? 0.5f :
            (bin >= 88U && bin < 92U ? 0.75f : 0.0f);
        check(overlap.minimum[bin] == low && overlap.maximum[bin] == high,
              "pan maps covered detail and uncovered overview to their new positions");
    }
    check(same(visibleSummary(overview, {4096U, 2048U, 3072U}, &cached),
               visibleSummary(overview, {4096U, 2048U, 3072U})),
          "disjoint detail falls back to the whole-source overview");

    // At 16 frames per output bin, adjacent 8-frame detail bins must merge.
    overview.minimum.fill(-0.125f);
    overview.maximum.fill(0.125f);
    cached.waveform.minimum.fill(-0.25f);
    cached.waveform.maximum.fill(0.25f);
    cached.waveform.minimum[32] = -0.875f;
    cached.waveform.maximum[33] = 0.75f;
    const auto zoomOut = visibleSummary(overview, {4096U, 512U, 2560U}, &cached);
    check(zoomOut.minimum[48] == -0.875f && zoomOut.maximum[48] == 0.75f &&
          zoomOut.minimum[47] == -0.25f && zoomOut.maximum[49] == 0.25f &&
          zoomOut.minimum[0] == -0.125f && zoomOut.maximum[127] == 0.125f,
          "zoom-out conserves both extrema and uses finer cached data where covered");
    auto coarse = cached;
    coarse.start = 0U;
    coarse.end = 8192U;
    coarse.waveform.frames = 8192U;
    check(same(visibleSummary(overview, {4096U, 512U, 2560U}, &coarse),
               visibleSummary(overview, {4096U, 512U, 2560U})),
          "coarser cached bins cannot replace higher-resolution overview bins");
    auto boundary = cached;
    boundary.start = 1032U;
    boundary.end = 2056U;
    const auto partial = visibleSummary(overview, {4096U, 512U, 2560U}, &boundary);
    check(partial.minimum[32] == -0.125f && partial.maximum[32] == 0.125f &&
          partial.minimum[33] == -0.25f && partial.maximum[33] == 0.25f,
          "an output bin crossing the cache edge uses overview for its entire interval");

    std::array<float, 32> pcm{};
    pcm[10] = -0.75f;
    pcm[11] = 0.5f;
    const auto tiny = sms::audio::summarizeStereo(7U, pcm.data(), 16U);
    const auto tinyView = visibleSummary(tiny, {16U, 4U, 12U});
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const bool impulse = bin >= 16U && bin < 32U;
        check(tinyView.minimum[bin] == (impulse ? -0.75f : 0.0f) &&
              tinyView.maximum[bin] == (impulse ? 0.5f : 0.0f),
              "sub-128-frame summaries retain repeated sample-bin extrema");
    }
    const WaveformSummary empty{};
    check(same(visibleSummary(empty, {4096U, 1024U, 2048U}), empty),
          "an empty overview remains safe while waiting for a source snapshot");
}

void sampleWaveformOrientation()
{
    using namespace midichopper::ui;
    using namespace sms::ui::waveform;
    sms::audio::WaveformSummary raw;
    raw.pad = 7U;
    raw.frames = 4096U;
    raw.sampleRate = 1000.0;
    raw.minimum[3] = -0.875f;
    raw.maximum[3] = 0.75f;
    raw.minimum[119] = -0.25f;
    raw.maximum[119] = 0.5f;
    const auto reverse = sampleDisplaySummary(raw, true);
    check(reverse.minimum[124] == -0.875f && reverse.maximum[124] == 0.75f &&
          reverse.minimum[8] == -0.25f && reverse.maximum[8] == 0.5f &&
          reverse.frames == raw.frames && reverse.sampleRate == raw.sampleRate &&
          reverse.pad == raw.pad,
          "reverse display flips asymmetric full-source extrema and preserves metadata");
    check(sampleDisplaySummary(reverse, true).minimum == raw.minimum &&
          sampleDisplaySummary(reverse, true).maximum == raw.maximum &&
          sampleDisplaySummary(raw, false).maximum == raw.maximum,
          "direction toggles are reversible while raw display stays original");
    const Viewport display{4096U, 512U, 1536U};
    const auto source = sampleSourceViewport(display, true);
    check(source.start == 2560U && source.end == 3584U &&
          sampleSourceViewport(source, true).start == display.start &&
          sampleSourceViewport(display, false).start == display.start,
          "asymmetric zoom intervals mirror around the complete source before trimming");
    CachedDetail rawDetail{raw, source.start, source.end};
    rawDetail.waveform.frames = 1024U;
    rawDetail.waveform.minimum.fill(0.0f);
    rawDetail.waveform.maximum.fill(0.0f);
    rawDetail.waveform.minimum[9] = -0.625f;
    rawDetail.waveform.maximum[9] = 0.875f;
    const auto visible = sampleVisibleSummary(raw, display, &rawDetail, true);
    check(visible.minimum[118] == -0.625f && visible.maximum[118] == 0.875f &&
          visible.maximum[9] == 0.0f && rawDetail.waveform.maximum[9] == 0.875f &&
          rawDetail.start == source.start,
          "zoomed detail reverses bins into display coordinates without changing raw cache");
    const Viewport panned{4096U, 1024U, 2048U};
    const auto pannedSource = sampleSourceViewport(panned, true);
    const auto expected = sampleDisplaySummary(visibleSummary(raw, pannedSource, &rawDetail), true);
    const auto actual = sampleVisibleSummary(raw, panned, &rawDetail, true);
    check(actual.minimum == expected.minimum && actual.maximum == expected.maximum,
          "reverse panning reprojects retained detail coverage and coarse source fallback");
    const auto rawDisplay = sampleVisibleSummary(raw, source, &rawDetail, false);
    check(rawDisplay.minimum == rawDetail.waveform.minimum &&
          rawDisplay.maximum[9] == 0.875f,
          "cut and split raw displays retain original summary and detail orientation");
    auto oddSource = raw;
    oddSource.frames = 1003U;
    const Viewport deep{1003U, 19U, 38U};
    CachedDetail oddDetail{rawDetail.waveform, 965U, 984U};
    oddDetail.waveform.frames = 19U;
    const auto deepDisplay = sampleVisibleSummary(oddSource, deep, &oddDetail, true);
    check(deepDisplay.minimum[118] == -0.625f && deepDisplay.maximum[118] == 0.875f &&
          deepDisplay.frames == 19U,
          "odd-length sources retain exact requested detail bins at deep reverse zoom");
    sms::dsp::SamplePlaybackSettings settings;
    settings.start = 0.2f;
    settings.end = 0.7f;
    settings.attackSeconds = 0.1f;
    settings.decaySeconds = 0.2f;
    settings.releaseSeconds = 0.3f;
    const sms::ui::Rect bounds{0.0f, 0.0f, 400.0f, 100.0f};
    const auto forwardEnvelope = envelopeGeometry(bounds, raw, settings);
    const auto mirroredSettings = mirrorSampleRegion(settings);
    const auto restoredSettings = mirrorSampleRegion(mirroredSettings);
    const auto reverseEnvelope = envelopeGeometry(bounds, reverse, mirroredSettings);
    check(std::abs(mirroredSettings.start - 0.3f) < 1.0e-6f &&
          std::abs(mirroredSettings.end - 0.8f) < 1.0e-6f &&
          nearestRegionHandle(120.0f, bounds, mirroredSettings) == EditTarget::regionStart &&
          nearestRegionHandle(320.0f, bounds, mirroredSettings) == EditTarget::regionEnd &&
          std::abs(restoredSettings.start - settings.start) < 1.0e-6f &&
          std::abs(restoredSettings.end - settings.end) < 1.0e-6f &&
          mirroredSettings.attackSeconds == settings.attackSeconds &&
          mirroredSettings.decaySeconds == settings.decaySeconds &&
          mirroredSettings.sustainLevel == settings.sustainLevel &&
          mirroredSettings.releaseSeconds == settings.releaseSeconds &&
          std::abs(forwardEnvelope.attackX - reverseEnvelope.attackX) < 1.0e-4f &&
          std::abs(forwardEnvelope.decayX - reverseEnvelope.decayX) < 1.0e-4f &&
          std::abs(forwardEnvelope.releaseX - reverseEnvelope.releaseX) < 1.0e-4f,
          "direction flips mirror Start/End, restore on a second flip, and preserve ADSR settings");
    settings.start = 0.125f;
    settings.end = 0.5f;
    const auto selected = mirrorSampleRegion(settings);
    const auto selectedRaw = sampleSourceViewport({4096U,
        static_cast<std::uint64_t>(selected.start * raw.frames),
        static_cast<std::uint64_t>(selected.end * raw.frames)}, true);
    check(selected.start == 0.5f && selected.end == 0.875f &&
          selectedRaw.start == 512U && selectedRaw.end == 2048U,
          "asymmetric mirrored cuts preserve the same raw audio interval");
}

void globalReverseProjection()
{
    using namespace midichopper::ui;
    using namespace sms::ui::waveform;
    sms::dsp::SamplePlaybackSettings local;
    local.start = 0.125f;
    local.end = 0.5f;
    local.attackSeconds = 0.1f;
    local.decaySeconds = 0.2f;
    local.sustainLevel = 0.7f;
    local.releaseSeconds = 0.3f;
    sms::dsp::SampleMixerSettings mixer;
    check(mixer.globalMixer == 1.0f, "Global Mixer is enabled for a new sample");
    for (const bool pad : {false, true}) {
        for (const bool master : {false, true}) {
            mixer.reverse = pad ? 1.0f : 0.0f;
            check(sampleDisplayReversed(mixer.reverse >= 0.5f, master) == (pad != master),
                  "global Reverse XORs every pad's saved direction");
            const auto displayed = sampleEditorSettings(local, master);
            const auto persisted = sampleEditorSettings(displayed, master);
            check(displayed.start == (master ? 0.5f : 0.125f) &&
                  displayed.end == (master ? 0.875f : 0.5f) &&
                  persisted.start == local.start && persisted.end == local.end &&
                  displayed.attackSeconds == local.attackSeconds &&
                  displayed.decaySeconds == local.decaySeconds &&
                  displayed.sustainLevel == local.sustainLevel &&
                  displayed.releaseSeconds == local.releaseSeconds &&
                  local.start == 0.125f && local.end == 0.5f &&
                  mixer.reverse == (pad ? 1.0f : 0.0f),
                  "master display projection preserves saved local cuts, pad direction, and ADSR");
            const bool bypassedMaster = sampleGlobalReverse(master, 0.0f);
            const auto bypassed = sampleEditorSettings(local, bypassedMaster);
            const auto bypassedEdit = sampleEditorLocalEdit(local, bypassed,
                bypassedMaster, true, true);
            check(!bypassedMaster && sampleGlobalReverse(master, 1.0f) == master &&
                  sampleDisplayReversed(pad, bypassedMaster) == pad &&
                  bypassed.start == local.start && bypassed.end == local.end &&
                  bypassedEdit.start == local.start && bypassedEdit.end == local.end,
                  "Global Mixer bypass excludes global Reverse from waveform, cuts, and edits");
        }
    }
    const sms::ui::Rect bounds{0.0f, 0.0f, 400.0f, 100.0f};
    Viewport viewport;
    viewport.reset(4096U);
    auto displayed = sampleEditorSettings(local, true);
    check(nearestRegionHandle(200.0f, bounds, displayed, viewport) == EditTarget::regionStart &&
          nearestRegionHandle(350.0f, bounds, displayed, viewport) == EditTarget::regionEnd,
          "master-reversed marker hit testing uses the effective interval");
    updateRegion(displayed, EditTarget::regionStart, 240.0f, bounds, viewport);
    const auto dragged = sampleEditorSettings(displayed, true);
    check(dragged.start == local.start && std::abs(dragged.end - 0.4f) < 1.0e-6f &&
          dragged.attackSeconds == local.attackSeconds,
          "dragging displayed Start under master Reverse persists the corresponding local End");
    displayed = sampleEditorSettings(local, true);
    adjustRegionByWheel(displayed, EditTarget::regionEnd, 1.0f, 4096U, bounds, viewport);
    const auto wheeled = sampleEditorSettings(displayed, true);
    check(wheeled.start < local.start && wheeled.end == local.end,
          "wheeling displayed End under master Reverse updates the local Start in the opposite direction");
    const auto padFlipped = mirrorSampleRegion(local);
    const auto padFlippedDisplay = sampleEditorSettings(padFlipped, true);
    check(padFlippedDisplay.start == local.start && padFlippedDisplay.end == local.end &&
          !sampleDisplayReversed(true, true),
          "an explicit pad flip while master Reverse is on mirrors local cuts and cancels effective reversal");
    local.start = 0.2f;
    local.end = 0.7f;
    displayed = sampleEditorSettings(local, true);
    updateEnvelopeSlider(displayed, EditTarget::attackSlider, 200.0f, bounds);
    const auto envelopeEdited = sampleEditorLocalEdit(local, displayed, true, false, false);
    check(envelopeEdited.start == local.start && envelopeEdited.end == local.end &&
          envelopeEdited.attackSeconds == displayed.attackSeconds &&
          envelopeEdited.attackSeconds != local.attackSeconds,
          "ADSR edits under master Reverse preserve non-binary saved cuts exactly");
    for (const bool master : {false, true}) {
        for (const auto edge : {EditTarget::regionStart, EditTarget::regionEnd}) {
            displayed = sampleEditorSettings(local, master);
            updateRegion(displayed, edge, edge == EditTarget::regionStart ? 144.0f : 256.0f,
                         bounds, viewport);
            const bool startEdited = edge == EditTarget::regionStart;
            const auto dragEdit = sampleEditorLocalEdit(local, displayed, master,
                                                       startEdited, !startEdited);
            const bool editsLocalStart = startEdited != master;
            check(editsLocalStart ? (dragEdit.end == local.end && dragEdit.start != local.start)
                                 : (dragEdit.start == local.start && dragEdit.end != local.end),
                  "region dragging changes only its mapped local boundary with master on or off");
            displayed = sampleEditorSettings(local, master);
            adjustRegionByWheel(displayed, edge, 1.0f, 4096U, bounds, viewport);
            const auto wheelEdit = sampleEditorLocalEdit(local, displayed, master,
                                                        startEdited, !startEdited);
            check(editsLocalStart ? (wheelEdit.end == local.end && wheelEdit.start != local.start)
                                 : (wheelEdit.start == local.start && wheelEdit.end != local.end),
                  "region wheel edits preserve the other non-binary saved boundary exactly");
        }
    }
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
    reply.waveform.frames = static_cast<std::uint32_t>(request->end - request->start);
    reply.waveform.minimum.fill(-0.5f);
    reply.waveform.maximum.fill(0.75f);
    check(session.accept(reply) && session.detail() != nullptr &&
          !session.requestDue(now + 1s),
          "matching detail replies stop retries and become visible");
    check(!session.pan(0.0f, now) && session.detail() != nullptr,
          "an unchanged viewport retains its matching detail");
    check(session.pan(-1.0f, now + 1s) && session.detail() != nullptr &&
          session.detail()->start == request->start && session.detail()->end == request->end &&
          !session.accept(reply),
          "panning retains cached detail coverage and rejects the previous range reply");
    sms::audio::WaveformSummary overview;
    overview.frames = 1000U;
    overview.minimum.fill(-0.25f);
    overview.maximum.fill(0.25f);
    const auto panned = sms::ui::waveform::visibleSummary(
        overview, session.viewport(), session.detail());
    check(panned.minimum[0] == -0.5f && panned.maximum[0] == 0.75f &&
          panned.minimum.back() == -0.25f && panned.maximum.back() == 0.25f,
          "panning displays cached overlap and overview before a fresh reply");
    const auto moved = session.request(7, 3U, true, now + 1050ms);
    check(moved && moved->sequence != request->sequence,
          "a changed viewport creates a new detail identity");
    auto replacement = reply;
    replacement.request = *moved;
    replacement.waveform.minimum.fill(-0.625f);
    replacement.waveform.maximum.fill(0.875f);
    check(session.accept(replacement) && session.detail()->start == moved->start &&
          session.detail()->end == moved->end &&
          session.detail()->waveform.maximum[0] == 0.875f && !session.accept(reply),
          "a fresh reply replaces cache data and captured range while old replies stay rejected");
    for (int gesture = 0; gesture < 4; ++gesture) {
        static_cast<void>(session.zoom(0.25f, 50.0f, bounds, now + 1100ms));
        static_cast<void>(session.pan(1.0f, now + 1100ms));
        const auto visible = sms::ui::waveform::visibleSummary(
            overview, session.viewport(), session.detail());
        check(session.detail() != nullptr && session.detail()->start == moved->start &&
              session.detail()->end == moved->end &&
              std::all_of(visible.minimum.begin(), visible.minimum.end(),
                  [](const float value) { return value <= -0.25f; }) &&
              std::all_of(visible.maximum.begin(), visible.maximum.end(),
                  [](const float value) { return value >= 0.25f; }) &&
              !session.accept(replacement),
              "continuous pan and zoom retain visible extrema and reject stale replies");
    }
    session.reset(1000U);
    check(session.detail() == nullptr && !session.accept({*moved, {}}) &&
          !session.requestDue(now + 2s),
          "source resets invalidate previous replies and pending retries");
    check(session.setZoomPosition(0.5f, now + 2s) &&
          !session.request(7, 1U, false, now + 2050ms) &&
          !session.requestDue(now + 3s),
          "hidden editors cancel detail retries");

    session.reset(4096U);
    check(session.zoom(2.0f, 25.0f, bounds, now + 4s),
          "orientation tests use an asymmetric display range");
    const auto forward = session.request(7, 1U, true, now + 4050ms);
    check(forward && forward->start == 768U && forward->end == 1792U,
          "forward detail uses its displayed source range");
    midichopper::plugin::WaveformDetailReply original{*forward, {}};
    original.waveform.frames = 1024U;
    original.waveform.maximum[4] = 0.75f;
    check(session.accept(original), "forward detail becomes a raw retained cache");
    session.setReversed(true, now + 4100ms);
    check(session.viewport().start == 768U && session.viewport().end == 1792U &&
          session.detail()->start == 768U && session.detail()->end == 1792U &&
          session.detail()->waveform.maximum[4] == 0.75f && !session.accept(original),
          "direction toggles retain display range and raw cache but reject old replies");
    const auto reversed = session.request(7, 1U, true, now + 4150ms);
    check(reversed && reversed->start == 2304U && reversed->end == 3328U &&
          reversed->sequence != forward->sequence,
          "reverse detail fetches the mirrored full-source interval");
    midichopper::plugin::WaveformDetailReply mirrored{*reversed, {}};
    mirrored.waveform.frames = 1024U;
    mirrored.waveform.maximum[8] = 0.875f;
    check(session.accept(mirrored) && session.detail()->start == 2304U &&
          session.detail()->waveform.maximum[8] == 0.875f,
          "reverse detail replies remain tagged and stored in raw coordinates");
    session.setReversed(false, now + 4200ms);
    const auto rawEditor = session.request(7, 3U, true, now + 4250ms);
    check(rawEditor && rawEditor->start == 768U && rawEditor->end == 1792U &&
          rawEditor->padCount == 3U && !session.accept(mirrored),
          "raw cut editor requests original coordinates and rejects reverse-mode replies");
    const auto rawSplit = session.request(7, 1U, true, now + 4300ms);
    check(rawSplit && rawSplit->start == 768U && rawSplit->end == 1792U,
          "raw split editor also requests the original source interval");
}

void editorAuditionModes()
{
    using namespace midichopper::ui;
    auto mode = EditorAuditionMode::Play;
    check(editorAuditionPressed(mode) && std::string_view(editorAuditionLabel(mode)) == "PLAY ON SELECT",
          "the default finite Play mode displays a pressed Play On Select button");
    mode = nextEditorAuditionMode(mode);
    check(mode == EditorAuditionMode::Loop && editorAuditionPressed(mode) &&
          std::string_view(editorAuditionLabel(mode)) == "LOOP ON SELECT",
          "the second state is pressed Loop On Select");
    mode = nextEditorAuditionMode(mode);
    check(mode == EditorAuditionMode::Off && !editorAuditionPressed(mode) &&
          std::string_view(editorAuditionLabel(mode)) == "PLAY ON SELECT" &&
          nextEditorAuditionMode(mode) == EditorAuditionMode::Play,
          "Off is unpressed and cycles back to finite Play");
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
    midichopper::ui::PlaybackIndicator bypassedIndicator;
    auto bypassedMixer = mixer;
    bypassedMixer.globalMixer = 0.0f;
    check(bypassedIndicator.start(7, 7, playback, now) &&
          bypassedIndicator.update(now + 100ms, 7, true, waveform, playback,
              bypassedMixer, 12.0f) &&
          std::abs(bypassedIndicator.position() - 8.35f) < 1.0e-5f,
          "playhead prediction excludes global tuning for a bypassed sample");
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
    check(indicator.start(7, 7, playback, now + 4s, true) &&
          indicator.update(now + 4750ms, 7, true, waveform, playback, mixer, 0.0f) &&
          std::abs(indicator.position() - 8.5f) < 1.0e-5f,
          "looping playheads retain overshoot when wrapping the edited region");
    check(indicator.update(now + 6500ms, 7, true, waveform, playback, mixer, 0.0f) &&
          std::abs(indicator.position() - 8.25f) < 1.0e-5f,
          "looping estimation remains active across several complete cycles");
    auto editedPlayback = playback;
    editedPlayback.start = 0.5f;
    editedPlayback.end = 0.7f;
    check(indicator.update(now + 7750ms, 7, true, waveform, editedPlayback, mixer, 0.0f) &&
          std::abs(indicator.position() - 8.6f) < 1.0e-5f,
          "loop estimation retains voice Start until retrigger and follows live End edits");
    indicator.acceptHostPosition(8.6f, now + 8s, true);
    check(indicator.position() == 8.6f &&
          !indicator.update(now + 8300ms, 7, true, waveform, playback, mixer, 0.0f) &&
          indicator.position() == 8.6f,
          "DSP positions disable direction assumptions between host notifications");
    mixer.reverse = 1.0f;
    check(!indicator.update(now + 8400ms, 7, true, waveform, playback, mixer, 0.0f) &&
          indicator.position() == 8.6f,
          "passive mixer settings cannot replace an explicit active-direction action");
    indicator.acceptHostPosition(8.7f, now + 8500ms, true);
    check(indicator.position() == 8.7f &&
          !indicator.update(now + 8600ms, 7, true, waveform, playback, mixer, 0.0f) &&
          indicator.position() == 8.7f,
          "reverse voices retain increasing DSP display positions without mirroring the playhead");
    indicator.acceptHostPosition(0.0f, now + 9s, true);
    check(indicator.position() == 0.0f &&
          !indicator.update(now + 10s, 7, true, waveform, playback, mixer, 0.0f),
          "host Stop immediately overrides an estimated loop");
    check(indicator.start(7, 7, playback, now + 11s, true, false) &&
          indicator.position() == 0.0f &&
          !indicator.update(now + 11500ms, 7, true, waveform, playback, mixer, 0.0f),
          "unknown voice direction waits for DSP position instead of guessing from settings");
    indicator.acceptSettings(7, playback, now + 11600ms);
    check(indicator.position() == 0.0f,
          "settings snapshots cannot create a guessed playhead for unknown voice direction");
    indicator.acceptHostPosition(8.7f, now + 11700ms, true);
    indicator.stop(true, now + 11800ms);
    check(indicator.position() == 8.7f &&
          indicator.update(now + 11900ms, 7, true, waveform, playback, mixer, 0.0f) &&
          indicator.position() == 0.0f,
          "DSP-driven positions retain the existing final-position hold and clear timing");

    sms::dsp::SamplePlaybackSettings cut;
    cut.start = 0.125f;
    cut.end = 0.5f;
    const auto mirroredCut = midichopper::ui::mirrorSampleRegion(cut);
    indicator.acceptHostPosition(8.0f + 1.0f / 3.0f, now + 12s, true);
    const float beforeFlip = indicator.position();
    indicator.flipDirection(6, 3000U, mirroredCut, now + 12010ms);
    check(indicator.position() == beforeFlip,
          "direction action leaves another sounding pad's cursor untouched");
    indicator.flipDirection(7, 3000U, mirroredCut, now + 12020ms);
    check(std::abs(indicator.position() - (8.0f + 2.0f / 3.0f - 1.0f / 3000.0f)) < 2.0e-6f &&
          indicator.pad() == 7 &&
          !indicator.update(now + 12030ms, 7, true, waveform, mirroredCut, mixer, 0.0f),
          "active one-third cursor mirrors near two-thirds using the last source frame without retrigger");
    indicator.flipDirection(7, 3000U, cut, now + 12040ms);
    check(std::abs(indicator.position() - beforeFlip) < 2.0e-6f,
          "two immediate direction flips restore the same active source position");
    indicator.stop(true, now + 12050ms);
    const float held = indicator.position();
    indicator.flipDirection(7, 3000U, mirroredCut, now + 12060ms);
    check(indicator.position() == held &&
          indicator.update(now + 12150ms, 7, true, waveform, cut, mixer, 0.0f) &&
          indicator.position() == 0.0f,
          "direction action cannot resurrect a stopped voice or reset its final hold deadline");
    indicator.acceptHostPosition(8.1f, now + 13s);
    indicator.flipDirection(7, 1000U, mirroredCut, now + 13010ms);
    check(indicator.position() <= 8.875f && indicator.position() >= 8.5f,
          "a nonlooping cursor outside an edited interval clamps into the mirrored cuts");
}

void mixerValueNavigation()
{
    using namespace midichopper::ui;
    check(isMixerValueCommitKey(0x0dU) && isMixerValueCommitKey(0xe06aU) &&
          isMixerValueCommitKey(0xe070U) && !isMixerValueCommitKey(0x09U),
          "normal Enter, declared keypad Enter and delivered Pugl keypad Enter all commit");
    const auto input = target(InteractiveType::levelValueLabel, 0);
    const auto output = target(InteractiveType::levelValueLabel, 1);
    check(mixerValueEntryTargetForControl(target(InteractiveType::levelFader, 0)) == input &&
          mixerValueEntryTargetForControl(target(InteractiveType::levelFader, 1)) == output,
          "both faders and their labels open the same value editor");
    for (int i = 0; i < 5; ++i) {
        check(mixerValueEntryTargetForControl(target(InteractiveType::mixerKnob, i)) ==
                  target(InteractiveType::mixerValueLabel, i) &&
              mixerValueEntryTargetForControl(target(InteractiveType::globalMixerKnob, i)) ==
                  target(InteractiveType::globalMixerValueLabel, i),
              "double clicking each continuous knob addresses its own value editor");
    }
    check(!mixerValueEntryTargetForControl(target(InteractiveType::mixerKnob, 5)).valid() &&
          !mixerValueEntryTargetForControl(target(InteractiveType::globalMixerKnob, 6)).valid(),
          "discrete slope and Dirty controls do not open numeric editors");
    for (const auto view : {MixerValueEntryView::main, MixerValueEntryView::sampleEditor,
                            MixerValueEntryView::cutPoints}) {
        std::array<sms::ui::InteractiveTarget, 7> expected{};
        std::size_t size = 0U;
        expected[size++] = input;
        if (view == MixerValueEntryView::main) {
            expected[size++] = target(InteractiveType::globalMixerValueLabel, 1);
            expected[size++] = target(InteractiveType::globalMixerValueLabel, 2);
            expected[size++] = target(InteractiveType::globalMixerValueLabel, 3);
            expected[size++] = target(InteractiveType::globalMixerValueLabel, 4);
        } else if (view == MixerValueEntryView::sampleEditor) {
            expected[size++] = target(InteractiveType::mixerValueLabel, 0);
            expected[size++] = target(InteractiveType::mixerValueLabel, 1);
            expected[size++] = target(InteractiveType::mixerValueLabel, 2);
            expected[size++] = target(InteractiveType::mixerValueLabel, 3);
            expected[size++] = target(InteractiveType::mixerValueLabel, 4);
        }
        expected[size++] = output;
        auto current = input;
        for (std::size_t i = 1; i <= size; ++i) {
            current = nextMixerValueEntryTarget(current, view, false);
            check(current == expected[i % size], "Tab traverses visible numeric controls in order and wraps");
        }
        current = input;
        for (std::size_t i = 1; i <= size; ++i) {
            current = nextMixerValueEntryTarget(current, view, true);
            check(current == expected[(size - i) % size], "Shift+Tab traverses the same order backwards");
        }
    }
    MixerValueEntry entry;
    entry.begin(input, -1, 0.0f);
    check(entry.type('9') && entry.type('9'), "out-of-range digits enter the value editor");
    check(!entry.value(1.0f, -24.0f, 12.0f) && entry.target() == input,
          "out-of-range text stays invalid and retains focus for Tab correction");
    entry.clear();
    check(entry.type('-'), "unfinished sign enters the value editor");
    check(!entry.value(1.0f, -24.0f, 12.0f) && entry.target() == input,
          "unfinished numeric text cannot advance focus");
    check(entry.type('3'), "digit completes the signed value");
    check(entry.value(1.0f, -24.0f, 12.0f) == -3.0f,
          "corrected text becomes eligible to commit and advance");
    entry.begin(input, -1, 10.0f);
    check(entry.value(0.01f, 0.1f, 0.2f) == 0.1f,
          "exact percentage endpoints survive display scaling without becoming invalid");
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
    logoGlow();
    padLayouts();
    wideCanvasGeometry();
    pluginIdentity();
    hamburgerMenuGeometry();
    contextMenuGeometry();
    skinMenuGeometryAndTargets();
    layoutAndBankSubmenuTargets();
    interactionTargets();
    spaceKeyTracking();
    padPressTracking();
    editorSnapshotCollection();
    chopEditorWorkflow();
    chopEditorAutomaticSave();
    splitEditorWorkflow();
    waveformVisibleSummary();
    sampleWaveformOrientation();
    globalReverseProjection();
    waveformDetailSession();
    editorAuditionModes();
    playbackIndicator();
    mixerValueEntry();
    mixerValueNavigation();
    wheelAdjustment();
    globalValueModes();
    mainSliderDragging();
    levelFaderTargets();
    levelMeterGeometry();
    waveformGeometry();
    chopEditorGeometry();
    chopEditorSession();
    std::cout << "UI geometry tests passed\n";
}
