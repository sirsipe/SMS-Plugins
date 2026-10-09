#pragma once

#include "Configuration.hpp"
#include "Audio/WaveformSummary.hpp"
#include "DSP/AdsrEnvelope.hpp"
#include "DSP/ColorEffects.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace midichopper {

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

/** Pull sorted events without imposing a fixed per-block event limit. */
struct MidiEventSource {
    void* context = nullptr;
    bool (*next)(void*, MidiEvent&) noexcept = nullptr;
};

struct EngineSettings {
    bool armed = false;
    CaptureMode captureMode = CaptureMode::Sequential;
    PlaybackMode playbackMode = PlaybackMode::OneShot;
    double fixedLengthSeconds = 1.0;
    bool monitorInput = true;
    std::uint8_t baseNote = kDefaultBaseMidiNote;
    std::uint8_t startPad = 0;
    std::uint8_t activeBank = 0;
    MidiBankMode midiBankMode = kDefaultMidiBankMode;
    std::uint8_t padsPerBank = static_cast<std::uint8_t>(kPadsPerBank);
    float preRollMilliseconds = 0.0f;
    float gain = 1.0f; // Pad playback and raw previews only.
    float monitorGain = 1.0f;
    float pan = 0.0f;
    float tuneSemitones = 0.0f;
    float lowpass = 0.0f;
    float highpass = 0.0f;
    float filterSlope = 0.0f;
    float dirty = 0.0f;
    std::uint8_t maxVoices = 1U;
};

/** A non-real-time copy of one pad, suitable for project state and UI work. */
struct PadData {
    double sampleRate = 48000.0;
    std::uint32_t frames = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    std::vector<float> stereo;
    std::uint64_t generation = 0; // transient snapshot revision; not serialized
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

struct PlaybackTrigger {
    std::uint32_t pad = kPadCount;
    std::uint64_t generation = 0;
};

struct ChopMidiPreview {
    bool active = false;
    std::uint32_t firstPad = 0;
    std::uint32_t sourcePadCount = 0;
    std::uint32_t previewPadCount = 0;
    std::array<std::uint64_t, 3> sourceFrames{};
    std::array<std::uint64_t, 3> sourceEndFrames{};
};

/**
 * Allocation-free/lock-free audio engine. Configuration and pad state import
 * are control-thread operations. With an installed dispatcher, transfers may
 * overlap process(); configuration requires host quiescence. Events passed
 * to process() must be sorted by frameOffset.
 *
 * Audio is stereo, non-interleaved. MIDI notes either address the visible pads
 * in the active bank or a gapless sequence across all four visible bank pages,
 * starting at baseNote. Selected Bank retains fixed 16-slot storage banks;
 * All Banks groups sequential sample slots by the selected layout size.
 * When armed in Sequential mode, the first note-on starts a slice at startPad,
 * and each later note-on commits the current slice and advances one visible
 * pad, continuing into the next bank when needed.
 * finalizeRecording() commits the final slice. In FixedDuration mode each
 * note-on starts the next slice and it ends at the configured length. In either
 * mode a full buffer or exhausted shared storage also ends recording. A pad
 * may use the entire shared pool when other pads are empty. Sample blocks are
 * preallocated by the constructor and never allocated from process().
 */
class SamplerEngine {
public:
    explicit SamplerEngine(double sampleRate = 48000.0,
                           double maxRecordSeconds = 30.0);
    ~SamplerEngine() = default;
    /** Install before processing; the adapter dispatches only bounded capture/commit work. */
    void setControlDispatcher(void* context,
        void (*dispatch)(void*, void (*)(void*) noexcept, void*)) noexcept
    { controlContext_ = context; controlDispatch_ = dispatch; }
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
    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight,
                 std::uint32_t frames, MidiEventSource events) noexcept;

    [[nodiscard]] PadMetadata padMetadata(std::uint32_t pad) const noexcept;
    /** Most recent successful MIDI playback trigger; read from the audio thread. */
    [[nodiscard]] PlaybackTrigger lastPlaybackTrigger() const noexcept
    {
        return lastPlaybackTrigger_;
    }
    /**
     * Current recording pad, or the next destination while armed and idle.
     * This is an audio-thread observation; kPadCount means there is no target.
     */
    [[nodiscard]] std::uint32_t captureTargetPad() const noexcept;
    /** Explicitly choose an idle armed capture destination in the active bank. */
    void selectCaptureTarget(std::uint32_t pad) noexcept;
    /** Copy a stable, already-published pad snapshot on the control/UI thread. */
    [[nodiscard]] bool exportPad(std::uint32_t pad, PadData& destination,
        sms::dsp::SamplePlaybackSettings* playback = nullptr,
        sms::dsp::SampleMixerSettings* mixer = nullptr) const;
    /** Host state reads use published revisions and do not require host processing. */
    [[nodiscard]] bool exportPublishedPad(std::uint32_t pad, PadData& destination,
        sms::dsp::SamplePlaybackSettings* playback = nullptr,
        sms::dsp::SampleMixerSettings* mixer = nullptr) const;
    void snapshotPadSettings(std::uint32_t pad, sms::dsp::SamplePlaybackSettings& playback,
        sms::dsp::SampleMixerSettings& mixer) const;
    /** Summarize retained adjacent-pad storage on the control thread while audio continues. */
    [[nodiscard]] bool summarizePadRange(std::uint32_t firstPad, std::uint32_t padCount,
                                         std::uint64_t start, std::uint64_t end,
                                         sms::audio::WaveformSummary& result) const;
    /** Capture a coherent ordinary-editor baseline and summarize retained PCM off-thread. */
    [[nodiscard]] bool getChopSnapshot(std::uint32_t firstPad, std::uint32_t padCount,
        std::span<sms::audio::WaveformSummary> summaries,
        std::span<std::uint64_t> generations) const;
    /** Import/replaces a pad; state restoration may opt out of the normal editor reset. */
    [[nodiscard]] bool importPad(std::uint32_t pad, const PadData& source,
                                 bool resetEditorSettings = true,
        const sms::dsp::SamplePlaybackSettings* playback = nullptr,
        const sms::dsp::SampleMixerSettings* mixer = nullptr,
        std::optional<std::uint64_t> expectedGeneration = {});
    /** Durable state may arrive while an active host has suspended callbacks. */
    [[nodiscard]] bool restorePad(std::uint32_t pad, const PadData* audio);
    void restorePadPlaybackSettings(std::uint32_t pad,
        const sms::dsp::SamplePlaybackSettings& settings);
    void restorePadMixerSettings(std::uint32_t pad,
        const sms::dsp::SampleMixerSettings& settings);
    /** Audio-owner: publish staged durable state at the next block; no allocation/free. */
    void applyPendingState() noexcept;
    [[nodiscard]] bool stateRestoreFailed() const noexcept
    { return stateRestoreFailed_.load(std::memory_order_acquire); }
    /**
     * Repartition raw audio across consecutive pad slots. Empty slots may
     * receive audio, and a zero-length result clears that pad. Boundary offsets
     * are frame deltas from the original boundaries. Control thread only.
     */
    [[nodiscard]] bool rechopPads(std::uint32_t firstPad, std::uint32_t padCount,
                                  std::span<const std::int64_t> boundaryOffsets,
                                  std::span<const std::uint64_t> expectedGenerations = {});
    /**
     * Close the empty run containing targetPad by moving the immediately
     * following occupied run left. Complete pad contents and settings move
     * together. The supplied range is one visible bank/page. Control thread
     * only; no pad changes when validation fails.
     */
    [[nodiscard]] bool collapsePadGap(std::uint32_t firstPad,
                                      std::uint32_t padCount,
                                      std::uint32_t targetPad);
    /**
     * Split firstPad at splitFrame and insert the suffix into firstPad + 1.
     * Occupied pads through emptyPad shift right by one, preserving their
     * complete contents and settings. expectedGenerations covers the inclusive
     * [firstPad, emptyPad] range and rejects stale UI plans. Control thread only.
     */
    [[nodiscard]] bool splitPadAndShiftRight(
        std::uint32_t firstPad, std::uint32_t emptyPad,
        std::uint32_t splitFrame,
        std::span<const std::uint64_t> expectedGenerations);
    /** Allocation-free raw preview used by the Cut Point Editor. Audio thread only. */
    void startChopPreview(std::uint32_t firstPad, std::uint32_t padCount,
                          std::uint64_t sourceFrame, std::uint64_t sourceEndFrame) noexcept;
    void stopChopPreview() noexcept;
    /** Route MIDI for the visible Cut Point Editor pads to proposed raw slices. */
    void setChopMidiPreview(const ChopMidiPreview& preview) noexcept;
    [[nodiscard]] bool chopMidiPreviewActive() const noexcept
    {
        return chopMidiPreview_.active;
    }
    /** Zero when stopped; otherwise global pad + 1 plus normalized position. */
    [[nodiscard]] float chopPreviewPosition() const noexcept;
    /** Zero when stopped; otherwise latest triggered pad + 1 plus source position. */
    [[nodiscard]] float playbackPosition() const noexcept;
    /** Hard-cut every pad voice and raw preview at the next audio block boundary. */
    void stopAllPlayback() noexcept;
    [[nodiscard]] bool anyPlaybackActive() const noexcept;
    /** UI transport action: cut active voices, otherwise audition this pad. */
    void togglePlayback(std::uint32_t selectedPad) noexcept;
    [[nodiscard]] sms::dsp::SamplePlaybackSettings padPlaybackSettings(std::uint32_t pad) const noexcept;
    void setPadPlaybackSettings(std::uint32_t pad,
                                const sms::dsp::SamplePlaybackSettings& settings);
    [[nodiscard]] sms::dsp::SampleMixerSettings padMixerSettings(std::uint32_t pad) const noexcept;
    void setPadMixerSettings(std::uint32_t pad,
                             const sms::dsp::SampleMixerSettings& settings);
    /** Audio-owner operations; control callers must dispatch or quiesce processing. */
    void clearPad(std::uint32_t pad) noexcept;
    void clearAllPads() noexcept;
    void finalizeRecording() noexcept;
    void undoLastSlice() noexcept;

private:
    // Control owns imported allocations. Audio owns capture blocks and runtime state.
    // Retaining a completed descriptor pins both its mapping and PCM without copying.
    struct Storage {
        std::atomic<std::uint32_t> references{0};
        std::vector<float> stereo; // immutable imported/staged PCM
        std::vector<std::uint32_t> blocks; // preallocated capture mapping
        std::uint32_t allocatedBlocks = 0;
        std::uint32_t frames = 0;
        double sampleRate = 48000.0;
        float peak = 0.0f;
        float rms = 0.0f;
    };
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<Storage*>::is_always_lock_free);
    struct Snapshot {
        Storage* storage = nullptr;
        PadMetadata metadata;
        sms::dsp::SamplePlaybackSettings playback;
        sms::dsp::SampleMixerSettings mixer;
        Snapshot() = default;
        Snapshot(const Snapshot&) = delete;
        Snapshot& operator=(const Snapshot&) = delete;
        ~Snapshot() { if (storage) storage->references.fetch_sub(1, std::memory_order_release); }
    };
    template<class Callback> void dispatchControl(Callback&& callback) const
    {
        static_assert(std::is_nothrow_invocable_v<Callback&>);
        auto bounded = [&]() noexcept {
            const_cast<SamplerEngine*>(this)->applyPendingState();
            callback();
        };
        if (controlDispatch_)
            controlDispatch_(controlContext_, [](void* opaque) noexcept {
                (*static_cast<decltype(bounded)*>(opaque))();
            }, &bounded);
        else
            bounded();
    }
    struct PendingState {
        std::atomic<bool> consumed{false};
        Storage* storage = nullptr;
        bool replaceAudio = false;
        bool replacePlayback = false;
        bool replaceMixer = false;
        sms::dsp::SamplePlaybackSettings playback{};
        sms::dsp::SampleMixerSettings mixer{};
    };
    std::unique_ptr<PendingState> preparePendingState();
    void enqueuePendingState(std::uint32_t pad, std::unique_ptr<PendingState> state);
    void collectPendingState();
    void overlayPendingState(std::uint32_t pad, Snapshot& snapshot) const noexcept;
    void captureSnapshot(std::uint32_t pad, Snapshot& snapshot) const noexcept;
    [[nodiscard]] float snapshotSample(const Snapshot& snapshot, std::uint32_t frame,
                                       std::uint32_t channel) const noexcept;
    void collectImportedStorage();
    void reclaimCaptureStorage(std::uint32_t budget) noexcept;
    [[nodiscard]] Storage* prepareStorage(const PadData& source);
    void publishStorage(std::uint32_t pad, Storage* storage, bool resetSettings) noexcept;
    void setPadPlaybackSettingsDirect(std::uint32_t pad,
        const sms::dsp::SamplePlaybackSettings& settings) noexcept;
    void setPadMixerSettingsDirect(std::uint32_t pad,
        const sms::dsp::SampleMixerSettings& settings) noexcept;
    struct Pad {
        Storage* storage = nullptr;
        std::atomic<Storage*> publishedStorage{nullptr};
        std::atomic<std::uint64_t> publicationSequence{0};
        std::uint32_t publicationDepth = 0; // only the state owner writes this
        std::uint32_t recordedFrames = 0;
        std::uint32_t recordPosition = 0;
        std::uint32_t allocatedBlocks = 0;
        bool recording = false;
        bool held = false;
        bool playing = false;
        std::uint64_t voiceOrder = 0;
        double playPosition = 0.0;
        double playStep = 1.0;
        std::uint32_t voiceStartFrame = 0;
        std::uint32_t voiceEndFrame = 0;
        bool releaseFromEnd = false;
        float velocityGain = 1.0f;
        float mixerGainLeft = 1.0f;
        float mixerGainRight = 1.0f;
        bool dirtyPlayback = false;
        sms::dsp::ColorFilters padFilters;
        sms::dsp::ColorFilters globalFilters;
        sms::dsp::AdsrEnvelope envelope;
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
        std::atomic<float> regionStart{0.0f};
        std::atomic<float> regionEnd{1.0f};
        std::atomic<float> attackSeconds{0.0f};
        std::atomic<float> decaySeconds{0.0f};
        std::atomic<float> sustainLevel{1.0f};
        std::atomic<float> releaseSeconds{0.0f};
        std::atomic<float> mixerGainDecibels{0.0f};
        std::atomic<float> mixerPan{0.0f};
        std::atomic<float> mixerTuneSemitones{0.0f};
        std::atomic<float> mixerLowpass{0.0f};
        std::atomic<float> mixerHighpass{0.0f};
        std::atomic<float> mixerFilterSlope{0.0f};
        std::atomic<float> mixerDirty{0.0f};
        Pad() = default;
        Pad(const Pad&) = delete;
        Pad& operator=(const Pad&) = delete;
    };
    struct PadMutation {
        Pad& pad;
        explicit PadMutation(Pad& value) noexcept : pad(value) {
            if (pad.publicationDepth++ == 0U)
                pad.publicationSequence.fetch_add(1U, std::memory_order_acq_rel);
        }
        ~PadMutation() { finish(pad); }
        static void finish(Pad& pad) noexcept {
            if (--pad.publicationDepth == 0U) {
                pad.publishedStorage.store(pad.storage, std::memory_order_release);
                pad.publicationSequence.fetch_add(1U, std::memory_order_release);
            }
        }
        static void begin(Pad& pad) noexcept {
            if (pad.publicationDepth++ == 0U)
                pad.publicationSequence.fetch_add(1U, std::memory_order_acq_rel);
        }
    };
    void capturePublishedSnapshot(std::uint32_t pad, Snapshot& snapshot) const;
    void copySnapshot(const Snapshot& snapshot, PadData& destination,
        sms::dsp::SamplePlaybackSettings* playback,
        sms::dsp::SampleMixerSettings* mixer) const;
    std::uint32_t noteToPad(std::uint8_t note) const noexcept;
    [[nodiscard]] std::uint32_t firstCapturePad() const noexcept;
    [[nodiscard]] std::uint32_t firstAvailableCapturePad() const noexcept;
    [[nodiscard]] std::uint32_t followingCapturePad(std::uint32_t pad) const noexcept;
    void resetCaptureTarget(bool preferEmpty) noexcept;
    [[nodiscard]] bool ensurePadBlock(std::uint32_t pad, std::uint32_t block) noexcept;
    void releasePadBlocks(std::uint32_t pad) noexcept;
    void trimPadBlocks(std::uint32_t pad, std::uint32_t frames) noexcept;
    [[nodiscard]] float sampleAt(std::uint32_t pad, std::uint32_t frame,
                                 std::uint32_t channel) const noexcept;
    [[nodiscard]] bool storeSample(std::uint32_t pad, std::uint32_t frame,
                                   float left, float right) noexcept;
    void movePadContents(std::uint32_t source, std::uint32_t destination) noexcept;
    void beginRecord(std::uint32_t pad) noexcept;
    void finishRecord(std::uint32_t pad, std::uint32_t trimFrames = 0) noexcept;
    void handleEvent(const MidiEvent& event) noexcept;
    void writeRecordFrame(std::uint32_t frame, float left, float right) noexcept;
    void startVoice(std::uint32_t pad, std::uint8_t velocity) noexcept;
    void stopVoice(std::uint32_t pad) noexcept;
    void hardStopVoice(std::uint32_t pad) noexcept;
    void refreshActiveVoiceSettings(std::uint32_t pad) noexcept;
    void updatePlaybackPositionOutput(std::uint32_t processedFrames) noexcept;
    void enforceVoiceLimit(std::uint32_t excludedPad = kPadCount) noexcept;
    void mixChopPreview(float& left, float& right) noexcept;

    double sample_rate_;
    double max_record_seconds_;
    std::uint32_t total_blocks_;
    std::uint32_t max_frames_;
    EngineSettings settings_{};
    std::vector<float> samples_;
    static constexpr std::size_t kCaptureStorageCount = kPadCount * 2U + 1U;
    std::array<Storage, kCaptureStorageCount> captureStorage_;
    std::vector<std::unique_ptr<Storage>> importedStorage_;
    std::array<std::atomic<PendingState*>, kPadCount> pendingState_{};
    std::vector<std::unique_ptr<PendingState>> pendingStateOwners_;
    std::atomic<bool> stateRestoreFailed_{false};
    mutable std::recursive_mutex storageControlMutex_;
    void* controlContext_ = nullptr;
    void (*controlDispatch_)(void*, void (*)(void*) noexcept, void*) = nullptr;
    std::uint32_t used_blocks_ = 0; // active pad capacity; retained readers use reserve blocks
    std::vector<std::uint32_t> free_blocks_;
    std::uint32_t free_block_count_ = 0;
    std::array<Pad, kPadCount> pads_{};
    std::uint32_t ringCapacityFrames_ = 0;
    std::vector<float> ring_;
    std::uint32_t ringWritePosition_ = 0;
    std::uint32_t ringCount_ = 0;
    std::uint32_t preRollFrames_ = 0;
    std::int32_t activePad_ = -1;
    std::int32_t lastCommittedPad_ = -1;
    std::uint32_t nextPad_ = 0;
    std::uint8_t capturePadsPerBank_ = static_cast<std::uint8_t>(kPadsPerBank);
    MidiBankMode captureMidiBankMode_ = kDefaultMidiBankMode;
    std::uint64_t nextVoiceOrder_ = 1;
    PlaybackTrigger lastPlaybackTrigger_{};
    std::uint64_t playbackPositionGeneration_ = 0;
    std::uint32_t playbackPositionHoldFrames_ = 0;
    float playbackPositionOutput_ = 0.0f;
    bool playbackPositionWasActive_ = false;
    bool previousArmed_ = false;
    bool sessionComplete_ = false;
    ChopMidiPreview chopMidiPreview_{};
    std::atomic<bool> finalizeRequested_{false};
    std::atomic<bool> undoRequested_{false};
    bool chopPreviewActive_ = false;
    std::uint32_t chopPreviewFirstPad_ = 0;
    std::uint32_t chopPreviewPadCount_ = 0;
    std::uint32_t chopPreviewPad_ = 0;
    double chopPreviewFrame_ = 0.0;
    double chopPreviewRemainingFrames_ = 0.0;
};

} // namespace midichopper
