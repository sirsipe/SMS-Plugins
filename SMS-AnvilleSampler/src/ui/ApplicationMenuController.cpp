#include "ApplicationMenuController.hpp"

#include "HelpLinks.hpp"
#include "MidichopperInteraction.hpp"

#include <algorithm>
#include <cmath>

namespace midichopper::ui {
namespace {
constexpr auto kConfirmationTimeout = std::chrono::seconds(2);
}

ApplicationMenuController::ApplicationMenuController(
    ApplicationMenuHost& host, const std::string_view helpRef)
    : fHost(host), fHelpUrl(onlineHelpUrl(helpRef))
{
}

bool ApplicationMenuController::press(const ApplicationMenuContext& context,
    const sms::ui::InteractiveTarget clicked, const Clock::time_point now)
{
    if (isTarget(clicked, InteractiveType::menuButton)) {
        fOpen = !fOpen;
        fResetArmed = false;
        fSubmenu = layout::MenuSubmenu::none;
        fHost.repaintApplicationMenu();
        return true;
    }
    if (!fOpen)
        return false;

    if (isTarget(clicked, InteractiveType::menuReset)) {
        fSubmenu = layout::MenuSubmenu::none;
        if (fResetArmed && now < fResetDeadline) {
            reset();
            fHost.clearHover();
            fHost.clearAllPads();
        } else {
            fResetArmed = true;
            fResetDeadline = now + kConfirmationTimeout;
            fHost.setStatus("Click CLEAR PADS again to confirm");
        }
        fHost.repaintApplicationMenu();
        return true;
    }

    fResetArmed = false;
    if (isTarget(clicked, InteractiveType::menuPadLayouts) ||
        isTarget(clicked, InteractiveType::menuBankModes)) {
        fSubmenu = isTarget(clicked, InteractiveType::menuPadLayouts)
            ? layout::MenuSubmenu::padLayout : layout::MenuSubmenu::bankMode;
        fHost.repaintApplicationMenu();
        return true;
    }
    if (isTarget(clicked, InteractiveType::menuSkins)) {
        openSkinMenu(context);
        fHost.repaintApplicationMenu();
        return true;
    }
    if (isTarget(clicked, InteractiveType::skinItem)) {
        const auto index = static_cast<std::size_t>(
            layout::skinMenuEntry(clicked.index, fSkinOffset));
        if (context.skinsSelectable && index < static_cast<std::size_t>(
                std::max(context.skinCount, 0)) && fHost.chooseSkin(index)) {
            fSubmenu = layout::MenuSubmenu::none;
            fOpen = false;
            fHost.clearHover();
            fHost.repaintApplicationMenu();
        }
        return true;
    }

    fSubmenu = layout::MenuSubmenu::none;
    if (isTarget(clicked, InteractiveType::skinGetMore)) {
        openLink(kMoreSkinsUrl.data());
        return true;
    }
    if (isTarget(clicked, InteractiveType::menuLayout)) {
        fOpen = false;
        fHost.chooseLayout(clicked.index);
        return true;
    }
    if (isTarget(clicked, InteractiveType::menuMidiBankMode)) {
        fOpen = false;
        fHost.chooseMidiBankMode(clicked.index);
        return true;
    }
    if (isTarget(clicked, InteractiveType::menuLink)) {
        openLink(clicked.index == 0 ? fHelpUrl.c_str() : kIssueUrl.data());
        return true;
    }

    fOpen = false;
    fHost.repaintApplicationMenu();
    return true;
}

bool ApplicationMenuController::rightPress()
{
    return dismiss();
}

bool ApplicationMenuController::dismiss()
{
    if (!fOpen)
        return false;
    fOpen = false;
    fSubmenu = layout::MenuSubmenu::none;
    fHost.clearHover();
    fHost.repaintApplicationMenu();
    return true;
}

void ApplicationMenuController::hover(const ApplicationMenuContext& context,
    const sms::ui::InteractiveTarget hovered)
{
    if (!fOpen)
        return;
    auto submenu = fSubmenu;
    if (isTarget(hovered, InteractiveType::menuPadLayouts))
        submenu = layout::MenuSubmenu::padLayout;
    else if (isTarget(hovered, InteractiveType::menuBankModes))
        submenu = layout::MenuSubmenu::bankMode;
    else if (isTarget(hovered, InteractiveType::menuSkins))
        submenu = layout::MenuSubmenu::skin;
    else if (isTarget(hovered, InteractiveType::menuLink) ||
             isTarget(hovered, InteractiveType::menuReset))
        submenu = layout::MenuSubmenu::none;
    if (submenu == fSubmenu)
        return;
    if (submenu == layout::MenuSubmenu::skin)
        openSkinMenu(context);
    else
        fSubmenu = submenu;
    fHost.repaintApplicationMenu();
}

bool ApplicationMenuController::scroll(const ApplicationMenuContext& context,
    const sms::ui::Point position, const float delta)
{
    if (delta == 0.0f || !std::isfinite(delta) || !fOpen ||
        fSubmenu != layout::MenuSubmenu::skin || !fSkinMenu.bounds().contains(position))
        return false;
    fSkinOffset = std::clamp(fSkinOffset + (delta < 0.0f ? 1 : -1),
                           0, layout::skinMenuMaxOffset(context.skinCount));
    fHost.clearHover();
    fHost.repaintApplicationMenu();
    return true;
}

bool ApplicationMenuController::escape()
{
    if (!fOpen)
        return false;
    fResetArmed = false;
    if (fSubmenu != layout::MenuSubmenu::none)
        fSubmenu = layout::MenuSubmenu::none;
    else
        fOpen = false;
    fHost.clearHover();
    fHost.repaintApplicationMenu();
    return true;
}

void ApplicationMenuController::idle(const Clock::time_point now)
{
    if (fResetArmed && (!fOpen || now >= fResetDeadline)) {
        fResetArmed = false;
        fHost.repaintApplicationMenu();
    }
}

void ApplicationMenuController::reset() noexcept
{
    fOpen = false;
    fSubmenu = layout::MenuSubmenu::none;
    fResetArmed = false;
}

void ApplicationMenuController::openSkinMenu(const ApplicationMenuContext& context)
{
    if (!context.skinsSelectable)
        return;
    // Preserve Default at row zero while bringing the selected optional skin into view.
    fSkinOffset = static_cast<int>(std::min(
        context.selectedSkin == 0 ? 0U : context.selectedSkin - 1U,
        static_cast<std::size_t>(layout::skinMenuMaxOffset(context.skinCount))));
    fSkinMenu = layout::skinMenuGeometry(context.skinCount);
    fSubmenu = layout::MenuSubmenu::skin;
    fHost.repaintApplicationMenu();
}

void ApplicationMenuController::openLink(const char* const url)
{
    fOpen = false;
    if (!fHost.openLink(url))
        fHost.setStatus("Could not open the browser");
    fHost.repaintApplicationMenu();
}

} // namespace midichopper::ui
