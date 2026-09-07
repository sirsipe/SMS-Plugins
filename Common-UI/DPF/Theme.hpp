#pragma once

#include "DistrhoUI.hpp"

namespace sms::ui::dpf {

struct Theme {
    DGL_NAMESPACE::Color canvas{14, 17, 24};
    DGL_NAMESPACE::Color surface{22, 27, 37};
    DGL_NAMESPACE::Color surfaceRaised{29, 35, 47};
    DGL_NAMESPACE::Color outline{51, 61, 78};
    DGL_NAMESPACE::Color contentPrimary{232, 238, 247};
    DGL_NAMESPACE::Color contentSecondary{143, 155, 177};
    DGL_NAMESPACE::Color activityPlayback{65, 214, 203};
    DGL_NAMESPACE::Color activityCapture{255, 175, 84};
    DGL_NAMESPACE::Color selection{255, 175, 84};
    DGL_NAMESPACE::Color controlAccent{255, 175, 84};
    DGL_NAMESPACE::Color intentDanger{238, 101, 112};

    float panelRadius = 12.0f;
    float controlRadius = 6.0f;
    float outlineWidth = 1.0f;
};

inline const Theme& theme() noexcept
{
    static const Theme sharedTheme;
    return sharedTheme;
}

} // namespace sms::ui::dpf
