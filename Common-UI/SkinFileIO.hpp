#pragma once

#include "IO/FileIO.hpp"

namespace sms::ui::skinFileIO {

using sms::io::pathFromUtf8;
using sms::io::pathToUtf8;
using sms::io::userConfigDirectory;
#if defined(_WIN32)
using sms::io::moduleBinaryPath;
#endif
using sms::io::readBytes;
using sms::io::writeAtomically;

// UI thread only. Empty results indicate missing/unreadable/oversized images.
inline constexpr std::size_t maxImageBytes = 64 * 1024 * 1024;
[[nodiscard]] inline std::vector<std::uint8_t> readImage(const std::filesystem::path& path) noexcept
{
    return readBytes(path, maxImageBytes);
}

} // namespace sms::ui::skinFileIO
