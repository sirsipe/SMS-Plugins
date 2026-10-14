#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sms::ui::skinFileIO {

// Paths crossing manifest/DPF boundaries are UTF-8; filesystem paths stay native.
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view value)
{
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

// UI thread only. Empty results indicate missing/unreadable/oversized files.
[[nodiscard]] std::filesystem::path userConfigDirectory() noexcept;
#if defined(_WIN32)
// Find our loaded module through the wide API, bypassing DPF's ANSI filename.
[[nodiscard]] std::filesystem::path moduleBinaryPath() noexcept;
#endif
[[nodiscard]] std::vector<std::uint8_t> readBytes(
    const std::filesystem::path& path, std::size_t maxBytes) noexcept;
[[nodiscard]] bool writeAtomically(
    const std::filesystem::path& path, std::string_view contents) noexcept;

inline constexpr std::size_t maxImageBytes = 64 * 1024 * 1024;
[[nodiscard]] inline std::vector<std::uint8_t> readImage(const std::filesystem::path& path) noexcept
{
    return readBytes(path, maxImageBytes);
}

} // namespace sms::ui::skinFileIO
