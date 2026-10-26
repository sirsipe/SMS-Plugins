#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace sms::io {

// Control-thread only: bounded, flat INI preferences. set() serializes writers
// across threads, processes and separately loaded copies, then merges the latest
// file and atomically replaces it. No persistent lock file is created.
class DefaultsFile {
public:
    using Values = std::map<std::string, std::string, std::less<>>;

    [[nodiscard]] static std::filesystem::path configPath(std::string_view appName) noexcept;
    [[nodiscard]] static Values read(const std::filesystem::path& path) noexcept;
    [[nodiscard]] static std::string get(const std::filesystem::path& path,
                                         std::string_view key) noexcept;
    // Safe ASCII keys: letters, digits, '_', '.', '-'; at most 128 bytes.
    // Values: at most 4096 bytes, without NUL/CR/LF. Files: 64 KiB/256 keys.
    // Unknown valid keys survive updates; malformed individual lines are ignored.
    // Whitespace-delimited values are quoted, with backslash/quote escaping.
    [[nodiscard]] static bool set(const std::filesystem::path& path,
                                  std::string_view key, std::string_view value) noexcept;
};

} // namespace sms::io
