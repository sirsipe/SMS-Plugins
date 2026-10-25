#pragma once

#include "Configuration.hpp"
#include "IO/FileIO.hpp"

namespace midichopper::plugin {

// A startup default only. Host values always win; never use from audio callbacks.
class BankModePreference {
public:
    static constexpr int defaultMode = static_cast<int>(kDefaultMidiBankMode);

    [[nodiscard]] static std::filesystem::path configPath() noexcept
    {
        try {
            const auto root = sms::io::userConfigDirectory();
            return root.empty() ? std::filesystem::path{}
                                : root / "SMS-AnvilleSampler" / "selected-bank-mode";
        } catch (...) { return {}; }
    }

    [[nodiscard]] static int read(const std::filesystem::path& path) noexcept
    {
        try {
            const auto bytes = sms::io::readBytes(path, 64);
            const std::string contents(bytes.begin(), bytes.end());
            if (contents == encoded(0)) return 0;
            if (contents == encoded(1)) return 1;
        } catch (...) {}
        return defaultMode;
    }

    [[nodiscard]] static bool write(const std::filesystem::path& path, const int mode) noexcept
    {
        if (mode != 0 && mode != 1) return false;
        return sms::io::writeAtomically(path, encoded(mode));
    }

private:
    // Stable names keep the preference independent of translated menu labels.
    [[nodiscard]] static constexpr std::string_view encoded(const int mode) noexcept
    {
        return mode == 0 ? "SMS-BANK-MODE-PREFERENCE 1\nselected\n"
                         : "SMS-BANK-MODE-PREFERENCE 1\nall\n";
    }
};

} // namespace midichopper::plugin
