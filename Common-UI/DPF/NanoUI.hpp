#pragma once

#include "DistrhoUI.hpp"

#include <algorithm>

namespace sms::ui::dpf {

struct ViewportTransform {
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

/** DPF/NanoVG base with a fixed logical canvas and host-safe repaint policy. */
class NanoUI : public DISTRHO_NAMESPACE::UI {
public:
    NanoUI(const unsigned int logicalWidth, const unsigned int logicalHeight,
           const unsigned int minimumWidth, const unsigned int minimumHeight)
        : DISTRHO_NAMESPACE::UI(logicalWidth, logicalHeight),
          logicalWidth_(static_cast<float>(logicalWidth)),
          logicalHeight_(static_cast<float>(logicalHeight))
    {
        // Input and rendering use the same explicit logical transform, so DPF
        // automatic scaling must remain disabled.
        setGeometryConstraints(minimumWidth, minimumHeight, false, false);
    }

protected:
    [[nodiscard]] ViewportTransform viewportTransform() const noexcept
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float scale = std::max(0.1f,
            std::min(width / logicalWidth_, height / logicalHeight_));
        return {scale,
                (width - logicalWidth_ * scale) * 0.5f,
                (height - logicalHeight_ * scale) * 0.5f};
    }

    [[nodiscard]] DGL_NAMESPACE::Point<float>
    toLogicalPosition(const DGL_NAMESPACE::Point<double>& position) const noexcept
    {
        const ViewportTransform transform = viewportTransform();
        return {(static_cast<float>(position.getX()) - transform.offsetX) / transform.scale,
                (static_cast<float>(position.getY()) - transform.offsetY) / transform.scale};
    }

    void beginLogicalDisplay()
    {
        repaintPending_ = false;
        const ViewportTransform transform = viewportTransform();
        save();
        translate(transform.offsetX, transform.offsetY);
        scale(transform.scale, transform.scale);
    }

    void endLogicalDisplay()
    {
        restore();
    }

    void requestRepaint() noexcept
    {
        repaintPending_ = true;
    }

    virtual void onUiIdle() {}

private:
    void uiIdle() final
    {
        onUiIdle();

        // Hosts may retain an LV2 UI and continue sending output values after
        // hiding it. Never invalidate a hidden OpenGL drawable.
        if (repaintPending_ && getWindow().isVisible()) {
            repaintPending_ = false;
            repaint();
        }
    }

    const float logicalWidth_;
    const float logicalHeight_;
    bool repaintPending_ = false;
};

} // namespace sms::ui::dpf
