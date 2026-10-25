#pragma once

#include "Configuration.hpp"
#include "IO/FileIO.hpp"

namespace midichopper::plugin {

// Per-user startup default only; host values always replace it. Call only at
// instance construction or from an explicit UI selection, never from audio.
class LayoutPreference {
public:
    [[nodiscard]] static std::filesystem::path configPath() noexcept
    {
        try {
            const auto root = sms::io::userConfigDirectory();
            return root.empty() ? std::filesystem::path{}
                                : root / "SMS-AnvilleSampler" / "selected-layout";
        } catch (...) { return {}; }
    }

    [[nodiscard]] static int read(const std::filesystem::path& path) noexcept
    {
        try {
            const auto bytes = sms::io::readBytes(path, 64);
            const std::string contents(bytes.begin(), bytes.end());
            for (std::size_t index = 0; index < kPadsPerBankByLayout.size(); ++index) {
                if (contents == encoded(static_cast<int>(index)))
                    return static_cast<int>(index);
            }
        } catch (...) {}
        return 0;
    }

    [[nodiscard]] static bool write(const std::filesystem::path& path, const int layout) noexcept
    {
        if (layout < 0 || static_cast<std::size_t>(layout) >= kPadsPerBankByLayout.size())
            return false;
        try {
            return sms::io::writeAtomically(path, encoded(layout));
        } catch (...) { return false; }
    }

private:
    // Persist the pad count rather than an incidental menu/parameter index.
    [[nodiscard]] static std::string encoded(const int layout)
    {
        return "SMS-LAYOUT-PREFERENCE 1\n" +
            std::to_string(kPadsPerBankByLayout[static_cast<std::size_t>(layout)]) + "\n";
    }
};

} // namespace midichopper::plugin
