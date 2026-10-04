#pragma once

#include "Audio/WavCodec.hpp"

#include <filesystem>
#include <string>

namespace midichopper::plugin {

struct PadFileResult {
    sms::audio::WavAudio audio;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

[[nodiscard]] PadFileResult readPadWav(const std::filesystem::path& path);
[[nodiscard]] std::string writePadWav(
    const std::filesystem::path& path, const sms::audio::WavAudio& audio);

} // namespace midichopper::plugin
