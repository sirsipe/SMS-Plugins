#include "Audio/WaveformSummary.hpp"
#include "PadStructureProtocol.hpp"
#include "UiMessageBus.hpp"
#include "WaveformDetailProtocol.hpp"

#include <cstdlib>
#include <cstring>
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

void independentReadersAndWrap()
{
    midichopper::plugin::UiMessageBus bus;
    auto first = bus.cursor();
    auto second = bus.cursor();
    check(bus.publish("waveform_data", "one"), "publish first message");
    check(bus.publish("chop_status", "two"), "publish second message");
    midichopper::plugin::UiMessageBus::Message message;
    bool skipped = false;
    check(bus.read(first, message, skipped) && !skipped &&
          std::strcmp(message.value.data(), "one") == 0,
          "first reader receives first message");
    check(bus.read(second, message, skipped) && !skipped &&
          std::strcmp(message.value.data(), "one") == 0,
          "second reader receives same first message");
    check(bus.read(first, message, skipped) && !skipped &&
          std::strcmp(message.key.data(), "chop_status") == 0,
          "first reader receives second message in order");
    check(bus.read(second, message, skipped) && !skipped &&
          std::strcmp(message.value.data(), "two") == 0,
          "second reader receives second message");
    check(!bus.read(first, message, skipped) && !skipped,
          "reader stops at current end");

    for (std::size_t i = 0; i < bus.kCapacity + 5U; ++i)
        check(bus.publish("status", std::to_string(i).c_str()), "publish ring message");
    check(bus.read(first, message, skipped) && skipped &&
          std::strcmp(message.value.data(), "5") == 0,
          "slow reader resumes at oldest retained message");
}

void boundsAndRealPayload()
{
    midichopper::plugin::UiMessageBus bus;
    const auto initial = bus.cursor();
    const std::string longKey(bus.kKeyCapacity, 'k');
    const std::string longValue(bus.kValueCapacity, 'v');
    check(!bus.publish(longKey.c_str(), "x") &&
          !bus.publish("x", longValue.c_str()) && bus.cursor() == initial,
          "oversize messages do not enter the ring");

    sms::audio::WaveformSummary waveform{};
    waveform.pad = 1U;
    waveform.frames = 48000U;
    waveform.sampleRate = 48000.0;
    waveform.minimum.fill(-1.0f);
    waveform.maximum.fill(1.0f);
    midichopper::plugin::SplitPlanReady ready{};
    ready.planId = 4U;
    ready.targetPad = 1U;
    ready.emptyPad = 2U;
    ready.waveform = waveform;
    const auto encoded = midichopper::plugin::encodeSplitPlanReady(ready);
    check(bus.publish("pad_structure_status", encoded.c_str()),
          "split plan with waveform fits the message bound");
    const midichopper::plugin::WaveformDetailReply detail{
        {9U, 1U, 1U, 0U, 48000U}, waveform};
    const auto detailEncoded = midichopper::plugin::encodeWaveformDetailReply(detail);
    check(bus.publish("waveform_detail_data", detailEncoded.c_str()),
          "zoomed waveform reply fits the VST3 message bound");
    auto reader = initial;
    midichopper::plugin::UiMessageBus::Message message;
    bool skipped = false;
    check(bus.read(reader, message, skipped) && !skipped &&
          std::strcmp(message.value.data(), encoded.c_str()) == 0,
          "large payload remains intact");
}

} // namespace

int main()
{
    independentReadersAndWrap();
    boundsAndRealPayload();
}
