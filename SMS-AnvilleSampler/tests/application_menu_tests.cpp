#include "ApplicationMenuController.hpp"
#include "HelpLinks.hpp"
#include "MidichopperInteraction.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace midichopper::ui;
using namespace std::chrono_literals;

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Host final : ApplicationMenuHost {
    int layout = -1;
    int bankMode = -1;
    int clears = 0;
    int repaints = 0;
    int hoverClears = 0;
    bool skinLoads = true;
    bool browserAvailable = true;
    std::string status;
    std::vector<std::size_t> skins;
    std::vector<std::string> links;

    void chooseLayout(const int value) override { layout = value; }
    void chooseMidiBankMode(const int value) override { bankMode = value; }
    bool chooseSkin(const std::size_t index) override
    { skins.push_back(index); return skinLoads; }
    void clearAllPads() override { ++clears; }
    void setStatus(const char* const message) override { status = message; }
    void repaintApplicationMenu() override { ++repaints; }
    void clearHover() noexcept override { ++hoverClears; }
    bool openLink(const char* const url) override
    { links.emplace_back(url); return browserAvailable; }
};

struct Fixture {
    Host host;
    ApplicationMenuController menu{host, "v1.2.3"};
    ApplicationMenuContext context{true, 25, 21};
    ApplicationMenuController::Clock::time_point now{};

    bool press(const InteractiveType type, const int index = -1)
    { return menu.press(context, target(type, index), now); }
    void open()
    { check(press(InteractiveType::menuButton) && menu.open(), "menu opens"); }
    void hover(const InteractiveType type, const int index = -1)
    { menu.hover(context, target(type, index)); }
    sms::ui::Point skinPosition() const
    {
        const auto bounds = menu.skinMenu().bounds();
        return {bounds.x + 1.0f, bounds.y + 1.0f};
    }
};

void menuChoicesAndDismissal()
{
    Fixture f;
    check(!f.menu.open() && !f.menu.resetArmed() &&
          f.menu.submenu() == layout::MenuSubmenu::none, "initial state is closed");
    check(!f.press(InteractiveType::menuLayout, 2) && !f.menu.escape() &&
          !f.menu.rightPress(), "closed menu leaves unrelated input to the adapter");
    f.hover(InteractiveType::menuPadLayouts);
    check(f.menu.submenu() == layout::MenuSubmenu::none && f.host.repaints == 0,
          "hover does not open a closed menu");

    f.open();
    f.press(InteractiveType::menuPadLayouts);
    check(f.menu.open() && f.menu.submenu() == layout::MenuSubmenu::padLayout,
          "layout button opens its submenu");
    f.press(InteractiveType::menuLayout, 2);
    check(!f.menu.open() && f.menu.submenu() == layout::MenuSubmenu::none &&
          f.host.layout == 2, "layout selection closes the menu and forwards the index");

    f.open();
    f.press(InteractiveType::menuBankModes);
    check(f.menu.submenu() == layout::MenuSubmenu::bankMode,
          "bank mode button opens its submenu");
    f.press(InteractiveType::menuMidiBankMode, 1);
    check(!f.menu.open() && f.host.bankMode == 1,
          "bank mode selection closes the menu and forwards the index");

    f.open();
    f.hover(InteractiveType::menuPadLayouts);
    const int repaint = f.host.repaints;
    f.hover(InteractiveType::menuPadLayouts);
    check(f.host.repaints == repaint, "unchanged submenu hover needs no repaint");
    f.menu.hover(f.context, {});
    check(f.menu.submenu() == layout::MenuSubmenu::padLayout,
          "moving outside the menu preserves its submenu");
    f.hover(InteractiveType::menuBankModes);
    check(f.menu.submenu() == layout::MenuSubmenu::bankMode,
          "hover switches submenus");
    f.hover(InteractiveType::menuLink, 0);
    check(f.menu.submenu() == layout::MenuSubmenu::none,
          "hover over a main action hides submenus");
    check(f.menu.press(f.context, {}, f.now) && !f.menu.open(),
          "outside press is consumed without clicking through");

    f.open();
    f.hover(InteractiveType::menuPadLayouts);
    check(f.menu.escape() && f.menu.open() &&
          f.menu.submenu() == layout::MenuSubmenu::none,
          "first Escape closes the submenu");
    check(f.menu.escape() && !f.menu.open(), "second Escape closes the main menu");
    f.open();
    f.hover(InteractiveType::menuBankModes);
    check(f.menu.rightPress() && !f.menu.open() &&
          f.menu.submenu() == layout::MenuSubmenu::none,
          "right press dismisses the complete menu");
    f.open();
    check(f.press(InteractiveType::menuButton) && !f.menu.open(),
          "menu button toggles the menu closed");
}

void clearConfirmation()
{
    Fixture f;
    f.open();
    f.press(InteractiveType::menuReset);
    check(f.menu.resetArmed() && f.menu.open() && f.host.clears == 0 &&
          f.host.status == "Click CLEAR PADS again to confirm",
          "first CLEAR PADS click requests confirmation");
    f.menu.idle(f.now + 1999ms);
    check(f.menu.resetArmed(), "confirmation remains armed before its deadline");
    f.now += 1999ms;
    f.press(InteractiveType::menuReset);
    check(f.host.clears == 1 && !f.menu.open() && !f.menu.resetArmed() &&
          f.host.hoverClears > 0, "second click before two seconds clears and closes");

    f.open();
    f.press(InteractiveType::menuReset);
    f.now += 2s;
    f.press(InteractiveType::menuReset);
    check(f.host.clears == 1 && f.menu.resetArmed(),
          "a click at the deadline starts a new confirmation");
    f.menu.idle(f.now + 2s);
    check(!f.menu.resetArmed() && f.menu.open(),
          "idle expires confirmation at two seconds without closing the menu");

    for (const auto action : {InteractiveType::menuPadLayouts,
                              InteractiveType::menuBankModes,
                              InteractiveType::menuSkins}) {
        f.press(InteractiveType::menuReset);
        f.press(action);
        check(!f.menu.resetArmed(), "another menu action cancels clear confirmation");
    }
    f.press(InteractiveType::menuReset);
    f.hover(InteractiveType::menuPadLayouts);
    check(f.menu.resetArmed(), "hover alone preserves the existing confirmation");
    f.menu.escape();
    check(!f.menu.resetArmed() && f.menu.open(),
          "Escape clears confirmation while closing a submenu");
    f.press(InteractiveType::menuReset);
    check(f.menu.dismiss() && !f.menu.open(), "dismiss closes an armed menu");
    f.menu.idle(f.now);
    check(!f.menu.resetArmed(), "idle cancels confirmation on a dismissed menu");
    f.open();
    f.press(InteractiveType::menuReset);
    f.press(InteractiveType::menuButton);
    check(!f.menu.resetArmed(), "menu toggle cancels confirmation immediately");
    f.open();
    f.press(InteractiveType::menuReset);
    f.menu.cancelConfirmation();
    check(f.menu.open() && !f.menu.resetArmed(),
          "adapter may cancel confirmation while retaining the menu");
    f.press(InteractiveType::menuReset);
    f.menu.reset();
    check(!f.menu.open() && !f.menu.resetArmed() &&
          f.menu.submenu() == layout::MenuSubmenu::none,
          "reset discards application menu state without performing a pad clear");
    check(f.host.clears == 1, "cancel and expiry never dispatch a clear request");
}

void skinChoicesAndScrolling()
{
    Fixture f;
    check(!f.menu.scroll(f.context, {820.0f, 160.0f}, -1.0f),
          "closed menu does not consume skin scrolling");
    f.open();
    f.hover(InteractiveType::menuSkins);
    check(f.menu.submenu() == layout::MenuSubmenu::skin && f.menu.skinOffset() == 7,
          "skin submenu scrolls selected optional skin into the bounded window");
    const auto position = f.skinPosition();
    const auto defaultRow = f.menu.skinMenu().item(0);
    check(f.menu.skinMenu().hit({defaultRow.x + 1.0f, defaultRow.y + 1.0f}) == 0,
          "skin geometry exposes the pinned Default row");
    check(!f.menu.scroll(f.context, {0.0f, 0.0f}, -1.0f) &&
          !f.menu.scroll(f.context, position, 0.0f) &&
          !f.menu.scroll(f.context, position, std::numeric_limits<float>::infinity()) &&
          !f.menu.scroll(f.context, position, std::numeric_limits<float>::quiet_NaN()),
          "outside, zero and nonfinite wheel events are ignored");
    const int hoverClears = f.host.hoverClears;
    check(f.menu.scroll(f.context, position, -100.0f) && f.menu.skinOffset() == 7,
          "scrolling at the bottom is consumed and clamped");
    check(f.menu.scroll(f.context, position, 100.0f) && f.menu.skinOffset() == 6 &&
          f.host.hoverClears == hoverClears + 2,
          "one wheel event moves one skin and clears stale hover");
    f.press(InteractiveType::skinItem, 0);
    check(f.host.skins.back() == 0U && !f.menu.open(),
          "Default remains pinned while optional choices scroll");

    f.open();
    f.press(InteractiveType::menuSkins);
    f.host.skinLoads = false;
    f.press(InteractiveType::skinItem, 1);
    check(f.host.skins.back() == 8U && f.menu.open() &&
          f.menu.submenu() == layout::MenuSubmenu::skin,
          "failed skin loading preserves the menu and uses the scrolling row mapping");
    const auto choices = f.host.skins.size();
    f.press(InteractiveType::skinItem, -1);
    f.press(InteractiveType::skinItem, 100);
    check(f.host.skins.size() == choices, "invalid skin rows never reach the host");
    f.host.skinLoads = true;
    f.press(InteractiveType::skinItem, 17);
    check(f.host.skins.back() == 24U && !f.menu.open(),
          "last visible scrolling choice selects its catalog entry and closes");

    f.context.selectedSkin = 0;
    f.open();
    f.press(InteractiveType::menuSkins);
    for (int i = 0; i < 10; ++i)
        f.menu.scroll(f.context, f.skinPosition(), 1.0f);
    check(f.menu.skinOffset() == 0, "skin offset clamps at the first optional choice");
    f.menu.reset();
    f.context.skinCount = 4;
    f.context.selectedSkin = std::numeric_limits<std::size_t>::max();
    f.open();
    f.press(InteractiveType::menuSkins);
    check(f.menu.skinOffset() == 0 && f.menu.scroll(f.context, f.skinPosition(), -1.0f) &&
          f.menu.skinOffset() == 0, "short skin lists consume scrolling without moving");
    f.menu.reset();
    f.context.skinsSelectable = false;
    f.open();
    f.press(InteractiveType::menuSkins);
    f.press(InteractiveType::skinItem, 0);
    check(f.menu.submenu() == layout::MenuSubmenu::none &&
          f.host.skins.size() == choices + 1, "forced skins cannot open or select a skin");
}

void links()
{
    Fixture f;
    f.open();
    f.press(InteractiveType::menuLink, 0);
    check(!f.menu.open() && f.host.links.back() == onlineHelpUrl("v1.2.3"),
          "online help uses the configured build reference");
    f.open();
    f.press(InteractiveType::menuLink, 1);
    check(!f.menu.open() && f.host.links.back() == kIssueUrl,
          "issue menu opens the issue tracker");
    f.open();
    f.press(InteractiveType::menuSkins);
    f.host.browserAvailable = false;
    f.press(InteractiveType::skinGetMore);
    check(!f.menu.open() && f.menu.submenu() == layout::MenuSubmenu::none &&
          f.host.links.back() == kMoreSkinsUrl && f.host.skins.empty() &&
          f.host.status == "Could not open the browser",
          "Get More closes the menu, reports launch failure and leaves skin selection alone");
}
} // namespace

int main()
{
    menuChoicesAndDismissal();
    clearConfirmation();
    skinChoicesAndScrolling();
    links();
    std::cout << "Application menu tests passed\n";
}
