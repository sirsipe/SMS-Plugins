#include "Audio/RealtimeLatestMailbox.hpp"
#include "ChopEditorProtocol.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using midichopper::plugin::ChopPreviewRequest;
using Mailbox = sms::audio::RealtimeLatestMailbox<ChopPreviewRequest>;
void check(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

ChopPreviewRequest request(const std::uint64_t revision)
{
    return {revision % 2U != 0U, static_cast<std::uint32_t>(revision % 61U),
            static_cast<std::uint32_t>(1U + revision % 3U), revision * 1000U,
            revision * 1000U + 999U};
}

bool coherent(const ChopPreviewRequest& value)
{
    const auto revision = value.sourceFrame / 1000U;
    const auto expected = request(revision);
    return value.play == expected.play && value.firstPad == expected.firstPad &&
           value.padCount == expected.padCount && value.sourceFrame == expected.sourceFrame &&
           value.sourceEndFrame == expected.sourceEndFrame;
}

void supersededCommands()
{
    Mailbox mailbox;
    ChopPreviewRequest value;
    check(!mailbox.tryConsume(value), "empty mailbox has no command");
    mailbox.publish(request(1));
    mailbox.publish(request(2)); // stop supersedes play including its entire range
    check(mailbox.tryConsume(value) && coherent(value) && value.sourceFrame == 2000U,
          "latest stop/range supersedes unread play as one value");
    check(!mailbox.tryConsume(value), "consumed command is not replayed");
    mailbox.publish(request(3));
    check(mailbox.tryConsume(value) && coherent(value) && value.sourceFrame == 3000U,
          "next publication remains observable after consumption");
}

void overlappingWritersAndAudio()
{
    Mailbox mailbox;
    std::atomic<bool> start{false};
    std::atomic<unsigned> completed{0};
    constexpr std::uint64_t count = 100000U;
    auto writer = [&](const std::uint64_t offset) {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::uint64_t revision = 1U; revision <= count; ++revision)
            mailbox.publish(request(offset + revision));
        completed.fetch_add(1U, std::memory_order_release);
    };
    std::thread first(writer, 0U), second(writer, count);
    start.store(true, std::memory_order_release);
    ChopPreviewRequest value;
    unsigned consumed = 0U;
    while (completed.load(std::memory_order_acquire) < 2U) {
        if (mailbox.tryConsume(value)) {
            check(coherent(value), "overlap never mixes command, pad, start or end revisions");
            ++consumed;
        }
    }
    first.join();
    second.join();
    if (mailbox.tryConsume(value)) {
        check(coherent(value), "final pending publication remains coherent");
        ++consumed;
    }
    check(consumed != 0U, "reader consumed concurrent publications");
    mailbox.publish(request(count * 2U + 1U));
    check(mailbox.tryConsume(value) && value.sourceFrame == (count * 2U + 1U) * 1000U &&
          coherent(value), "latest value is delivered after overlap");
    check(!mailbox.tryConsume(value), "final threaded value is consumed only once");
}
}

int main()
{
    supersededCommands();
    overlappingWritersAndAudio();
    std::cout << "Preview mailbox tests passed\n";
}
