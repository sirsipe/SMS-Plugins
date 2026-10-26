#pragma once

#include "GlobalValueMode.hpp"
#include "IO/DefaultsFile.hpp"

namespace midichopper::plugin {

// Startup defaults only. Durable host state is authoritative for each instance.
struct KnobPreference {
    inline static constexpr std::array<const char*, kGlobalValueControls.size()> keys{
        "knob.output", "knob.pan", "knob.tune", "knob.lowpass", "knob.highpass"};

    [[nodiscard]] static std::filesystem::path configPath() noexcept
    {
        return sms::io::DefaultsFile::configPath("SMS-AnvilleSampler");
    }

    [[nodiscard]] static auto read(const std::filesystem::path& path) noexcept
    {
        auto settings = defaultGlobalValueSettings();
        const auto values = sms::io::DefaultsFile::read(path);
        for (std::size_t i = 0; i < settings.size(); ++i) {
            const auto found = values.find(keys[i]);
            if (found != values.end())
                static_cast<void>(decodeGlobalValueSettings(
                    kGlobalValueControls[i].parameter, found->second, settings[i]));
        }
        return settings;
    }

    [[nodiscard]] static bool write(const std::filesystem::path& path,
        const std::uint32_t parameter, const GlobalValueSettings& settings) noexcept
    {
        if (!validGlobalValueSettings(parameter, settings))
            return false;
        try {
            return sms::io::DefaultsFile::set(path, keys[globalControlIndex(parameter)],
                                             encodeGlobalValueSettings(settings));
        } catch (...) {
            return false;
        }
    }
};

} // namespace midichopper::plugin
