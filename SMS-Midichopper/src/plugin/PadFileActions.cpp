#include "PadFileActions.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace midichopper::plugin {
namespace {

// The largest accepted 30-second stereo float32 WAV is about 92 MB at 384 kHz.
// Leave bounded room for ordinary metadata without allocating for arbitrary files.
constexpr std::uintmax_t kMaximumWavFileBytes = 128U * 1024U * 1024U;

} // namespace

PadFileResult readPadWav(const std::filesystem::path& path)
{
    PadFileResult result;
    std::error_code fileError;
    const std::uintmax_t size = std::filesystem::file_size(path, fileError);
    if (fileError) {
        result.error = "Could not open WAV file";
        return result;
    }
    if (size > kMaximumWavFileBytes || size > std::numeric_limits<std::size_t>::max()) {
        result.error = "WAV file is too large";
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "Could not open WAV file";
        return result;
    }
    std::vector<std::uint8_t> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (...) {
        result.error = "WAV file is too large";
        return result;
    }
    if (!bytes.empty() && !input.read(
            reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        result.error = "Could not read WAV file";
        return result;
    }

    auto decoded = sms::audio::decodeWav(bytes);
    if (!decoded) {
        result.error = sms::audio::wavErrorMessage(decoded.error);
        return result;
    }
    result.audio = std::move(decoded.audio);
    return result;
}

std::string writePadWav(const std::filesystem::path& path,
                        const sms::audio::WavAudio& audio)
{
    std::vector<std::uint8_t> bytes;
    if (!sms::audio::encodeStereoPcm16Wav(audio, bytes))
        return "Could not encode WAV file";

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return "Could not create WAV file";
    if (!output.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size())))
        return "Could not write WAV file";
    output.close();
    return output ? std::string{} : "Could not finish writing WAV file";
}

} // namespace midichopper::plugin
