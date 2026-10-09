#pragma once

#include "Configuration.hpp"
#include "SampleData.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace midichopper {

/**
 * Owns immutable imported PCM and the physical reserve for live capture.
 * The audio owner acquires/maps/trims/reclaims capture blocks without allocation.
 * Serialized control work prepares/collects imports and reconfigures the pool.
 * Pad publication and logical capacity accounting belong to SamplerEngine.
 */
class SampleStoragePool {
public:
    static constexpr std::uint32_t kBlockFrames = 1024U;
    struct Storage {
        std::atomic<std::uint32_t> references{0};
        std::uint32_t allocatedBlocks = 0;
        std::uint32_t frames = 0;
        double sampleRate = 48000.0;
        float peak = 0.0f;
        float rms = 0.0f;
        [[nodiscard]] bool isCapture() const noexcept { return !blocks.empty(); }
    private:
        friend class SampleStoragePool;
        std::vector<float> stereo; // immutable imported/staged PCM
        std::vector<std::uint32_t> blocks; // preallocated capture mapping
    };

    explicit SampleStoragePool(std::uint32_t logicalBlocks);
    SampleStoragePool(const SampleStoragePool&) = delete;
    SampleStoragePool& operator=(const SampleStoragePool&) = delete;
    /** Control thread only; processing/readers quiescent, all capture references released. */
    void configure(std::uint32_t logicalBlocks);
    /** Control thread only; result starts unreferenced until staged/published. */
    [[nodiscard]] Storage* prepareImport(const PadData& source,
        double hostSampleRate, std::uint32_t maxFrames);
    void collectImported();

    /** Audio owner only. Acquiring a descriptor pins it with one reference. */
    [[nodiscard]] Storage* acquireCapture() noexcept;
    [[nodiscard]] bool hasCaptureBlock(const Storage& storage, std::uint32_t block) const noexcept;
    [[nodiscard]] bool ensureCaptureBlock(Storage& storage, std::uint32_t block) noexcept;
    /** Return trimmed block count for the engine's logical accounting. Recording storage only. */
    [[nodiscard]] std::uint32_t trimCapture(Storage& storage, std::uint32_t keepBlocks) noexcept;
    void reclaimCapture(std::uint32_t blockBudget) noexcept;
    /** Storage must have a mapping for this frame (ensureCaptureBlock first). */
    void writeCapture(Storage& storage, std::uint32_t frame, float left, float right) noexcept;
    /** Control reader retains completed storage; destination holds at most frames * 2 samples. */
    void copySamples(const Storage* storage, std::span<float> destination) const;
    /** Audio owner or a reader retaining a completed immutable descriptor. */
    /** Audio owner or a reader retaining a completed immutable descriptor. */
    [[nodiscard]] float sample(const Storage* storage, std::uint32_t frame,
        std::uint32_t channel) const noexcept;

private:
    static constexpr std::uint32_t kNoBlock = ~std::uint32_t{0};
    static constexpr std::size_t kCaptureStorageCount = kPadCount * 2U + 1U;
    std::vector<float> samples_;
    std::array<Storage, kCaptureStorageCount> capture_;
    std::vector<std::unique_ptr<Storage>> imported_;
    std::vector<std::uint32_t> freeBlocks_;
    std::uint32_t freeBlockCount_ = 0;
};

} // namespace midichopper
