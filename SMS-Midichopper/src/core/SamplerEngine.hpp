#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace midichopper {

inline constexpr std::uint32_t kPadCount = 16;

enum class CaptureMode : std::uint8_t { Sequential, FixedDuration };
// Kept as a source-compatible alias for early clients of the core.
using RecordMode = CaptureMode;
enum class PlaybackMode : std::uint8_t { OneShot, Gated };
enum class MidiEventType : std::uint8_t { NoteOff, NoteOn };

/** A timestamp is an offset into the current process block (zero based). */
struct MidiEvent {
    union { std::uint32_t frameOffset; std::uint32_t frame_offset; };
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    MidiEventType type = MidiEventType::NoteOff;

    constexpr MidiEvent() noexcept : frameOffset(0) {}
    constexpr MidiEvent(std::uint32_t offset, std::uint8_t n,
                        std::uint8_t v, MidiEventType t) noexcept
        : frameOffset(offset), note(n), velocity(v), type(t) {}
    [[nodiscard]] constexpr bool isNoteOn() const noexcept { return type == MidiEventType::NoteOn && velocity != 0; }
};

struct EngineSettings {
    bool armed = false;
    CaptureMode captureMode = CaptureMode::Sequential;
    PlaybackMode playbackMode = PlaybackMode::OneShot;
    double fixedLengthSeconds = 1.0;
    bool monitorInput = true;
    std::uint8_t baseNote = 36;
    std::uint8_t startPad = 0;
    float preRollMilliseconds = 0.0f;
    float gain = 1.0f;
};

/** A non-real-time copy of one pad, suitable for project state and UI work. */
struct PadData {
    double sampleRate = 48000.0;
    std::uint32_t frames = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    std::vector<float> stereo;
};

struct PadMetadata {
    double sampleRate = 48000.0;
    std::uint32_t frames = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    std::uint64_t generation = 0;
    bool occupied = false;
    bool recording = false;
    bool active = false;
};

/**
 * Allocation-free/lock-free audio engine. Configuration and pad state import
 * are control-thread operations and must not be called concurrently with
 * process(). Events passed to process() must be sorted by frameOffset.
 *
 * Audio is stereo, non-interleaved. A pad is note baseNote + [0, 15].
 * When armed in Sequential mode, the first note-on starts a slice at startPad,
 * and each later note-on commits the current slice and advances one pad.
 * finalizeRecording() commits the final slice. In FixedDuration mode each
 * note-on starts the next slice and it ends at the configured length. In either
 * mode a full buffer also ends recording. Samples are preallocated by the
 * constructor and never allocated from process().
 */
class SamplerEngine {
public:
    explicit SamplerEngine(double sampleRate = 48000.0,
                           double maxRecordSeconds = 30.0);
    ~SamplerEngine() = default;
    SamplerEngine(const SamplerEngine&) = delete;
    SamplerEngine& operator=(const SamplerEngine&) = delete;

    void setSampleRate(double sampleRate); // control thread, reallocates if needed
    [[nodiscard]] double sampleRate() const noexcept { return sample_rate_; }
    [[nodiscard]] double maxRecordSeconds() const noexcept { return max_record_seconds_; }
    void reset() noexcept;
    void setSettings(const EngineSettings& settings) noexcept;
    [[nodiscard]] EngineSettings settings() const noexcept { return settings_; }

    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight,
                 std::uint32_t frames,
                 const MidiEvent* events = nullptr,
                 std::uint32_t eventCount = 0) noexcept;
    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight,
                 std::uint32_t frames,
                 std::span<const MidiEvent> events) noexcept;

    [[nodiscard]] PadMetadata padMetadata(std::uint32_t pad) const noexcept;
    /** Copy a stable, already-published pad snapshot on the control/UI thread. */
    [[nodiscard]] bool exportPad(std::uint32_t pad, PadData& destination) const;
    /** Import/replaces a pad on the control thread; stereo must be interleaved. */
    [[nodiscard]] bool importPad(std::uint32_t pad, const PadData& source);
    void clearPad(std::uint32_t pad) noexcept;
    void clearAllPads() noexcept;
    void finalizeRecording() noexcept;
    void undoLastSlice() noexcept;

private:
    struct Pad {
        std::uint32_t recordedFrames = 0;
        std::uint32_t recordPosition = 0;
        bool recording = false;
        bool held = false;
        bool playing = false;
        double playPosition = 0.0;
        float velocityGain = 1.0f;
        float recordPeak = 0.0f;
        double recordSumSquares = 0.0;
        std::atomic<double> sourceSampleRate{48000.0};
        std::atomic<std::uint32_t> publishedFrames{0};
        std::atomic<float> publishedPeak{0.0f};
        std::atomic<float> publishedRms{0.0f};
        std::atomic<std::uint64_t> generation{0};
        std::atomic<bool> occupied{false};
        std::atomic<bool> publishedRecording{false};
        std::atomic<bool> publishedPlaying{false};
        Pad() = default;
        Pad(const Pad&) = delete;
        Pad& operator=(const Pad&) = delete;
    };
    std::uint32_t noteToPad(std::uint8_t note) const noexcept;
    void beginRecord(std::uint32_t pad) noexcept;
    void finishRecord(std::uint32_t pad, std::uint32_t trimFrames = 0) noexcept;
    void handleEvent(const MidiEvent& event) noexcept;
    void writeRecordFrame(std::uint32_t frame, float left, float right) noexcept;
    void startVoice(std::uint32_t pad, std::uint8_t velocity) noexcept;
    void stopVoice(std::uint32_t pad) noexcept;

    double sample_rate_;
    double max_record_seconds_;
    std::uint32_t max_frames_;
    EngineSettings settings_{};
    std::vector<float> samples_;
    std::array<Pad, kPadCount> pads_{};
    std::uint32_t ringCapacityFrames_ = 0;
    std::vector<float> ring_;
    std::uint32_t ringWritePosition_ = 0;
    std::uint32_t ringCount_ = 0;
    std::uint32_t preRollFrames_ = 0;
    std::int32_t activePad_ = -1;
    std::int32_t lastCommittedPad_ = -1;
    std::uint32_t nextPad_ = 0;
    bool previousArmed_ = false;
    bool sessionComplete_ = false;
    std::atomic<bool> finalizeRequested_{false};
    std::atomic<bool> undoRequested_{false};
};

} // namespace midichopper
