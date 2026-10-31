#pragma once

#include "ViewRenderContext.hpp"

namespace midichopper::ui::rendering {

// Product controls share layout and interaction identities, while generic DPF
// materials and widgets remain in Common-UI.
void drawViewportControls(const ViewRenderContext& ctx, const sms::ui::Rect zoom,
                          const sms::ui::Rect scroll);

void drawMixerKnob(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                   const InteractiveType type, const int index, const char* const label,
                   const float value, const float minimum, const float maximum,
                   const char* const display);

void drawDirtySwitch(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                     const InteractiveType type, const bool enabled);

void drawDirectionToggle(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                         const InteractiveType type, const bool reverse);

void drawGlobalMixerToggle(const ViewRenderContext& ctx);

void drawSlopeSlider(const ViewRenderContext& ctx, const sms::ui::Rect bounds,
                     const InteractiveType type, const float slope);

void drawGlobalMixer(const ViewRenderContext& ctx);

void drawControlPanelBacking(const ViewRenderContext& ctx);

} // namespace midichopper::ui::rendering
