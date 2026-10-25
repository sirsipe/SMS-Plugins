#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sms::io {

// Control-thread helpers only; never call from an audio callback.
// UTF-8 strings cross application boundaries; filesystem paths stay native.
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view value)
{
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

// Empty results indicate unavailable directories or missing/unreadable/oversized files.
[[nodiscard]] std::filesystem::path userConfigDirectory() noexcept;
#if defined(_WIN32)
// Find our loaded module through the wide API.
[[nodiscard]] std::filesystem::path moduleBinaryPath() noexcept;
#endif
[[nodiscard]] std::vector<std::uint8_t> readBytes(
    const std::filesystem::path& path, std::size_t maxBytes) noexcept;
[[nodiscard]] bool writeAtomically(
    const std::filesystem::path& path, std::string_view contents) noexcept;

} // namespace sms::io
