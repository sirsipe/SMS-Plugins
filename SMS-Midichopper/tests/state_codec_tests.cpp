#include "StateCodec.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void roundTrip()
{
    midichopper::PadData original;
    original.sampleRate = 44100.0;
    original.frames = 3;
    original.stereo = {-1.0f, -0.5f, 0.0f, 0.25f, 0.75f, 1.0f};

    const std::string encoded = midichopper::plugin::encodePadState(original, 44100U);
    check(!encoded.empty(), "encoded state is not empty");

    midichopper::plugin::DecodedPadState decoded;
    check(midichopper::plugin::decodePadState(encoded.c_str(), decoded), "valid state decodes");
    check(decoded.sourceSampleRate == 44100U, "sample rate round-trips");
    check(decoded.pad.frames == original.frames, "frame count round-trips");
    check(decoded.pad.stereo.size() == original.stereo.size(), "channel data size round-trips");
    for (std::size_t index = 0; index < original.stereo.size(); ++index)
        check(std::abs(decoded.pad.stereo[index] - original.stereo[index]) < 4.0e-5f,
              "PCM16 round-trip tolerance");
}

void rejectsDamage()
{
    midichopper::PadData original;
    original.sampleRate = 48000.0;
    original.frames = 2;
    original.stereo = {0.1f, -0.1f, 0.2f, -0.2f};
    std::string encoded = midichopper::plugin::encodePadState(original, 48000U);
    check(encoded.size() > 8U, "test state has payload");

    midichopper::plugin::DecodedPadState decoded;
    encoded[encoded.size() / 2U] = encoded[encoded.size() / 2U] == 'A' ? 'B' : 'A';
    check(!midichopper::plugin::decodePadState(encoded.c_str(), decoded), "CRC rejects modified payload");
    check(!midichopper::plugin::decodePadState("not base64", decoded), "invalid Base64 is rejected");
    check(!midichopper::plugin::decodePadState(nullptr, decoded), "null state is rejected");
}

} // namespace

int main()
{
    roundTrip();
    rejectsDamage();
    std::cout << "state codec tests passed\n";
}
