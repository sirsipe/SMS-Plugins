#include "ViewRenderers.hpp"
#include "DPF/Controls.hpp"
#include "DPF/ContextMenu.hpp"
#include "Parameters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace midichopper::ui::rendering {

namespace uiLayout = layout;

namespace {

void drawGlobalControlMenu(const ViewRenderContext& ctx)
{
    const sms::ui::dpf::ScopedCanvasState canvasState(ctx.canvas);
    const auto& colors = sms::ui::dpf::theme();
    const auto& menu = ctx.state.globalControlMenu;
    const auto index = plugin::globalControlIndex(menu.parameter);
    if (index >= ctx.state.globalValueSettings.size())
        return;
    const auto& settings = ctx.state.globalValueSettings[index];
    const sms::ui::ContextMenuItemView parentItems[] = {
        {" ", false},
        {"Value Mode", true, false, sms::ui::ContextMenuItemKind::action,
         menu.group == GlobalSettingsSubmenu::valueMode, -1, true},
        {"Range", true, false, sms::ui::ContextMenuItemKind::action,
         menu.group == GlobalSettingsSubmenu::range, -1, true},
    };
    const int parentHover =
        hovered(ctx, InteractiveType::globalSettingsGroup) ? ctx.state.hoveredTarget.index : -1;
    sms::ui::dpf::drawContextMenu(ctx.canvas, menu.menu, parentItems, parentHover);

    const auto header = menu.menu.item(0);
    constexpr const char* names[] = {"OUT", "PAN", "TUNE", "LOWPASS", "HIGHPASS"};
    char title[48];
    std::snprintf(title, sizeof(title), "%s %s", sms::ui::dpf::themeText("Settings:"),
                  names[index]);
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(10.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
    ctx.canvas.fillColor(colors.contentSecondary);
    ctx.canvas.text(header.x + 11.0f, header.y + header.height * 0.5f, title, nullptr);

    if (menu.group == GlobalSettingsSubmenu::valueMode) {
        auto quantized = settings;
        quantized.mode = plugin::GlobalValueMode::quantized;
        auto zeroSnap = settings;
        zeroSnap.mode = plugin::GlobalValueMode::zeroSnap;
        const sms::ui::ContextMenuItemView modeItems[] = {
            {"Normal", true, false, sms::ui::ContextMenuItemKind::action,
             settings.mode == plugin::GlobalValueMode::normal},
            {"Quantized", plugin::validGlobalValueSettings(menu.parameter, quantized), false,
             sms::ui::ContextMenuItemKind::action,
             settings.mode == plugin::GlobalValueMode::quantized},
            {"Zero Snap", plugin::validGlobalValueSettings(menu.parameter, zeroSnap), false,
             sms::ui::ContextMenuItemKind::action,
             settings.mode == plugin::GlobalValueMode::zeroSnap},
        };
        sms::ui::dpf::drawContextMenu(
            ctx.canvas, menu.submenu, modeItems,
            hovered(ctx, InteractiveType::valueModeItem) ? ctx.state.hoveredTarget.index : -1);
    } else if (menu.group == GlobalSettingsSubmenu::range) {
        const bool percentage = index == 1U || index == 3U || index == 4U;
        const char* unit = percentage ? "%" : index == 0U ? " dB" : " st";
        const auto originalRange = plugin::parameterRange(menu.parameter);
        const bool bipolar = originalRange.minimum < 0.0f;
        const sms::ui::ContextMenuItemView rangeItems[] = {
            {" ", true},
            {" ", true},
            {"", false, false, sms::ui::ContextMenuItemKind::separator},
            {menu.rangeResetArmed ? "Confirm: RESET" : "RESET", menu.rangeResetEnabled(settings),
             true},
        };
        const int rangeHover = hovered(ctx, InteractiveType::globalRangeEntry)
                                   ? ctx.state.hoveredTarget.index
                               : hovered(ctx, InteractiveType::globalRangeReset) ? 3
                                                                                 : -1;
        sms::ui::dpf::drawContextMenu(ctx.canvas, menu.submenu, rangeItems, rangeHover);
        ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
        ctx.canvas.fontSize(11.0f);
        for (int row = 0; row < 2; ++row) {
            const auto bounds = menu.submenu.item(row);
            const float centerY = bounds.y + bounds.height * 0.5f;
            const float scale = percentage ? 100.0f : 1.0f;
            const float originalValue =
                (row == 0 ? originalRange.minimum : originalRange.maximum) * scale;
            char original[48];
            std::snprintf(original, sizeof(original),
                          bipolar && originalValue > 0.0f ? "(+%.6g%s)" : "(%.6g%s)", originalValue,
                          unit);
            ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_RIGHT |
                                 DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            const float originalX = bounds.x + bounds.width - 11.0f;
            DGL_NAMESPACE::Rectangle<float> originalBounds;
            ctx.canvas.textBounds(originalX, centerY, original, nullptr, originalBounds);
            const float currentRight = originalX - originalBounds.getWidth() - 8.0f;
            ctx.canvas.fillColor(colors.contentSecondary);
            ctx.canvas.text(originalX, centerY, original, nullptr);

            const bool editing =
                isTarget(ctx.state.mixerValueEntryTarget, InteractiveType::globalRangeEntry, row);
            char current[96];
            if (editing) {
                ctx.canvas.beginPath();
                ctx.canvas.roundedRect(bounds.x + 2.0f, bounds.y + 2.0f,
                                       std::max(0.0f, currentRight - bounds.x),
                                       bounds.height - 4.0f, 3.0f);
                ctx.canvas.fillColor(colors.recess);
                ctx.canvas.fill();
                ctx.canvas.strokeColor(colors.selection);
                ctx.canvas.strokeWidth(1.0f);
                ctx.canvas.stroke();
                std::snprintf(current, sizeof(current), "%s: %s_%s",
                              sms::ui::dpf::themeText(row == 0 ? "Min" : "Max"),
                              ctx.state.mixerValueEntryText, unit);
            } else {
                const float value = (row == 0 ? settings.minimum : settings.maximum) * scale;
                std::snprintf(current, sizeof(current), "%s: %.6g%s",
                              sms::ui::dpf::themeText(row == 0 ? "Min" : "Max"), value, unit);
            }
            ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                                 DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
            ctx.canvas.fillColor(editing             ? colors.selection
                                 : rangeHover == row ? colors.controlAccent
                                                     : colors.contentPrimary);
            ctx.canvas.save();
            ctx.canvas.intersectScissor(bounds.x + 11.0f, bounds.y,
                                        std::max(0.0f, currentRight - bounds.x - 11.0f),
                                        bounds.height);
            ctx.canvas.text(bounds.x + 11.0f, centerY, current, nullptr);
            ctx.canvas.restore();
        }
    }
}

void drawMenuOverlay(const ViewRenderContext& ctx)
{
    const auto& colors = sms::ui::dpf::theme();
    sms::ui::dpf::drawPanel(ctx.canvas, uiLayout::menuPanelBounds(ctx.state.skinsAvailable));
    ctx.canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
    ctx.canvas.fontSize(10.0f);
    ctx.canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT | DGL_NAMESPACE::NanoVG::ALIGN_TOP);
    ctx.canvas.fillColor(colors.contentSecondary);
    const auto submenuButton = [&](const sms::ui::Rect bounds, const char* label,
                                   const uiLayout::MenuSubmenu submenu,
                                   const InteractiveType type) {
        sms::ui::dpf::drawSegment(ctx.canvas, bounds, label, ctx.state.menuSubmenu == submenu,
                                  colors.selection, hovered(ctx, type));
        const float x = bounds.x + 13.0f;
        const float y = bounds.y + bounds.height * 0.5f;
        ctx.canvas.beginPath();
        ctx.canvas.moveTo(x + 2.0f, y - 4.0f);
        ctx.canvas.lineTo(x - 2.0f, y);
        ctx.canvas.lineTo(x + 2.0f, y + 4.0f);
        ctx.canvas.strokeColor(colors.contentSecondary);
        ctx.canvas.strokeWidth(1.3f);
        ctx.canvas.stroke();
    };
    submenuButton(uiLayout::layoutMenuButton, "PAD LAYOUT", uiLayout::MenuSubmenu::padLayout,
                  InteractiveType::menuPadLayouts);
    submenuButton(uiLayout::bankModeMenuButton, "BANK MODE", uiLayout::MenuSubmenu::bankMode,
                  InteractiveType::menuBankModes);
    if (ctx.state.skinsAvailable)
        submenuButton(uiLayout::skinMenuButton, "SKIN", uiLayout::MenuSubmenu::skin,
                      InteractiveType::menuSkins);
    sms::ui::dpf::drawSegment(ctx.canvas, uiLayout::resetMenuButton(ctx.state.skinsAvailable),
                              ctx.state.resetArmed ? "Confirm: CLEAR PADS" : "CLEAR PADS",
                              ctx.state.resetArmed, colors.intentDanger,
                              hovered(ctx, InteractiveType::menuReset), ctx.state.resetEnabled);
    text(ctx, 1060.0f, ctx.state.skinsAvailable ? 235.0f : 199.0f, "LINKS", nullptr);
    static constexpr const char* linkLabels[] = {"ONLINE HELP", "REPORT ISSUE"};
    for (int index = 0; index < 2; ++index)
        sms::ui::dpf::drawSegment(ctx.canvas,
                                  uiLayout::menuLinkOption(index, ctx.state.skinsAvailable),
                                  linkLabels[index], false, colors.selection,
                                  hovered(ctx, InteractiveType::menuLink, index));
    if (ctx.state.menuSubmenu == uiLayout::MenuSubmenu::padLayout) {
        const sms::ui::ContextMenuItemView items[] = {
            {"16 PADS  ·  4×4", ctx.state.currentPad < 0, false,
             sms::ui::ContextMenuItemKind::action, ctx.state.layout == 0},
            {"12 PADS  ·  3×4", ctx.state.currentPad < 0, false,
             sms::ui::ContextMenuItemKind::action, ctx.state.layout == 1},
            {"8 PADS   ·  4×2", ctx.state.currentPad < 0, false,
             sms::ui::ContextMenuItemKind::action, ctx.state.layout == 2},
        };
        sms::ui::dpf::drawContextMenu(
            ctx.canvas, uiLayout::layoutMenuGeometry(), items,
            hovered(ctx, InteractiveType::menuLayout) ? ctx.state.hoveredTarget.index : -1);
    } else if (ctx.state.menuSubmenu == uiLayout::MenuSubmenu::bankMode) {
        const sms::ui::ContextMenuItemView items[] = {
            {"SELECTED  ·  SHARED", true, false, sms::ui::ContextMenuItemKind::action,
             ctx.state.midiBankMode == 0},
            {"ALL BANKS  ·  UNIQUE", true, false, sms::ui::ContextMenuItemKind::action,
             ctx.state.midiBankMode == 1},
        };
        sms::ui::dpf::drawContextMenu(
            ctx.canvas, uiLayout::bankModeMenuGeometry(), items,
            hovered(ctx, InteractiveType::menuMidiBankMode) ? ctx.state.hoveredTarget.index : -1);
    } else if (ctx.state.menuSubmenu == uiLayout::MenuSubmenu::skin) {
        sms::ui::dpf::drawContextMenu(
            ctx.canvas, ctx.state.skinMenu, ctx.state.skinMenuItems,
            hovered(ctx, InteractiveType::skinGetMore)
                ? uiLayout::skinMenuVisibleCount(ctx.state.skinMenuTotal) + 1
                : (hovered(ctx, InteractiveType::skinItem) ? ctx.state.hoveredTarget.index : -1));
        const int maximum = uiLayout::skinMenuMaxOffset(ctx.state.skinMenuTotal);
        if (maximum > 0) {
            const auto bounds = ctx.state.skinMenu.bounds();
            const float top = ctx.state.skinMenu.item(1).y;
            const int choices = uiLayout::skinMenuVisibleCount(ctx.state.skinMenuTotal);
            const auto last = ctx.state.skinMenu.item(choices - 1);
            const float height = last.y + last.height - top;
            const float thumb =
                std::max(10.0f, height * static_cast<float>(choices - 1) /
                                    static_cast<float>(ctx.state.skinMenuTotal - 1));
            ctx.canvas.beginPath();
            ctx.canvas.roundedRect(bounds.x + bounds.width - 5.0f, top, 2.0f, height, 1.0f);
            ctx.canvas.fillColor(colors.outline);
            ctx.canvas.fill();
            ctx.canvas.beginPath();
            ctx.canvas.roundedRect(bounds.x + bounds.width - 5.0f,
                                   top + (height - thumb) *
                                             static_cast<float>(ctx.state.skinMenuOffset) /
                                             static_cast<float>(maximum),
                                   2.0f, thumb, 1.0f);
            ctx.canvas.fillColor(colors.selection);
            ctx.canvas.fill();
        }
    }
}

} // namespace

void drawMenus(const ViewRenderContext& ctx)
{
    if (ctx.state.menuOpen)
        drawMenuOverlay(ctx);
    if (ctx.state.padContextMenuOpen) {
        sms::ui::dpf::drawContextMenu(
            ctx.canvas, ctx.state.padContextMenu, ctx.state.padContextMenuItems,
            hovered(ctx, InteractiveType::padContextItem) ? ctx.state.hoveredTarget.index : -1);
        if (ctx.state.padColorMenuOpen)
            sms::ui::dpf::drawContextMenu(
                ctx.canvas, ctx.state.padColorMenu, ctx.state.padColorMenuItems,
                hovered(ctx, InteractiveType::padColorItem) ? ctx.state.hoveredTarget.index : -1);
    }
    if (ctx.state.globalControlMenu.active())
        drawGlobalControlMenu(ctx);
}

} // namespace midichopper::ui::rendering
