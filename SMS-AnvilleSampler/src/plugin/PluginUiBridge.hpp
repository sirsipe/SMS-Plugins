#pragma once

#include "DistrhoPlugin.hpp"
#include "UiMessageBus.hpp"

START_NAMESPACE_DISTRHO

// The VST3 view receives a Plugin* from DPF. This base makes the optional
// same-process message bus accessible without exposing the sampler to the UI.
class PluginUiBridge : public Plugin {
public:
    [[nodiscard]] virtual std::uint64_t uiMessageCursor() const = 0;
    [[nodiscard]] virtual bool readUiMessage(
        std::uint64_t& cursor, midichopper::plugin::UiMessageBus::Message& message,
        bool& skipped) const = 0;
    virtual void stopUiPreview() = 0;

protected:
    PluginUiBridge(const std::uint32_t parameterCount, const std::uint32_t stateCount)
        : Plugin(parameterCount, 0, stateCount) {}
};

END_NAMESPACE_DISTRHO
