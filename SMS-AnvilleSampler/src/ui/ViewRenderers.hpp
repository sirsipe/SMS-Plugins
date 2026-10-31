#pragma once

#include "ViewRenderContext.hpp"

namespace midichopper::ui::rendering {

// Faders use canvas coordinates; the remaining renderers use content coordinates.
void drawLevelFaders(const ViewRenderContext& ctx);
void drawMainHeader(const ViewRenderContext& ctx);
void drawMainFooter(const ViewRenderContext& ctx);
void drawMainView(const ViewRenderContext& ctx);
void drawSampleEditorView(const ViewRenderContext& ctx);
void drawCutEditorView(const ViewRenderContext& ctx);
void drawMenus(const ViewRenderContext& ctx);

} // namespace midichopper::ui::rendering
