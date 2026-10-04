#pragma once

#include <string>
#include <string_view>

namespace midichopper::ui {

inline constexpr std::string_view kIssueUrl =
    "https://github.com/sirsipe/SMS-Plugins/issues";

[[nodiscard]] inline std::string onlineHelpUrl(const std::string_view ref)
{
    return "https://github.com/sirsipe/SMS-Plugins/tree/" +
           std::string(ref) + "/SMS-AnvilSampler";
}

} // namespace midichopper::ui
