#pragma once

#include "DistrhoUI.hpp"

namespace sms::ui::dpf {

struct Theme {
    DGL_NAMESPACE::Color canvas{15, 15, 18};
    DGL_NAMESPACE::Color surface{27, 27, 33};
    DGL_NAMESPACE::Color surfaceRaised{39, 39, 47};
    DGL_NAMESPACE::Color outline{69, 68, 81};
    DGL_NAMESPACE::Color contentPrimary{244, 241, 246};
    DGL_NAMESPACE::Color contentSecondary{165, 158, 174};
    DGL_NAMESPACE::Color activityPlayback{232, 55, 68};
    DGL_NAMESPACE::Color activityCapture{255, 94, 77};
    DGL_NAMESPACE::Color selection{255, 74, 91};
    DGL_NAMESPACE::Color controlAccent{232, 55, 68};
    DGL_NAMESPACE::Color intentDanger{255, 30, 55};
    DGL_NAMESPACE::Color meterGreen{75, 211, 112};
    DGL_NAMESPACE::Color meterYellow{245, 198, 66};
    DGL_NAMESPACE::Color meterRed{239, 83, 96};

    float panelRadius = 12.0f;
    float controlRadius = 6.0f;
    float outlineWidth = 1.0f;
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
