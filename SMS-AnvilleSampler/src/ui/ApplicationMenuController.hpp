#pragma once

#include "Interaction.hpp"
#include "MidichopperLayout.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace midichopper::ui {

/** Catalog inputs; skin assets, names and preferences remain in the adapter. */
struct ApplicationMenuContext {
    bool skinsSelectable = false;
    int skinCount = 0;
    std::size_t selectedSkin = 0;
};

/** Framework-neutral effects for application menu choices. */
class ApplicationMenuHost {
public:
    virtual ~ApplicationMenuHost() = default;
    virtual void chooseLayout(int layout) = 0;
    virtual void chooseMidiBankMode(int mode) = 0;
    // A failed skin load keeps the menu open for another choice.
    virtual bool chooseSkin(std::size_t index) = 0;
    virtual void clearAllPads() = 0;
    virtual void setStatus(const char* message) = 0;
    virtual void repaintApplicationMenu() = 0;
    virtual void clearHover() noexcept = 0;
    virtual bool openLink(const char* url) = 0;
};

/** Owns the main menu, skin scrolling and CLEAR PADS confirmation.
 * Input routing, rendering and catalog I/O remain outside this controller.
 */
class ApplicationMenuController {
public:
    using Clock = std::chrono::steady_clock;
    ApplicationMenuController(ApplicationMenuHost& host, std::string_view helpRef);

    [[nodiscard]] bool open() const noexcept { return fOpen; }
    [[nodiscard]] layout::MenuSubmenu submenu() const noexcept { return fSubmenu; }
    [[nodiscard]] const sms::ui::ContextMenuGeometry& skinMenu() const noexcept
    { return fSkinMenu; }
    [[nodiscard]] int skinOffset() const noexcept { return fSkinOffset; }
    [[nodiscard]] bool resetArmed() const noexcept { return fResetArmed; }

    bool press(const ApplicationMenuContext&, sms::ui::InteractiveTarget,
               Clock::time_point now = Clock::now());
    bool rightPress();
    bool dismiss();
    void hover(const ApplicationMenuContext&, sms::ui::InteractiveTarget);
    bool scroll(const ApplicationMenuContext&, sms::ui::Point position, float delta);
    bool escape();
    void idle(Clock::time_point now = Clock::now());
    void cancelConfirmation() noexcept { fResetArmed = false; }
    void reset() noexcept;

private:
    void openSkinMenu(const ApplicationMenuContext&);
    void openLink(const char* url);

    ApplicationMenuHost& fHost;
    const std::string fHelpUrl;
    bool fOpen = false;
    layout::MenuSubmenu fSubmenu = layout::MenuSubmenu::none;
    sms::ui::ContextMenuGeometry fSkinMenu;
    int fSkinOffset = 0;
    bool fResetArmed = false;
    Clock::time_point fResetDeadline{};
};

} // namespace midichopper::ui
