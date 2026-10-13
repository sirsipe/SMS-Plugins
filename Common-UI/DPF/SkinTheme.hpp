#pragma once

#include "Theme.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace sms::ui::dpf {

struct ThemeColorField {
    std::string_view name;
    DGL_NAMESPACE::Color Theme::*member;
};
inline constexpr std::array themeColorFields{
    ThemeColorField{"canvas", &Theme::canvas},
    ThemeColorField{"surface", &Theme::surface},
    ThemeColorField{"surfaceRaised", &Theme::surfaceRaised},
    ThemeColorField{"outline", &Theme::outline},
    ThemeColorField{"contentPrimary", &Theme::contentPrimary},
    ThemeColorField{"contentSecondary", &Theme::contentSecondary},
    ThemeColorField{"activityPlayback", &Theme::activityPlayback},
    ThemeColorField{"activityCapture", &Theme::activityCapture},
    ThemeColorField{"selection", &Theme::selection},
    ThemeColorField{"controlAccent", &Theme::controlAccent},
    ThemeColorField{"controlActive", &Theme::controlActive},
    ThemeColorField{"controlActiveContent", &Theme::controlActiveContent},
    ThemeColorField{"intentDanger", &Theme::intentDanger},
    ThemeColorField{"meterGreen", &Theme::meterGreen},
    ThemeColorField{"meterYellow", &Theme::meterYellow},
    ThemeColorField{"meterRed", &Theme::meterRed},
    ThemeColorField{"logoGlow", &Theme::logoGlow},
    ThemeColorField{"chassisTop", &Theme::chassisTop},
    ThemeColorField{"chassisBottom", &Theme::chassisBottom},
    ThemeColorField{"chassisEdge", &Theme::chassisEdge},
    ThemeColorField{"recess", &Theme::recess},
    ThemeColorField{"recessEdge", &Theme::recessEdge},
    ThemeColorField{"panelTop", &Theme::panelTop},
    ThemeColorField{"panelBottom", &Theme::panelBottom},
    ThemeColorField{"rubberTop", &Theme::rubberTop},
    ThemeColorField{"rubberBottom", &Theme::rubberBottom},
    ThemeColorField{"controlTop", &Theme::controlTop},
    ThemeColorField{"controlBottom", &Theme::controlBottom},
    ThemeColorField{"edgeHighlight", &Theme::edgeHighlight},
    ThemeColorField{"shadow", &Theme::shadow},
    ThemeColorField{"faderTop", &Theme::faderTop},
    ThemeColorField{"faderBottom", &Theme::faderBottom},
};

struct ThemeMetricField {
    std::string_view name;
    float Theme::*member;
    float minimum;
    float maximum;
};
inline constexpr std::array themeMetricFields{
    ThemeMetricField{"backgroundAlpha", &Theme::backgroundAlpha, 0.0f, 1.0f},
    ThemeMetricField{"padAlpha", &Theme::padAlpha, 0.0f, 1.0f},
    ThemeMetricField{"padPanelAlpha", &Theme::padPanelAlpha, 0.0f, 1.0f},
    ThemeMetricField{"padTintAlpha", &Theme::padTintAlpha, 0.0f, 1.0f},
    ThemeMetricField{"panelRadius", &Theme::panelRadius, 0.0f, 24.0f},
    ThemeMetricField{"controlRadius", &Theme::controlRadius, 0.0f, 24.0f},
    ThemeMetricField{"outlineWidth", &Theme::outlineWidth, 0.0f, 24.0f},
    ThemeMetricField{"materialGrainAlpha", &Theme::materialGrainAlpha, 0.0f, 1.0f},
    ThemeMetricField{"shallowShadowAlpha", &Theme::shallowShadowAlpha, 0.0f, 1.0f},
    ThemeMetricField{"bevelHighlightAlpha", &Theme::bevelHighlightAlpha, 0.0f, 1.0f},
    ThemeMetricField{"disabledAlpha", &Theme::disabledAlpha, 0.0f, 1.0f},
    ThemeMetricField{"hoverFillAlpha", &Theme::hoverFillAlpha, 0.0f, 1.0f},
    ThemeMetricField{"hoverMenuFillAlpha", &Theme::hoverMenuFillAlpha, 0.0f, 1.0f},
    ThemeMetricField{"hoverHaloAlpha", &Theme::hoverHaloAlpha, 0.0f, 1.0f},
    ThemeMetricField{"logoGlowAlpha", &Theme::logoGlowAlpha, 0.0f, 1.0f},
    ThemeMetricField{"logoGlowRadius", &Theme::logoGlowRadius, 0.0f, 24.0f},
    ThemeMetricField{"logoGlowPulseSeconds", &Theme::logoGlowPulseSeconds, 0.0f, 60.0f},
};

inline Theme skinTheme(const sms::ui::Skin& skin)
{
    Theme result;
    const auto applyColor = [&](const std::string_view key, DGL_NAMESPACE::Color& color) {
        const auto entry = skin.colors.find(key);
        std::array<std::uint8_t, 4> rgba{};
        if (entry != skin.colors.end() && sms::ui::Skin::parseColor(entry->second, rgba))
            color = DGL_NAMESPACE::Color(static_cast<int>(rgba[0]),
                static_cast<int>(rgba[1]), static_cast<int>(rgba[2]),
                static_cast<float>(rgba[3]) / 255.0f);
    };
    for (const auto& field : themeColorFields)
        applyColor(field.name, result.*field.member);
    constexpr std::array<std::string_view, Theme::padColorCount> padKeys{
        "pad1", "pad2", "pad3", "pad4", "pad5", "pad6"};
    for (std::size_t index = 0; index < padKeys.size(); ++index)
        applyColor(padKeys[index], result.padColors[index].tint);
    for (const auto& field : themeMetricFields) {
        const auto entry = skin.metrics.find(field.name);
        float value = 0.0f;
        if (entry != skin.metrics.end() && sms::ui::Skin::parseMetric(entry->second, value)
            && value >= field.minimum && value <= field.maximum)
            result.*field.member = value;
    }
    return result;
}

} // namespace sms::ui::dpf
