#pragma once

#include "Configuration.hpp"
#include "SampleData.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace midichopper {

/**
 * Owns immutable imported PCM and the physical reserve for live capture.
 * The audio owner acquires/maps/trims/reclaims capture blocks without allocation.
 * Serialized control work prepares/collects imports and reconfigures the pool.
 * Pad publication and logical capacity accounting belong to SamplerEngine.
 */
class SampleStoragePool {
    struct CaptureBlock;
    static constexpr std::uint32_t kMappingPageBlocks = 256U;
    struct MappingPage {
        std::array<CaptureBlock*, kMappingPageBlocks> blocks{};
    };
public:
    static constexpr std::uint32_t kBlockFrames = 1024U;
    struct Storage {
        std::atomic<std::uint32_t> references{0};
        std::uint32_t allocatedBlocks = 0;
        std::uint32_t frames = 0;
        double sampleRate = 48000.0;
        float peak = 0.0f;
        float rms = 0.0f;
        [[nodiscard]] bool isCapture() const noexcept { return mapping != nullptr; }
    private:
        friend class SampleStoragePool;
        std::vector<float> stereo; // immutable imported/staged PCM
        std::unique_ptr<std::atomic<MappingPage*>[]> mapping;
        std::uint32_t mappingPages = 0;
    };

    explicit SampleStoragePool(std::uint32_t logicalBlocks);
    /** A zero logical block limit selects dynamic storage. */
    SampleStoragePool(std::uint32_t logicalBlocks, double sampleRate);
    /** Dynamic capture: 30 seconds initially; 60-second refills at <=15 seconds free. */
    explicit SampleStoragePool(double sampleRate);
    ~SampleStoragePool();
    SampleStoragePool(const SampleStoragePool&) = delete;
    SampleStoragePool& operator=(const SampleStoragePool&) = delete;
    /** Control thread only; processing/readers quiescent, all capture references released. */
    void configure(std::uint32_t logicalBlocks);
    /** Control thread, with processing quiescent; preserves all existing storage. */
    void setCaptureSampleRate(double sampleRate);
    /** Audio owner: accept one prepared chunk without allocating or destroying it. */
    void serviceCapture(bool recording = false) noexcept;
    /** Audio owner: select the host's explicit offline-render profile. */
    void setOffline(bool offline) noexcept { offline_.store(offline, std::memory_order_release); }
    /** Audio owner: permit a new allocation attempt when a new Arm session begins. */
    void retryCaptureAllocation() noexcept;
    [[nodiscard]] std::uint64_t captureCapacityFrames() const noexcept
    { return static_cast<std::uint64_t>(captureBlocks_.load(std::memory_order_acquire)) * kBlockFrames; }
    [[nodiscard]] std::uint64_t freeCaptureFrames() const noexcept
    { return static_cast<std::uint64_t>(publishedFreeBlocks_.load(std::memory_order_acquire)) * kBlockFrames; }
    [[nodiscard]] bool captureAllocationFailed() const noexcept;
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
    [[nodiscard]] float sample(const Storage* storage, std::uint32_t frame,
        std::uint32_t channel) const noexcept;
    /** Stack-local FIR reader. Same ownership requirements as sample(); reuse
     * only while the retained descriptor/mappings stay immutable. Cache one
     * capture block and read channels together instead of reloading its mapping
     * for each channel of each neighboring tap. */
    class StereoReader {
    public:
        StereoReader(const SampleStoragePool& pool, const Storage* storage) noexcept
            : pool_(pool), storage_(storage) {}
        [[nodiscard]] std::array<float, 2> operator()(const std::uint32_t frame) noexcept {
            if (!storage_) return {};
            if (!storage_->isCapture()) {
                if (frame >= storage_->frames) return {};
                const auto offset = static_cast<std::size_t>(frame) * 2U;
                return {storage_->stereo[offset], storage_->stereo[offset + 1U]};
            }
            const auto index = frame / kBlockFrames;
            if (index != blockIndex_) {
                block_ = pool_.mappedBlock(*storage_, index);
                blockIndex_ = index;
            }
            if (!block_) return {};
            const auto offset = (frame % kBlockFrames) * 2U;
            return {block_->stereo[offset], block_->stereo[offset + 1U]};
        }
    private:
        const SampleStoragePool& pool_;
        const Storage* storage_;
        const CaptureBlock* block_ = nullptr;
        std::uint32_t blockIndex_ = UINT32_MAX;
    };

private:
    static constexpr std::size_t kCaptureStorageCount = kPadCount * 2U + 1U;
    struct CaptureBlock {
        CaptureBlock* next = nullptr;
        std::array<float, kBlockFrames * 2U> stereo{};
    };
    struct CaptureChunk {
        std::unique_ptr<CaptureBlock[]> blocks;
        std::uint32_t count = 0;
    };
    enum class Refill : std::uint8_t { idle, requested, preparing, ready, failed };
    void initializeMappings(std::uint32_t maxBlocks);
    void prepareMappings(std::uint32_t blocks);
    CaptureChunk* prepareChunk(std::uint32_t blocks);
    void acceptChunk(CaptureChunk& chunk) noexcept;
    [[nodiscard]] CaptureBlock*& mapping(Storage& storage, std::uint32_t block) noexcept;
    [[nodiscard]] const CaptureBlock* mappedBlock(const Storage& storage,
        std::uint32_t block) const noexcept;
    void requestRefill() noexcept;
    [[nodiscard]] std::uint32_t refillThresholdBlocks() const noexcept;
    void startWorker();
    void stopWorker() noexcept;
    void refillLoop() noexcept;
    std::vector<std::unique_ptr<CaptureChunk>> chunks_; // only allocator/control thread mutates
    std::vector<std::unique_ptr<MappingPage>> mappingOwners_;
    std::array<Storage, kCaptureStorageCount> capture_;
    std::vector<std::unique_ptr<Storage>> imported_;
    CaptureBlock* freeBlocks_ = nullptr; // audio owner
    std::uint32_t freeBlockCount_ = 0;
    std::uint32_t maxCaptureBlocks_ = 0;
    std::uint32_t preparedMappingPages_ = 0; // allocator/control thread
    std::uint32_t refillBlocks_ = 0;
    std::uint32_t thresholdBlocks_ = 0;
    std::uint32_t offlineRefillBlocks_ = 0;
    std::uint32_t offlineThresholdBlocks_ = 0;
    std::atomic<bool> offline_{false};
    bool dynamic_ = false;
    std::atomic<std::uint32_t> captureBlocks_{0};
    std::atomic<std::uint32_t> publishedFreeBlocks_{0};
    std::atomic<Refill> refill_{Refill::idle};
    CaptureChunk* preparedChunk_ = nullptr; // published by refill_=ready
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

} // namespace midichopper
