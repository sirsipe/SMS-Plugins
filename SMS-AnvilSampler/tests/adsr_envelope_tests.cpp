#include "DSP/AdsrEnvelope.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using sms::dsp::AdsrEnvelope;
using sms::dsp::SamplePlaybackSettings;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expectNear(float actual, float expected, const std::string& message)
{
    constexpr float tolerance = 1.0e-6f;
    expect(std::fabs(actual - expected) <= tolerance,
           message + " (expected " + std::to_string(expected) + ", got " +
               std::to_string(actual) + ")");
}

void expectSequence(AdsrEnvelope& envelope, const std::vector<float>& expected,
                    const std::string& context)
{
    for (std::size_t i = 0; i < expected.size(); ++i)
        expectNear(envelope.next(), expected[i], context + " sample " + std::to_string(i));
}

void expectStage(const AdsrEnvelope& envelope, AdsrEnvelope::Stage expected,
                 const std::string& context)
{
    expect(envelope.stage() == expected, context + " stage");
}

void testExactAdsrSequence()
{
    AdsrEnvelope envelope;
    envelope.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.2f, .decaySeconds = 0.2f, .sustainLevel = 0.5f,
        .releaseSeconds = 0.2f});
    envelope.noteOn();
    expectStage(envelope, AdsrEnvelope::Stage::Attack, "noteOn enters attack");
    expectSequence(envelope, {0.0f, 0.5f}, "two-frame attack");
    expectStage(envelope, AdsrEnvelope::Stage::Decay, "attack ends at decay");
    expectNear(envelope.level(), 1.0f, "attack endpoint");
    expectSequence(envelope, {1.0f, 0.75f}, "two-frame decay");
    expectStage(envelope, AdsrEnvelope::Stage::Sustain, "decay ends at sustain");
    expectNear(envelope.level(), 0.5f, "decay endpoint");
    expectSequence(envelope, {0.5f, 0.5f}, "sustain holds level");
    envelope.noteOff();
    expectStage(envelope, AdsrEnvelope::Stage::Release, "noteOff enters release");
    expectSequence(envelope, {0.5f, 0.25f}, "two-frame release");
    expectStage(envelope, AdsrEnvelope::Stage::Idle, "release ends idle");
    expectNear(envelope.level(), 0.0f, "release endpoint");
    expectNear(envelope.next(), 0.0f, "idle output");
}

void testNoteOffDuringAttackAndDecay()
{
    AdsrEnvelope attack;
    attack.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.4f, .sustainLevel = 0.25f, .releaseSeconds = 0.2f});
    attack.noteOn();
    expectNear(attack.next(), 0.0f, "attack first sample before noteOff");
    expectNear(attack.level(), 0.25f, "attack progress before noteOff");
    attack.noteOff();
    expectSequence(attack, {0.25f, 0.125f}, "release starts from attack level");
    expectStage(attack, AdsrEnvelope::Stage::Idle, "attack noteOff release endpoint");

    AdsrEnvelope decay;
    decay.configure(10.0, SamplePlaybackSettings{
        .decaySeconds = 0.4f, .sustainLevel = 0.2f, .releaseSeconds = 0.3f});
    decay.noteOn();
    expectStage(decay, AdsrEnvelope::Stage::Decay, "zero attack enters decay");
    expectNear(decay.next(), 1.0f, "decay first sample before noteOff");
    expectNear(decay.level(), 0.8f, "decay progress before noteOff");
    decay.noteOff();
    expectSequence(decay, {0.8f, 0.53333336f, 0.26666668f},
                   "release starts from decay level");
    expectStage(decay, AdsrEnvelope::Stage::Idle, "decay noteOff release endpoint");
}

void testImmediateNoteOffAndZeroSustain()
{
    AdsrEnvelope beforeFirstSample;
    beforeFirstSample.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.4f, .sustainLevel = 0.5f, .releaseSeconds = 0.2f});
    beforeFirstSample.noteOn();
    beforeFirstSample.noteOff();
    expectStage(beforeFirstSample, AdsrEnvelope::Stage::Idle,
                "noteOff before first attack sample resets zero-level voice");
    expectNear(beforeFirstSample.next(), 0.0f, "noteOff before first sample output");

    AdsrEnvelope zeroSustain;
    zeroSustain.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.1f, .decaySeconds = 0.1f, .sustainLevel = 0.0f,
        .releaseSeconds = 0.3f});
    zeroSustain.noteOn();
    expectSequence(zeroSustain, {0.0f, 1.0f}, "zero-sustain attack and decay");
    expectStage(zeroSustain, AdsrEnvelope::Stage::Sustain, "zero sustain stage");
    expectNear(zeroSustain.level(), 0.0f, "zero sustain level");
    zeroSustain.noteOff();
    expectStage(zeroSustain, AdsrEnvelope::Stage::Idle,
                "noteOff at zero sustain ends immediately");
    expectNear(zeroSustain.next(), 0.0f, "zero-sustain release output");
}

void testZeroAndOneFrameStages()
{
    AdsrEnvelope zero;
    zero.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.0f, .decaySeconds = 0.0f, .sustainLevel = 0.4f,
        .releaseSeconds = 0.0f});
    zero.noteOn();
    expectStage(zero, AdsrEnvelope::Stage::Sustain, "zero attack and decay skip to sustain");
    expectNear(zero.level(), 0.4f, "zero stages sustain endpoint");
    expectNear(zero.next(), 0.4f, "zero stages sustain sample");
    zero.noteOff();
    expectStage(zero, AdsrEnvelope::Stage::Idle, "zero release ends immediately");
    expectNear(zero.next(), 0.0f, "zero release idle sample");

    AdsrEnvelope one;
    one.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.1f, .decaySeconds = 0.1f, .sustainLevel = 0.4f,
        .releaseSeconds = 0.1f});
    one.noteOn();
    expectSequence(one, {0.0f}, "one-frame attack");
    expectStage(one, AdsrEnvelope::Stage::Decay, "one-frame attack endpoint");
    expectNear(one.level(), 1.0f, "one-frame attack reaches peak");
    expectSequence(one, {1.0f}, "one-frame decay");
    expectStage(one, AdsrEnvelope::Stage::Sustain, "one-frame decay endpoint");
    expectNear(one.level(), 0.4f, "one-frame decay reaches sustain");
    one.noteOff();
    expectSequence(one, {0.4f}, "one-frame release");
    expectStage(one, AdsrEnvelope::Stage::Idle, "one-frame release endpoint");
}

void testRepeatedNoteOffAndRetrigger()
{
    AdsrEnvelope envelope;
    envelope.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.2f, .sustainLevel = 0.8f, .releaseSeconds = 0.4f});
    envelope.noteOn();
    static_cast<void>(envelope.next());
    static_cast<void>(envelope.next());
    envelope.noteOff();
    expectNear(envelope.next(), 0.8f, "release sample before repeated noteOff");
    envelope.noteOff();
    expectSequence(envelope, {0.6f, 0.4f, 0.2f}, "repeated noteOff preserves release");
    expectStage(envelope, AdsrEnvelope::Stage::Idle, "repeated noteOff release endpoint");

    envelope.noteOn();
    expectNear(envelope.next(), 0.0f, "retrigger starts from zero");
    expectStage(envelope, AdsrEnvelope::Stage::Attack, "retrigger starts attack");
    expectNear(envelope.level(), 0.5f, "retrigger attack progress");
    envelope.noteOn();
    expectNear(envelope.level(), 0.0f, "second retrigger clears prior level");
    expectNear(envelope.next(), 0.0f, "second retrigger starts at zero");
    expectNear(envelope.level(), 0.5f, "second retrigger restarts attack");
    envelope.noteOff();
    expectNear(envelope.next(), 0.5f, "attack noteOff setup sample");
    expectNear(envelope.level(), 0.375f, "attack noteOff setup release level");
    envelope.noteOn();
    expectNear(envelope.level(), 0.0f, "noteOn during release clears partial level");
    expectSequence(envelope, {0.0f, 0.5f}, "noteOn during release starts fresh attack");
    expectStage(envelope, AdsrEnvelope::Stage::Sustain,
                "release retrigger completes attack and zero decay");

    envelope.noteOff();
    expectNear(envelope.next(), 0.8f, "mid-release retrigger setup sample");
    expectNear(envelope.level(), 0.6f, "mid-release retrigger setup level");
    envelope.noteOn();
    expectStage(envelope, AdsrEnvelope::Stage::Attack, "noteOn during release restarts attack");
    expectNear(envelope.level(), 0.0f, "noteOn during release resets level");
    expectSequence(envelope, {0.0f, 0.5f}, "noteOn during release starts another fresh attack");
    expectStage(envelope, AdsrEnvelope::Stage::Sustain,
                "release retrigger completes attack and zero decay");
}

void testReset()
{
    AdsrEnvelope envelope;
    envelope.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.4f, .sustainLevel = 0.5f, .releaseSeconds = 0.4f});
    envelope.noteOn();
    static_cast<void>(envelope.next());
    envelope.reset();
    expectStage(envelope, AdsrEnvelope::Stage::Idle, "reset stage");
    expect(!envelope.active(), "reset clears active state");
    expectNear(envelope.level(), 0.0f, "reset clears level");
    expectNear(envelope.next(), 0.0f, "reset output remains idle");
    envelope.noteOn();
    expectNear(envelope.next(), 0.0f, "noteOn after reset starts fresh");
}

void testUpdateSettingsDuringStages()
{
    AdsrEnvelope attack;
    attack.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.4f, .sustainLevel = 0.2f});
    attack.noteOn();
    static_cast<void>(attack.next());
    attack.updateSettings(SamplePlaybackSettings{
        .attackSeconds = 0.2f, .sustainLevel = 0.2f});
    expectStage(attack, AdsrEnvelope::Stage::Attack, "attack edit keeps stage");
    expectSequence(attack, {0.25f, 0.625f}, "attack edit retimes from current level");
    expectStage(attack, AdsrEnvelope::Stage::Sustain, "attack edit reaches peak and zero decay");
    expectNear(attack.level(), 0.2f, "attack edit zero-decay endpoint");

    AdsrEnvelope decay;
    decay.configure(10.0, SamplePlaybackSettings{
        .decaySeconds = 0.4f, .sustainLevel = 0.2f});
    decay.noteOn();
    static_cast<void>(decay.next());
    decay.updateSettings(SamplePlaybackSettings{
        .decaySeconds = 0.2f, .sustainLevel = 0.2f});
    expectSequence(decay, {0.8f, 0.5f}, "decay edit retimes from current level");
    expectStage(decay, AdsrEnvelope::Stage::Sustain, "decay edit reaches sustain");

    AdsrEnvelope sustain;
    sustain.configure(10.0, SamplePlaybackSettings{
        .sustainLevel = 0.3f, .releaseSeconds = 0.4f});
    sustain.noteOn();
    sustain.updateSettings(SamplePlaybackSettings{
        .sustainLevel = 0.7f, .releaseSeconds = 0.4f});
    expectStage(sustain, AdsrEnvelope::Stage::Sustain, "sustain edit keeps stage");
    expectNear(sustain.level(), 0.7f, "sustain edit updates held level");
    expectNear(sustain.next(), 0.7f, "sustain edit output");

    AdsrEnvelope release;
    release.configure(10.0, SamplePlaybackSettings{
        .sustainLevel = 0.8f, .releaseSeconds = 0.4f});
    release.noteOn();
    release.noteOff();
    expectNear(release.next(), 0.8f, "release edit setup sample");
    release.updateSettings(SamplePlaybackSettings{
        .sustainLevel = 0.8f, .releaseSeconds = 0.2f});
    expectStage(release, AdsrEnvelope::Stage::Release, "release edit keeps stage");
    expectSequence(release, {0.6f, 0.3f}, "release edit retimes from current level");
    expectStage(release, AdsrEnvelope::Stage::Idle, "release edit reaches idle");

    AdsrEnvelope idle;
    idle.configure(10.0, SamplePlaybackSettings{});
    idle.updateSettings(SamplePlaybackSettings{.attackSeconds = 0.2f});
    expectStage(idle, AdsrEnvelope::Stage::Idle, "idle edit does not start voice");
    idle.noteOn();
    expectStage(idle, AdsrEnvelope::Stage::Attack, "idle edit applies to next note");
    expectSequence(idle, {0.0f, 0.5f}, "idle edit settings used on noteOn");
}

void testZeroDurationEditsAndUnchangedSettings()
{
    AdsrEnvelope attack;
    attack.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = 0.4f, .sustainLevel = 0.3f});
    attack.noteOn();
    expectNear(attack.next(), 0.0f, "zero attack edit setup sample");
    attack.updateSettings(SamplePlaybackSettings{
        .attackSeconds = 0.0f, .sustainLevel = 0.3f});
    expectStage(attack, AdsrEnvelope::Stage::Sustain, "zero attack edit completes stages");
    expectNear(attack.level(), 0.3f, "zero attack edit reaches zero-decay sustain");

    AdsrEnvelope decay;
    decay.configure(10.0, SamplePlaybackSettings{
        .decaySeconds = 0.4f, .sustainLevel = 0.2f});
    decay.noteOn();
    expectNear(decay.next(), 1.0f, "zero decay edit setup sample");
    decay.updateSettings(SamplePlaybackSettings{
        .decaySeconds = 0.0f, .sustainLevel = 0.2f});
    expectStage(decay, AdsrEnvelope::Stage::Sustain, "zero decay edit completes decay");
    expectNear(decay.level(), 0.2f, "zero decay edit reaches sustain immediately");

    AdsrEnvelope release;
    release.configure(10.0, SamplePlaybackSettings{
        .sustainLevel = 0.8f, .releaseSeconds = 0.4f});
    release.noteOn();
    release.noteOff();
    expectNear(release.next(), 0.8f, "zero release edit setup sample");
    release.updateSettings(SamplePlaybackSettings{
        .sustainLevel = 0.8f, .releaseSeconds = 0.0f});
    expectStage(release, AdsrEnvelope::Stage::Idle, "zero release edit ends voice");
    expectNear(release.level(), 0.0f, "zero release edit clears level");

    AdsrEnvelope unchanged;
    const SamplePlaybackSettings original{
        .start = 0.0f, .end = 1.0f, .attackSeconds = 0.4f, .decaySeconds = 0.2f,
        .sustainLevel = 0.5f, .releaseSeconds = 0.3f};
    unchanged.configure(10.0, original);
    unchanged.noteOn();
    expectNear(unchanged.next(), 0.0f, "unchanged-settings setup sample");
    auto regionOnlyEdit = original;
    regionOnlyEdit.start = 0.25f;
    regionOnlyEdit.end = 0.75f;
    unchanged.updateSettings(regionOnlyEdit);
    expectStage(unchanged, AdsrEnvelope::Stage::Attack,
                "region-only edit preserves attack phase");
    expectNear(unchanged.level(), 0.25f, "region-only edit preserves attack level");
    expectNear(unchanged.next(), 0.25f, "region-only edit preserves attack timing");
    unchanged.updateSettings(regionOnlyEdit);
    expectNear(unchanged.level(), 0.5f, "identical settings preserve remaining attack phase");
    expectNear(unchanged.next(), 0.5f, "identical settings preserve attack timing");
}

void testEnvelopeSettingsSanitization()
{
    AdsrEnvelope negative;
    negative.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = -0.5f, .decaySeconds = -1.0f, .sustainLevel = -0.25f,
        .releaseSeconds = -2.0f});
    negative.noteOn();
    expectStage(negative, AdsrEnvelope::Stage::Sustain,
                "negative stage times sanitize to zero");
    expectNear(negative.level(), 0.0f, "negative sustain sanitizes to zero");
    expect(negative.releaseFrames() == 0U, "negative release sanitizes to zero frames");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    AdsrEnvelope nonfinite;
    nonfinite.configure(10.0, SamplePlaybackSettings{
        .attackSeconds = nan, .decaySeconds = infinity, .sustainLevel = nan,
        .releaseSeconds = -infinity});
    nonfinite.noteOn();
    expectStage(nonfinite, AdsrEnvelope::Stage::Sustain,
                "nonfinite stage times use zero-duration fallback");
    expectNear(nonfinite.level(), 1.0f, "nonfinite sustain uses default fallback");
    expect(nonfinite.releaseFrames() == 0U, "nonfinite release uses zero-duration fallback");
}

void testAutomaticReleaseResume()
{
    AdsrEnvelope envelope;
    envelope.configure(10.0, SamplePlaybackSettings{
        .decaySeconds = 0.2f, .sustainLevel = 1.0f, .releaseSeconds = 0.2f});
    envelope.noteOn();
    envelope.noteOff();
    expectNear(envelope.next(), 1.0f, "automatic release before resume");
    expectNear(envelope.level(), 0.5f, "automatic release partial level");
    expect(envelope.releasing(), "automatic release active before resume");
    envelope.resumeAfterAutomaticRelease();
    expectStage(envelope, AdsrEnvelope::Stage::Decay, "resume enters decay");
    expectSequence(envelope, {0.5f, 0.75f}, "resume ramps from current level to sustain");
    expectStage(envelope, AdsrEnvelope::Stage::Sustain, "resume reaches sustain");
    expectNear(envelope.next(), 1.0f, "resumed envelope holds sustain");

    AdsrEnvelope immediate;
    immediate.configure(10.0, SamplePlaybackSettings{
        .decaySeconds = 0.0f, .sustainLevel = 0.6f, .releaseSeconds = 0.2f});
    immediate.noteOn();
    immediate.noteOff();
    static_cast<void>(immediate.next());
    immediate.resumeAfterAutomaticRelease();
    expectStage(immediate, AdsrEnvelope::Stage::Sustain,
                "resume with zero decay reaches sustain immediately");
    expectNear(immediate.level(), 0.6f, "zero-decay resume uses sustain level");
}

void testFrameRoundingAndSampleRateFallback()
{
    AdsrEnvelope envelope;
    envelope.configure(10.0, SamplePlaybackSettings{.releaseSeconds = 0.15f});
    expect(envelope.releaseFrames() == 2U, "half frame rounds up for stage length");
    envelope.configure(1.0, SamplePlaybackSettings{.releaseSeconds = 1.0f / 48000.0f});
    expect(envelope.releaseFrames() == 1U, "sample rates at or below one use 48 kHz fallback");
}

} // namespace

int main()
{
    testExactAdsrSequence();
    testNoteOffDuringAttackAndDecay();
    testImmediateNoteOffAndZeroSustain();
    testZeroAndOneFrameStages();
    testRepeatedNoteOffAndRetrigger();
    testReset();
    testUpdateSettingsDuringStages();
    testZeroDurationEditsAndUnchangedSettings();
    testEnvelopeSettingsSanitization();
    testAutomaticReleaseResume();
    testFrameRoundingAndSampleRateFallback();

    if (failures != 0) {
        std::cerr << failures << " ADSR envelope assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ADSR envelope tests passed\n";
    return EXIT_SUCCESS;
}
