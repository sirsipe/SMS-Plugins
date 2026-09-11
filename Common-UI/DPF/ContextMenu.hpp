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
        const bool hovered = item.enabled && hoveredItem == static_cast<int>(index);
        const auto& accent = item.dangerous ? colors.intentDanger : colors.controlAccent;

        if (hovered) {
            canvas.beginPath();
            canvas.roundedRect(bounds.x, bounds.y, bounds.width, bounds.height,
                               colors.controlRadius);
            canvas.fillColor(accent.withAlpha(0.18f));
            canvas.fill();
        }

        canvas.fontFace(NANOVG_DEJAVU_SANS_TTF);
        canvas.fontSize(11.0f);
        canvas.textAlign(DGL_NAMESPACE::NanoVG::ALIGN_LEFT |
                         DGL_NAMESPACE::NanoVG::ALIGN_MIDDLE);
        canvas.fillColor(item.enabled
            ? (hovered ? accent : colors.contentPrimary)
            : colors.contentSecondary.withAlpha(0.45f));
        canvas.text(bounds.x + 11.0f, bounds.y + bounds.height * 0.5f,
                    item.label, nullptr);
    }
}

} // namespace sms::ui::dpf
