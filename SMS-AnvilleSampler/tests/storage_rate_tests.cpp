#include "DSP/StreamingDownsampler.hpp"
#include "SamplerEngine.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

namespace {
thread_local bool audioThread = false;
thread_local unsigned allocations = 0, deletions = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void release(void* pointer) noexcept {
    if (audioThread && pointer)
        ++deletions;
    std::free(pointer);
}
} // namespace
void* operator new(std::size_t size) {
    if (audioThread)
        ++allocations;
    if (void* p = std::malloc(std::max<std::size_t>(size, 1U)))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }

namespace {
using namespace midichopper;
PadData source(double rate, unsigned frames) {
    PadData result;
    result.sampleRate = rate;
    result.frames = frames;
    result.stereo.resize(frames * 2U);
    for (unsigned i = 0; i < frames; ++i) {
        result.stereo[i * 2] = static_cast<float>(.4 * std::sin(i * .073) + .1 * std::cos(i * 1.97));
        result.stereo[i * 2 + 1] = static_cast<float>(.3 * std::cos(i * .041) - .2 * std::sin(i * 2.11));
    }
    return result;
}
std::vector<float> converted(const PadData& raw) {
    sms::dsp::SincResampler filter;
    double step = raw.sampleRate / kMaximumStoredSampleRate;
    filter.configure(step);
    unsigned frames = static_cast<unsigned>(std::max(1LL, std::llround(raw.frames / step)));
    std::vector<float> result(frames * 2U);
    for (unsigned i = 0; i < frames; ++i) {
        auto value = filter.read(i * step, 0U, raw.frames, false, [&](unsigned frame) noexcept {
            return sms::dsp::StereoFrame{raw.stereo[frame * 2], raw.stereo[frame * 2 + 1]};
        });
        result[i * 2] = value.left;
        result[i * 2 + 1] = value.right;
    }
    return result;
}
void metadata(const PadData& pad) {
    float peak = 0;
    double energy = 0;
    for (float sample : pad.stereo) {
        peak = std::max(peak, std::abs(sample));
        energy += double(sample) * sample;
    }
    check(std::abs(pad.peak - peak) < 1e-6f, "metadata peak describes retained converted PCM");
    check(std::abs(pad.rms - std::sqrt(energy / pad.stereo.size())) < 1e-6,
          "metadata RMS describes retained converted PCM");
}
void streamMatchesBatch() {
    for (double rate : {96000.1, 100000., 176400., 192000., 352800., 384000.})
        for (unsigned length : {1U, 2U, 3U, 31U, 1025U, 4096U}) {
            const auto raw = source(rate, length);
            const auto expected = converted(raw);
            sms::dsp::StreamingDownsampler stream;
            stream.configure(rate, 96000);
            std::vector<float> result;
            result.reserve(expected.size());
            auto sink = [&](sms::dsp::StereoFrame frame) noexcept {
                result.push_back(frame.left);
                result.push_back(frame.right);
                return true;
            };
            audioThread = true;
            for (unsigned i = 0; i < length; ++i)
                check(stream.push({raw.stereo[i * 2], raw.stereo[i * 2 + 1]}, sink), "stream accepts input");
            check(stream.finish(sink), "stream flushes its final output centers");
            audioThread = false;
            check(result == expected, "streaming history and edge flush equal whole-source sinc output");
            stream.reset();
            result.clear();
            check(stream.finish(sink) && result.empty(), "empty/reset converter has no tail");
        }
}
void refusedOutputAndReset() {
    sms::dsp::StreamingDownsampler stream;
    stream.configure(192000.0, 96000.0);
    unsigned accepted = 0;
    auto full = [&](sms::dsp::StereoFrame) noexcept {
        if (accepted == 1U)
            return false;
        ++accepted;
        return true;
    };
    bool kept = true;
    audioThread = true;
    for (unsigned i = 0; i < 100U && kept; ++i)
        kept = stream.push({0.25f, -0.5f}, full);
    check(!kept && accepted == 1U && !stream.finish(full),
          "converter propagates exhausted storage without spinning");
    stream.reset();
    check(stream.push({0.75f, -0.25f}, full), "reset converter accepts fresh input before its right taps");
    auto resumed = [&](sms::dsp::StereoFrame frame) noexcept {
        check(frame.left == 0.75f && frame.right == -0.25f, "reset drops old history and rejected output");
        return true;
    };
    check(stream.finish(resumed), "reset converter edge-flushes a one-frame capture");
    audioThread = false;
}

void rateChangeFinishesCapture() {
    SamplerEngine engine(384000.0, 1.0);
    auto raw = source(384000.0, 31U);
    auto settings = engine.settings();
    settings.armed = true;
    engine.setSettings(settings);
    std::array<float, 31U> left{}, right{}, outL{}, outR{};
    for (unsigned i = 0; i < left.size(); ++i) {
        left[i] = raw.stereo[i * 2U];
        right[i] = raw.stereo[i * 2U + 1U];
    }
    MidiEvent on{0U, 36U, 127U, MidiEventType::NoteOn};
    audioThread = true;
    engine.process(left.data(), right.data(), outL.data(), outR.data(), left.size(), &on, 1U);
    audioThread = false;
    check(engine.padMetadata(0U).recording, "host rate change starts with pending FIR output");
    engine.setSampleRate(48000.0); // Control thread: finish at the old rate first.
    PadData saved;
    check(engine.exportPad(0U, saved), "host rate change retains pending capture");
    check(saved.sampleRate == 96000.0 && saved.stereo == converted(raw),
          "host rate change edge-flushes using the old conversion ratio");
    metadata(saved);
}

void importsAndRestoration() {
    SamplerEngine engine(192000, 1.0);
    for (double rate : {44100., 48000., 96000., 176400., 192000., 384000.}) {
        auto raw = source(rate, 4097);
        check(engine.importPad(0, raw), "import supported source rate");
        PadData stored;
        check(engine.exportPad(0, stored), "export capped PCM");
        check(stored.sampleRate == std::min(rate, 96000.), "import stores at most 96kHz without upsampling");
        if (rate > 96000) {
            check(stored.stereo == converted(raw),
                  "import uses anti-aliased conversion with aligned duration");
            metadata(stored);
        } else
            check(stored.stereo == raw.stereo, "lower-rate PCM remains bit exact");
        if (rate > 96000) {
            check(engine.restorePad(1, &raw), "older high-rate project can be restored");
            float l = 0, r = 0;
            audioThread = true;
            engine.process(nullptr, nullptr, &l, &r, 0);
            audioThread = false;
            PadData restored;
            check(engine.exportPad(1, restored), "read restored source");
            check(restored.sampleRate == 96000 && restored.stereo == stored.stereo,
                  "state path applies same storage cap");
        }
    }
    const auto before = engine.padMetadata(0U);
    auto excessive = source(192000.0, 65U);
    std::fill(excessive.stereo.begin(), excessive.stereo.end(), std::numeric_limits<float>::max());
    // Removing a negative sinc-lobe contribution causes finite input to
    // overflow float output. A failed conversion must leave the old pad intact.
    excessive.stereo[6U] = excessive.stereo[7U] = 0.0f;
    check(!engine.importPad(0U, excessive), "non-finite converted output is rejected before publication");
    check(engine.padMetadata(0U).generation == before.generation,
          "failed rate conversion preserves the existing pad");
    auto raw = source(384000, 1);
    check(engine.importPad(0, raw) && engine.padMetadata(0).frames == 1,
          "nonempty sub-output-frame input retains one sample");
    engine.setSampleRate(48000);
    check(engine.padMetadata(0).sampleRate == 96000, "host reconfiguration preserves capped completed pads");
}
void capturePartitions() {
    constexpr unsigned frames = 8192;
    constexpr std::array<unsigned, 3> cuts{17, 1107, 5033};
    for (double rate : {100000., 176400., 192000., 384000.})
        for (float preRoll : {0.f, 1.f, 100.f})
            for (bool irregular : {false, true}) {
                SamplerEngine engine(rate, 1.0);
                auto settings = engine.settings();
                settings.armed = true;
                settings.preRollMilliseconds = preRoll;
                engine.setSettings(settings);
                auto raw = source(rate, frames);
                std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
                for (unsigned i = 0; i < frames; ++i) {
                    inL[i] = raw.stereo[i * 2];
                    inR[i] = raw.stereo[i * 2 + 1];
                }
                unsigned event = 0, block = 0;
                constexpr std::array<unsigned, 6> sizes{1, 17, 64, 511, 3, 128};
                for (unsigned offset = 0; offset < frames;) {
                    unsigned count =
                        std::min(frames - offset, irregular ? sizes[block++ % sizes.size()] : frames);
                    std::array<MidiEvent, 3> events{};
                    unsigned n = 0;
                    while (event < cuts.size() && cuts[event] < offset + count) {
                        events[n++] = {cuts[event] - offset, static_cast<std::uint8_t>(36 + event), 127,
                                       MidiEventType::NoteOn};
                        ++event;
                    }
                    audioThread = true;
                    engine.process(inL.data() + offset, inR.data() + offset, outL.data() + offset,
                                   outR.data() + offset, count, events.data(), n);
                    audioThread = false;
                    offset += count;
                }
                check(outL == inL && outR == inR,
                      "high-rate capture preserves raw monitoring and MIDI timing");
                settings.armed = false;
                engine.setSettings(settings);
                audioThread = true;
                engine.process(nullptr, nullptr, outL.data(), outR.data(), 0);
                audioThread = false;
                unsigned history = static_cast<unsigned>(std::llround(preRoll * rate / 1000.));
                for (unsigned pad = 0; pad < cuts.size(); ++pad) {
                    unsigned start = cuts[pad] - std::min(cuts[pad], history);
                    unsigned end = pad + 1 < cuts.size() ? cuts[pad + 1] : frames;
                    PadData segment;
                    segment.sampleRate = rate;
                    segment.frames = end - start;
                    segment.stereo.assign(raw.stereo.begin() + start * 2, raw.stereo.begin() + end * 2);
                    auto expected = converted(segment);
                    unsigned retained =
                        segment.frames - (pad + 1 < cuts.size() ? std::min(history, segment.frames) : 0);
                    unsigned count =
                        retained
                            ? static_cast<unsigned>(std::max(1LL, std::llround(retained * 96000. / rate)))
                            : 0;
                    check(engine.padMetadata(pad).frames == count &&
                              engine.padMetadata(pad).sampleRate == 96000,
                          "cuts and pre-roll map host duration to stored frames");
                    if (count) {
                        PadData stored;
                        check(engine.exportPad(pad, stored), "export captured pad");
                        expected.resize(count * 2);
                        check(stored.stereo == expected, "partitioning, cuts and pre-roll "
                                                         "preserve filtered capture PCM");
                        metadata(stored);
                    }
                }
            }
}
void fixedCapture() {
    for (double rate : {100000., 192000., 384000.}) {
        SamplerEngine engine(rate, 1.0);
        auto raw = source(rate, 4096);
        auto settings = engine.settings();
        settings.armed = true;
        settings.captureMode = CaptureMode::FixedDuration;
        settings.fixedLengthSeconds = 1001. / rate;
        engine.setSettings(settings);
        std::vector<float> in(4096, .25f), out(4096);
        MidiEvent on{13, 36, 127, MidiEventType::NoteOn};
        audioThread = true;
        engine.process(in.data(), in.data(), out.data(), out.data(), 4096, &on, 1);
        audioThread = false;
        check(engine.padMetadata(0).frames == static_cast<unsigned>(std::llround(1001 * 96000. / rate)),
              "fixed capture counts host duration before converting");
        check(!engine.padMetadata(0).recording,
              "fixed capture completes and flushes without extra callbacks");
    }
}
} // namespace
int main() {
    streamMatchesBatch();
    refusedOutputAndReset();
    rateChangeFinishesCapture();
    importsAndRestoration();
    capturePartitions();
    fixedCapture();
    check(allocations == 0 && deletions == 0,
          "capture/cuts/restoration callbacks allocate and delete nothing");
    std::cout << "Storage-rate cap, streaming, timing, metadata and RT checks passed\n";
}
