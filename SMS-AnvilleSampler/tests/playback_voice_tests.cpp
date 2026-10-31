#include "../src/core/PlaybackVoice.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
using midichopper::PlaybackVoice;

void check(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Source {
    midichopper::SampleStoragePool pool{1U};
    midichopper::SampleStoragePool::Storage* storage;
    PlaybackVoice::Parameters parameters;

    Source() {
        midichopper::PadData data;
        data.sampleRate = 1000.0;
        data.frames = 4U;
        data.stereo = {1.0f, -1.0f, 2.0f, -2.0f, 3.0f, -3.0f, 4.0f, -4.0f};
        storage = pool.prepareImport(data, 2000.0, 4U);
        check(storage != nullptr, "prepare immutable source");
        storage->references.store(1U); // The engine owns the storage reference.
        parameters.frames = data.frames;
        parameters.sourceSampleRate = data.sampleRate;
        parameters.sampleRate = 2000.0;
    }

    void render(PlaybackVoice& voice, const float expected) {
        check(voice.playing(), "caller only renders an active voice");
        float left = 8.0f, right = -16.0f;
        const bool active = voice.render(pool, storage, parameters.frames,
                                        parameters.sourceSampleRate, left, right);
        check(active == voice.playing(), "render reports resulting activity");
        check(left == 8.0f + expected && right == -16.0f - expected,
              "interpolated stereo adds to the existing mix");
        check(storage->references.load() == 1U, "DSP leaves storage ownership with the engine");
    }
};

void fractional_loop_and_direction() {
    Source source;
    PlaybackVoice voice;
    voice.start(source.parameters, 127U, PlaybackVoice::Mode::Loop, -1, 7U);
    constexpr std::array expected{1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 2.5f, 1.0f};
    for (const auto sample : expected)
        source.render(voice, sample);
    check(voice.position() == 0.5, "loop retains fractional source phase");
    voice.redirect(source.parameters.frames, true);
    check(voice.reverse() && voice.position() == 2.5,
          "direction turn mirrors the active fractional cursor");
    source.render(voice, 1.5f);
    source.render(voice, 1.0f);
    source.render(voice, 2.5f);
    source.render(voice, 4.0f);
}

void cut_edits_report_engine_actions() {
    Source source;
    PlaybackVoice voice;
    voice.start(source.parameters, 127U, PlaybackVoice::Mode::Hold, 36, 1U);
    source.render(voice, 1.0f);
    source.render(voice, 1.5f);
    source.render(voice, 2.0f);
    source.parameters.playback.start = 0.5f;
    check(voice.refresh(source.parameters), "loop accepts edited Start");
    check(voice.position() == 3.5, "edited loop wraps an earlier cursor fractionally");
    source.render(voice, 3.5f);

    source.parameters.playback = {};
    voice.start(source.parameters, 127U, PlaybackVoice::Mode::OneShot, 36, 2U);
    source.render(voice, 1.0f);
    source.parameters.playback.start = 0.5f;
    check(voice.refresh(source.parameters), "one-shot accepts settings without moving Start");
    source.render(voice, 1.5f);
    source.parameters.playback.start = 0.0f;
    source.parameters.playback.end = 0.25f;
    check(!voice.refresh(source.parameters), "one-shot cut past its cursor asks the engine to stop");
    check(voice.playing(), "refresh leaves ownership cleanup to the engine");
    voice.hardStop();
    check(!voice.playing() && voice.triggerNote() == -1, "engine stop clears MIDI ownership");
}

void lifecycle_preserves_distinct_reset_contracts() {
    Source source;
    PlaybackVoice voice;
    source.parameters.mixer.reverse = 1.0f;
    voice.start(source.parameters, 127U, PlaybackVoice::Mode::Hold, 60, 9U);
    source.render(voice, 4.0f);
    voice.suspend();
    check(!voice.playing() && voice.order() == 9U && voice.triggerNote() == 60 &&
          voice.position() == 0.5 && voice.reverse() && voice.mode() == PlaybackVoice::Mode::Hold,
          "rate suspension preserves DSP and trigger metadata");

    voice.start(source.parameters, 127U, PlaybackVoice::Mode::Hold, 61, 10U);
    source.render(voice, 4.0f);
    voice.reset();
    check(!voice.playing() && voice.order() == 0U && voice.triggerNote() == 61 &&
          voice.position() == 0.5 && voice.reverse() && voice.mode() == PlaybackVoice::Mode::Hold,
          "capture and clear reset activity and age while preserving trigger metadata");

    voice.start(source.parameters, 127U, PlaybackVoice::Mode::Hold, 62, 11U);
    check(voice.release(source.parameters.playback), "zero release requests stop publication");
    check(!voice.playing() && voice.triggerNote() == 62 && voice.order() == 11U,
          "natural release retains trigger ownership and age");
    check(!voice.release(source.parameters.playback), "repeated release requires no publication");
    voice.hardStop();
    check(!voice.playing() && voice.triggerNote() == -1 && voice.order() == 0U,
          "hard stop clears ownership and age");
    check(source.storage->references.load() == 1U, "voice lifecycle never releases engine storage");
}
} // namespace

int main() {
    fractional_loop_and_direction();
    cut_edits_report_engine_actions();
    lifecycle_preserves_distinct_reset_contracts();
    std::cout << "Playback voice tests passed\n";
}
