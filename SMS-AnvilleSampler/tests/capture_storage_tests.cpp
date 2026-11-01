#include "../src/core/SamplerEngine.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace probe {
thread_local bool audio = false;
std::thread::id control;
std::atomic<unsigned> allocations{0}, deletions{0}, workerAttempts{0};
std::atomic<bool> delay{false}, entered{false}, fail{false};
constexpr std::size_t refillPcmBytes = 60U * 1024U * 2U * sizeof(float);
void allocate(const std::size_t bytes) {
    if (audio) allocations.fetch_add(1U);
    if (std::this_thread::get_id() == control || bytes < refillPcmBytes) return;
    workerAttempts.fetch_add(1U);
    entered.store(true, std::memory_order_release);
    while (delay.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (fail.load(std::memory_order_acquire)) throw std::bad_alloc{};
}
void release(void* pointer) noexcept {
    if (audio && pointer) deletions.fetch_add(1U);
}
}

void* operator new(const std::size_t bytes) {
    probe::allocate(bytes);
    if (auto* pointer = std::malloc(bytes ? bytes : 1U)) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { probe::release(pointer); std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    probe::allocate(bytes);
#ifdef _WIN32
    if (auto* pointer = _aligned_malloc(bytes ? bytes : 1U, static_cast<std::size_t>(alignment)))
        return pointer;
#else
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), bytes ? bytes : 1U) == 0)
        return pointer;
#endif
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept {
#ifdef _WIN32
    probe::release(pointer); _aligned_free(pointer);
#else
    ::operator delete(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }

namespace {
using namespace midichopper;
constexpr std::uint32_t frames = 1024U;
void check(const bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
template<class Predicate> void await(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Capture {
    SamplerEngine engine{1024.0};
    std::array<float, frames> left{}, right{}, outLeft{}, outRight{};
    EngineSettings settings{};
    float voiceLeft = 0.0f, voiceRight = 0.0f;
    Capture() {
        left.fill(0.25f); right.fill(-0.125f);
        settings.monitorInput = true;
        settings.maxVoices = 16U;
    }
    void arm(const bool value) {
        settings.armed = value;
        engine.setSettings(settings);
    }
    void block(const MidiEvent* event = nullptr, const std::uint32_t count = frames) {
        probe::audio = true;
        engine.process(left.data(), right.data(), outLeft.data(), outRight.data(), count,
                       event, event ? 1U : 0U);
        probe::audio = false;
        for (std::uint32_t i = 0; i < count; ++i)
            check(outLeft[i] == left[i] + voiceLeft && outRight[i] == right[i] + voiceRight,
                  "refill failure/delay leaves every monitoring and unrelated playback frame unchanged");
    }
    void retainedVoice() {
        PadData source;
        source.sampleRate = 2.0; source.frames = 128U;
        source.peak = source.rms = 0.25f;
        source.stereo.resize(256U);
        for (std::uint32_t i = 0; i < 128U; ++i) {
            source.stereo[i * 2U] = 0.125f; source.stereo[i * 2U + 1U] = -0.25f;
        }
        check(engine.importPad(1U, source), "install unrelated 64-second performance voice");
        arm(false);
        voiceLeft = 0.125f; voiceRight = -0.25f;
        const MidiEvent play{0U, 37U, 127U, MidiEventType::NoteOn};
        block(&play);
    }
    void preservedPad(const std::uint32_t pad, const std::uint32_t expectedFrames) {
        PadData pcm;
        check(engine.exportPad(pad, pcm) && pcm.frames == expectedFrames, "completed capture preserves exact frame count");
        for (std::uint32_t i = 0; i < expectedFrames; ++i)
            check(pcm.stereo[i * 2U] == 0.25f && pcm.stereo[i * 2U + 1U] == -0.125f,
                  "exhaustion/recovery preserves every captured stereo frame");
    }
};

void delayedRefillExhaustion(const bool offline) {
    Capture capture;
    capture.engine.setOffline(offline);
    capture.retainedVoice();
    capture.arm(true);
    const auto initial = capture.engine.captureCapacityFrames();
    probe::entered.store(false);
    probe::delay.store(true);
    const MidiEvent note{17U, 36U, 127U, MidiEventType::NoteOn};
    capture.block(&note);
    for (unsigned block = 1U; block < 16U; ++block) capture.block();
    await([] { return probe::entered.load(); }, "background refill reached delay gate");
    for (unsigned block = 0U; block < 17U; ++block) capture.block();
    check(capture.engine.captureStorageExhausted() && !capture.engine.padMetadata(0U).recording,
          "late worker finalizes capture instead of waiting on audio");
    check(capture.engine.padMetadata(1U).active, "unrelated voice survives capture exhaustion");
    capture.preservedPad(0U, static_cast<std::uint32_t>(initial));
    probe::delay.store(false);
    await([&] {
        capture.block(nullptr, 0U);
        return capture.engine.freeCaptureFrames() == (offline ? 120U : 60U) * frames;
    }, "delayed refill completes after exhaustion");
    check(capture.engine.freeCaptureFrames() == (offline ? 120U : 60U) * frames,
          "late chunk remains available for reuse");
    capture.engine.setSampleRate(2048.0);
    check(!capture.engine.captureStorageExhausted(), "successful sample-rate reconfiguration clears exhaustion status");
    capture.preservedPad(0U, static_cast<std::uint32_t>(initial));
}

void failedRefillAndRearmRecovery(const bool offline) {
    Capture capture;
    capture.engine.setOffline(offline);
    const auto initial = capture.engine.captureCapacityFrames();
    probe::workerAttempts.store(0U);
    probe::fail.store(true);
    capture.arm(true);
    const MidiEvent note{0U, 36U, 127U, MidiEventType::NoteOn};
    capture.block(&note);
    for (unsigned block = 1U; block < 16U; ++block) capture.block();
    await([&] { return capture.engine.captureAllocationFailed(); }, "refill allocation failure is caught");
    for (unsigned block = 0U; block < 16U; ++block) capture.block();
    check(capture.engine.captureStorageExhausted() && probe::workerAttempts.load() == 1U,
          "failed refill latches without retry storms and capture stops cleanly");
    capture.preservedPad(0U, static_cast<std::uint32_t>(initial));
    probe::fail.store(false);
    capture.arm(false);
    capture.arm(true);
    await([&] {
        capture.block(nullptr, 0U);
        return capture.engine.freeCaptureFrames() == (offline ? 120U : 60U) * frames;
    }, "rearming retries even when no free blocks remain");
    check(!capture.engine.captureAllocationFailed() && !capture.engine.captureStorageExhausted(),
          "new Arm session clears failure/exhaustion after refill recovery");
    capture.block(&note);
    capture.block();
    capture.arm(false);
    capture.block(nullptr, 0U);
    capture.preservedPad(1U, frames * 2U);
    capture.preservedPad(0U, static_cast<std::uint32_t>(initial));
}

void offlineProfileAndReturnToRealtime() {
    Capture capture;
    probe::audio = true;
    capture.engine.setOffline(true);
    probe::audio = false;
    capture.arm(true);
    capture.block(nullptr, 0U);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(capture.engine.captureCapacityFrames() == 30U * frames,
          "offline mode and Arm alone do not request capture growth");
    const MidiEvent note{0U, 36U, 127U, MidiEventType::NoteOn};
    capture.block(&note);
    await([&] {
        capture.block(nullptr, 0U);
        return capture.engine.freeCaptureFrames() == 149U * frames;
    }, "offline capture requests two minutes immediately from its thirty-second reserve");
    check(capture.engine.captureCapacityFrames() == 150U * frames,
          "offline refill adds exactly two minutes");
    for (unsigned block = 1U; block < 120U; ++block) capture.block();
    check(capture.engine.freeCaptureFrames() == 30U * frames,
          "offline capture reaches its thirty-second refill threshold");
    await([&] {
        capture.block(nullptr, 0U);
        return capture.engine.freeCaptureFrames() == 150U * frames;
    }, "offline capture requests another two minutes at thirty seconds free");
    check(capture.engine.captureCapacityFrames() == 270U * frames,
          "repeated offline refills use the same chunk size");
    probe::audio = true;
    capture.engine.setOffline(false);
    probe::audio = false;
    while (capture.engine.freeCaptureFrames() > 16U * frames) capture.block();
    check(capture.engine.captureCapacityFrames() == 270U * frames,
          "returning to realtime restores the smaller refill threshold");
    capture.block();
    await([&] {
        capture.block(nullptr, 0U);
        return capture.engine.freeCaptureFrames() == 75U * frames;
    }, "realtime mode adds one minute at fifteen seconds free");
    check(capture.engine.captureCapacityFrames() == 330U * frames,
          "returning to realtime restores the one-minute refill size");
    capture.arm(false);
    capture.block(nullptr, 0U);
    capture.preservedPad(0U, 255U * frames);
}
}

int main() {
    probe::control = std::this_thread::get_id();
    for (const bool offline : {false, true}) {
        delayedRefillExhaustion(offline);
        failedRefillAndRearmRecovery(offline);
    }
    offlineProfileAndReturnToRealtime();
    check(probe::allocations.load() == 0U && probe::deletions.load() == 0U,
          "capture exhaustion, recovery, and chunk adoption allocate/free nothing on audio");
    std::cout << "Capture storage failure and delay tests passed\n";
}
