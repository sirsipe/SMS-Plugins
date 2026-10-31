#pragma once

#include "MidichopperView.hpp"
#include "MidichopperInteraction.hpp"
#include "DPF/Theme.hpp"

namespace midichopper::ui::rendering {

// A frame borrows its immutable presentation state and the active NanoVG canvas.
// Renderers preserve the caller's draw order and existing canvas/theme scopes.
struct ViewRenderContext {
    DGL_NAMESPACE::NanoVG& canvas;
    const ViewState& state;
};

inline void text(const ViewRenderContext& ctx, float x, float y, const char* label, const char* end)
{
    ctx.canvas.text(x, y, sms::ui::dpf::themeText(label), end);
}

inline void measureText(const ViewRenderContext& ctx, float x, float y, const char* label,
                        const char* end, DGL_NAMESPACE::Rectangle<float>& bounds)
{
    ctx.canvas.textBounds(x, y, sms::ui::dpf::themeText(label), end, bounds);
}

[[nodiscard]] inline bool hovered(const ViewRenderContext& ctx, const InteractiveType type,
                                  const int index = -1) noexcept
{
    return isTarget(ctx.state.hoveredTarget, type, index);
}

} // namespace midichopper::ui::rendering
