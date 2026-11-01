#include "src/DistrhoPluginInternal.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main()
{
    using namespace DISTRHO;
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    d_nextPluginIsDummy = true;
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    d_nextPluginIsDummy = false;
    const auto* instance = static_cast<const Plugin*>(plugin.getInstancePointer());
    check(!instance->isOffline(), "unspecified host processing mode defaults to realtime");
    plugin.setOffline(true);
    check(instance->isOffline(), "offline host signal reaches the plugin instance");
    plugin.setOffline(false);
    check(!instance->isOffline(), "returning to realtime clears the offline host signal");

    check(DISTRHO_PLUGIN_NUM_INPUTS == 2 && DISTRHO_PLUGIN_NUM_OUTPUTS == 2,
          "all formats retain two audio inputs and two audio outputs");
    for (const bool input : {true, false}) {
        for (uint32_t channel = 0; channel < 2; ++channel) {
            const auto& port = plugin.getAudioPort(input, channel);
            check(port.groupId == kPortGroupStereo, "channels remain one stereo group");
            const char* const symbol = input ? (channel == 0 ? "in_l" : "in_r")
                                            : (channel == 0 ? "out_l" : "out_r");
            check(std::string_view(port.symbol.buffer()) == symbol,
                  "port symbols and channel order stay compatible");
            check((port.hints & kAudioPortIsCV) == 0, "ports carry ordinary audio");
#ifdef ANVILLESAMPLER_VST3
            check(bool(port.hints & kAudioPortIsSidechain) == input,
                  "VST3 inputs are sidechain channels; outputs remain main");
#else
            check((port.hints & kAudioPortIsSidechain) == 0,
                  "other formats retain ordinary stereo inputs and outputs");
#endif
        }
    }
    std::cout << "Audio port metadata tests passed\n";
}
