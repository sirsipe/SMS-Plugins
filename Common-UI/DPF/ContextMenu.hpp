#pragma once

#include "../ContextMenu.hpp"
#include "Controls.hpp"
#include "Theme.hpp"

#include <span>

namespace sms::ui::dpf {

inline void drawContextMenu(DGL_NAMESPACE::NanoVG& canvas,
                            const ContextMenuGeometry& geometry,
                            const std::span<const ContextMenuItemView> items,
                            const int hoveredItem)
{
    const ScopedCanvasState canvasState(canvas);
    const Theme& colors = theme();
    drawPanel(canvas, geometry.bounds());

    for (std::size_t index = 0; index < items.size(); ++index) {
        const ContextMenuItemView& item = items[index];
        const Rect bounds = geometry.item(static_cast<int>(index));
        if (item.kind == ContextMenuItemKind::separator) {
            canvas.beginPath();
            canvas.moveTo(bounds.x + 7.0f, bounds.y + bounds.height * 0.5f);
            canvas.lineTo(bounds.x + bounds.width - 7.0f,
                          bounds.y + bounds.height * 0.5f);
            canvas.strokeColor(colors.outline.withAlpha(0.7f));
            canvas.strokeWidth(colors.outlineWidth);
            canvas.stroke();
            continue;
        }
        const bool hovered = item.enabled && hoveredItem == static_cast<int>(index);
        const auto& accent = item.dangerous ? colors.intentDanger : colors.controlAccent;

        if (hovered) {
            canvas.beginPath();
            canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height,
                               colors.controlRadius);
            canvas.fillColor(accent.withAlpha(colors.hoverMenuFillAlpha));
            canvas.fill();
        }

        if (item.selected) {
            canvas.beginPath();
            canvas.roundedRect(bounds.x + 3.0f, bounds.y + 5.0f,
                               3.0f, bounds.height - 10.0f, 1.5f);
            canvas.fillColor(colors.selection);
            canvas.fill();
        }
        if (item.paletteIndex >= 0 &&
            item.paletteIndex < static_cast<int>(colors.padColors.size())) {
            canvas.beginPath();
            canvas.circle(bounds.x + 17.0f, bounds.y + bounds.height * 0.5f, 5.5f);
            canvas.fillColor(colors.padColors[static_cast<std::size_t>(item.paletteIndex)].tint);
            canvas.fill();
        }
        if (item.hasSubmenu) {
            const float x = bounds.x + bounds.width - 13.0f;
            const float y = bounds.y + bounds.height * 0.5f;
            canvas.beginPath();
            canvas.moveTo(x - 2.0f, y - 4.0f);
            canvas.lineTo(x + 2.0f, y);
            canvas.lineTo(x - 2.0f, y + 4.0f);
            canvas.strokeColor(item.enabled ? colors.contentPrimary
                                            : colors.contentSecondary.withAlpha(0.45f));
            canvas.strokeWidth(1.3f);
            canvas.stroke();
        }

        canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas.fontSize(11.0f);
        canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                         DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas.fillColor(item.enabled
            ? (hovered ? accent : colors.contentPrimary)
            : colors.contentSecondary.withAlpha(0.45f));
        canvas.text(bounds.x + (item.paletteIndex >= 0 ? 29.0f : 11.0f),
                    bounds.y + bounds.height * 0.5f,
                    item.label, nullptr);
    }
}

} // namespace sms::ui::dpf
