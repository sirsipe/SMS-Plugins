#pragma once

#include "Configuration.hpp"
#include "IO/DefaultsFile.hpp"

namespace midichopper::plugin {

// A startup default only. Host values always win; never use from audio callbacks.
class BankModePreference {
public:
    static constexpr int defaultMode = static_cast<int>(kDefaultMidiBankMode);

    [[nodiscard]] static std::filesystem::path configPath() noexcept
    {
        return sms::io::DefaultsFile::configPath("SMS-AnvilleSampler");
    }

    [[nodiscard]] static int read(const std::filesystem::path& path) noexcept
    {
        try {
            const auto contents = sms::io::DefaultsFile::get(path, "bank_mode");
            if (contents == encoded(0)) return 0;
            if (contents == encoded(1)) return 1;
        } catch (...) {}
        return defaultMode;
    }

    [[nodiscard]] static bool write(const std::filesystem::path& path, const int mode) noexcept
    {
        if (mode != 0 && mode != 1) return false;
        return sms::io::DefaultsFile::set(path, "bank_mode", encoded(mode));
    }

private:
    // Stable names keep the preference independent of translated menu labels.
    [[nodiscard]] static constexpr std::string_view encoded(const int mode) noexcept
    {
        return mode == 0 ? "selected" : "all";
    }
};

} // namespace midichopper::plugin
