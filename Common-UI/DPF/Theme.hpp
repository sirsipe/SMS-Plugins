#pragma once

#include "DistrhoUI.hpp"

namespace sms::ui::dpf {

struct Theme {
    DGL_NAMESPACE::Color canvas{15, 15, 18};
    DGL_NAMESPACE::Color surface{27, 27, 33};
    DGL_NAMESPACE::Color surfaceRaised{39, 39, 47};
    DGL_NAMESPACE::Color outline{69, 68, 81};
    DGL_NAMESPACE::Color contentPrimary{232, 225, 214};
    DGL_NAMESPACE::Color contentSecondary{174, 166, 156};
    DGL_NAMESPACE::Color activityPlayback{232, 55, 68};
    DGL_NAMESPACE::Color activityCapture{255, 94, 77};
    DGL_NAMESPACE::Color selection{255, 74, 91};
    DGL_NAMESPACE::Color controlAccent{232, 55, 68};
    DGL_NAMESPACE::Color intentDanger{255, 30, 55};
    DGL_NAMESPACE::Color meterGreen{75, 211, 112};
    DGL_NAMESPACE::Color meterYellow{245, 198, 66};
    DGL_NAMESPACE::Color meterRed{239, 83, 96};

    // Physical material palette. These remain semantic so shared controls do
    // not need to know which product palette is active.
    DGL_NAMESPACE::Color chassisTop{38, 38, 39};
    DGL_NAMESPACE::Color chassisBottom{22, 22, 24};
    DGL_NAMESPACE::Color chassisEdge{91, 88, 83};
    DGL_NAMESPACE::Color recess{13, 13, 15};
    DGL_NAMESPACE::Color recessEdge{7, 7, 8};
    DGL_NAMESPACE::Color panelTop{31, 31, 32};
    DGL_NAMESPACE::Color panelBottom{22, 22, 24};
    DGL_NAMESPACE::Color rubberTop{47, 46, 46};
    DGL_NAMESPACE::Color rubberBottom{25, 25, 27};
    DGL_NAMESPACE::Color controlTop{48, 47, 47};
    DGL_NAMESPACE::Color controlBottom{27, 27, 29};
    DGL_NAMESPACE::Color edgeHighlight{151, 143, 132};
    DGL_NAMESPACE::Color shadow{0, 0, 0};

    float panelRadius = 12.0f;
    float controlRadius = 6.0f;
    float outlineWidth = 1.0f;
    float materialGrainAlpha = 0.035f;
    float shallowShadowAlpha = 0.48f;
    float bevelHighlightAlpha = 0.28f;
    float disabledAlpha = 0.38f;
    float hoverFillAlpha = 0.07f;
    float hoverMenuFillAlpha = 0.10f;
    float hoverHaloAlpha = 0.50f;
};

inline const Theme& theme() noexcept
{
    static const Theme sharedTheme;
    return sharedTheme;
}

} // namespace sms::ui::dpf
