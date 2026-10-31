#pragma once

#include "DistrhoPlugin.hpp"
#include "GlobalValueMode.hpp"

namespace midichopper::plugin {

// DPF registration only. The adapter supplies immutable construction-time
// defaults, including factory values for dummy discovery/export instances.
[[nodiscard]] std::uint32_t pluginStateCount() noexcept;
void initPluginParameter(std::uint32_t index, DISTRHO::Parameter& parameter,
                         int initialLayout, int initialBankMode);
void initPluginState(std::uint32_t index, DISTRHO::State& state,
                     const std::array<GlobalValueSettings, kGlobalValueControls.size()>& initialSettings);

} // namespace midichopper::plugin
