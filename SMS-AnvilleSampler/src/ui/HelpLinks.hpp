#pragma once

#include <string>
#include <string_view>

namespace midichopper::ui {

inline constexpr std::string_view kIssueUrl =
    "https://github.com/sirsipe/SMS-Plugins/issues";
inline constexpr std::string_view kMoreSkinsUrl =
    "https://github.com/sirsipe/SMS-Plugins/blob/main/SMS-AnvilleSampler/additional-skins/README.md";

[[nodiscard]] inline std::string onlineHelpUrl(const std::string_view ref)
{
    return "https://github.com/sirsipe/SMS-Plugins/tree/" +
           std::string(ref) + "/SMS-AnvilleSampler";
}

} // namespace midichopper::ui
