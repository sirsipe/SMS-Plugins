#pragma once

#include "ContextMenu.hpp"
#include "MidichopperLayout.hpp"
#include "../plugin/GlobalValueMode.hpp"

namespace midichopper::ui {

enum class GlobalSettingsSubmenu { none, valueMode, range };

/** Geometry and selection for one global control's private settings menu. */
struct GlobalControlMenu {
    std::uint32_t parameter = plugin::kParameterCount;
    sms::ui::ContextMenuGeometry menu;
    sms::ui::ContextMenuGeometry submenu;
    GlobalSettingsSubmenu group = GlobalSettingsSubmenu::none;
    bool rangeResetArmed = false;

    [[nodiscard]] bool active() const noexcept { return parameter != plugin::kParameterCount; }

    void open(const std::uint32_t control, const sms::ui::Point anchor) noexcept
    {
        parameter = control;
        menu = {anchor, 3, layout::contentBounds, 130.0f};
        group = GlobalSettingsSubmenu::none;
        rangeResetArmed = false;
    }

    [[nodiscard]] bool rangeResetEnabled(const plugin::GlobalValueSettings& settings) const noexcept
    {
        const auto original = plugin::parameterRange(parameter);
        return active() && (settings.minimum != original.minimum || settings.maximum != original.maximum);
    }

    [[nodiscard]] plugin::GlobalValueSettings originalRangeSettings(
        plugin::GlobalValueSettings settings, const int bound = -1) const noexcept
    {
        const auto original = plugin::parameterRange(parameter);
        if (bound == -1 || bound == 0) settings.minimum = original.minimum;
        if (bound == -1 || bound == 1) settings.maximum = original.maximum;
        return settings;
    }

    void select(const GlobalSettingsSubmenu selected) noexcept
    {
        if (group != selected)
            rangeResetArmed = false;
        group = selected;
        if (group == GlobalSettingsSubmenu::valueMode) {
            submenu = sms::ui::ContextMenuGeometry::submenu(menu, 1, 3,
                layout::contentBounds, 120.0f);
        } else if (group == GlobalSettingsSubmenu::range) {
            constexpr std::array kinds{
                sms::ui::ContextMenuItemKind::action, sms::ui::ContextMenuItemKind::action,
                sms::ui::ContextMenuItemKind::separator, sms::ui::ContextMenuItemKind::action};
            const auto placement = sms::ui::ContextMenuGeometry::submenu(menu, 2, 4,
                layout::contentBounds, 190.0f).bounds();
            submenu = sms::ui::ContextMenuGeometry(
                {placement.x, menu.item(2).y - 5.0f},
                std::span<const sms::ui::ContextMenuItemKind>{kinds}, layout::contentBounds, 190.0f);
        }
    }

    void close() noexcept
    {
        parameter = plugin::kParameterCount;
        group = GlobalSettingsSubmenu::none;
        rangeResetArmed = false;
    }

};

} // namespace midichopper::ui
