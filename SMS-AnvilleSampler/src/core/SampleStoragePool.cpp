#include "SampleStoragePool.hpp"
#include "DSP/SincResampler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace midichopper {

SampleStoragePool::SampleStoragePool(const std::uint32_t logicalBlocks)
    : SampleStoragePool(logicalBlocks, 48000.0) {}

SampleStoragePool::SampleStoragePool(const double sampleRate)
    : SampleStoragePool(0U, sampleRate) {}

SampleStoragePool::SampleStoragePool(const std::uint32_t logicalBlocks, const double sampleRate) {
    if (logicalBlocks != 0U) {
        configure(logicalBlocks);
    } else {
        dynamic_ = true;
        initializeMappings((kMaximumPadFrames + kBlockFrames - 1U) / kBlockFrames);
        setCaptureSampleRate(sampleRate);
    }
}

SampleStoragePool::~SampleStoragePool() { stopWorker(); }

void SampleStoragePool::stopWorker() noexcept {
    stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
}

void SampleStoragePool::startWorker() {
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { refillLoop(); });
}

void SampleStoragePool::initializeMappings(const std::uint32_t maxBlocks) {
    maxCaptureBlocks_ = maxBlocks;
    const auto pages = (maxBlocks + kMappingPageBlocks - 1U) / kMappingPageBlocks;
    for (auto& storage : capture_) {
        storage.references.store(0U, std::memory_order_relaxed);
        storage.allocatedBlocks = 0U;
        storage.mapping = std::make_unique<std::atomic<MappingPage*>[]>(pages);
        storage.mappingPages = pages;
        for (std::uint32_t page = 0; page < pages; ++page)
            storage.mapping[page].store(nullptr, std::memory_order_relaxed);
    }
    preparedMappingPages_ = 0U;
}

void SampleStoragePool::prepareMappings(const std::uint32_t blocks) {
    const auto pages = (std::min(blocks, maxCaptureBlocks_) + kMappingPageBlocks - 1U) /
                       kMappingPageBlocks;
    for (; preparedMappingPages_ < pages; ++preparedMappingPages_) {
        for (auto& storage : capture_) {
            // A failed attempt can leave some valid, already-published pages.
            if (storage.mapping[preparedMappingPages_].load(std::memory_order_acquire)) continue;
            auto page = std::make_unique<MappingPage>();
            auto* pointer = page.get();
            mappingOwners_.push_back(std::move(page));
            storage.mapping[preparedMappingPages_].store(pointer, std::memory_order_release);
        }
    }
}

SampleStoragePool::CaptureChunk* SampleStoragePool::prepareChunk(const std::uint32_t blocks) {
    const auto previous = captureBlocks_.load(std::memory_order_relaxed);
    if (blocks == 0U || blocks > std::numeric_limits<std::uint32_t>::max() - previous)
        throw std::length_error("Capture storage capacity exhausted");
    auto chunk = std::make_unique<CaptureChunk>();
    chunk->blocks = std::make_unique<CaptureBlock[]>(blocks);
    chunk->count = blocks;
    // Write every page before publication, including when the allocator supplied
    // lazily zeroed memory. This work must never first occur on the audio thread.
    auto* bytes = reinterpret_cast<volatile unsigned char*>(chunk->blocks.get());
    for (std::size_t offset = 0; offset < sizeof(CaptureBlock) * blocks; offset += 4096U)
        bytes[offset] = 0;
    for (std::uint32_t block = 0; block + 1U < blocks; ++block)
        chunk->blocks[block].next = &chunk->blocks[block + 1U];
    prepareMappings(previous + blocks);
    auto* result = chunk.get();
    chunks_.push_back(std::move(chunk));
    captureBlocks_.store(previous + blocks, std::memory_order_release);
    return result;
}

void SampleStoragePool::acceptChunk(CaptureChunk& chunk) noexcept {
    chunk.blocks[chunk.count - 1U].next = freeBlocks_;
    freeBlocks_ = chunk.blocks.get();
    freeBlockCount_ += chunk.count;
    publishedFreeBlocks_.store(freeBlockCount_, std::memory_order_release);
}

void SampleStoragePool::setCaptureSampleRate(const double sampleRate) {
    if (!dynamic_) return;
    stopWorker();
    serviceCapture();
    const auto blocksForSeconds = [sampleRate](const double seconds) {
        const double frames = std::ceil(sampleRate * seconds);
        if (!std::isfinite(frames) || frames < 1.0 || frames > kMaximumPadFrames)
            throw std::invalid_argument("Unsupported capture sample rate");
        return static_cast<std::uint32_t>(std::ceil(frames / kBlockFrames));
    };
    refillBlocks_ = blocksForSeconds(60.0);
    thresholdBlocks_ = blocksForSeconds(15.0);
    offlineRefillBlocks_ = blocksForSeconds(120.0);
    offlineThresholdBlocks_ = blocksForSeconds(30.0);
    const auto initial = blocksForSeconds(30.0);
    // Existing chunks and recorded audio survive rate changes. Ensure the new
    // rate has its startup reserve, without shrinking or rebuilding old storage.
    try {
        if (freeBlockCount_ < initial)
            acceptChunk(*prepareChunk(initial - freeBlockCount_));
        refill_.store(Refill::idle, std::memory_order_release);
    } catch (...) {
        if (chunks_.empty()) throw; // No startup reserve: instance creation cannot succeed.
        refill_.store(Refill::failed, std::memory_order_release);
    }
    startWorker();
}

void SampleStoragePool::configure(const std::uint32_t logicalBlocks) {
    stopWorker();
    dynamic_ = false;
    chunks_.clear();
    mappingOwners_.clear();
    freeBlocks_ = nullptr;
    freeBlockCount_ = 0U;
    captureBlocks_.store(0U, std::memory_order_relaxed);
    refill_.store(Refill::idle, std::memory_order_relaxed);
    initializeMappings(logicalBlocks);
    acceptChunk(*prepareChunk(logicalBlocks * 2U));
}

void SampleStoragePool::requestRefill() noexcept {
    if (!dynamic_ || freeBlockCount_ > refillThresholdBlocks()) return;
    auto expected = Refill::idle;
    static_cast<void>(refill_.compare_exchange_strong(expected, Refill::requested,
        std::memory_order_release, std::memory_order_relaxed));
}

std::uint32_t SampleStoragePool::refillThresholdBlocks() const noexcept {
    return offline_.load(std::memory_order_relaxed) ? offlineThresholdBlocks_ : thresholdBlocks_;
}

void SampleStoragePool::refillLoop() noexcept {
    while (!stop_.load(std::memory_order_acquire)) {
        auto expected = Refill::requested;
        if (refill_.compare_exchange_strong(expected, Refill::preparing,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            try {
                preparedChunk_ = prepareChunk(offline_.load(std::memory_order_acquire)
                    ? offlineRefillBlocks_ : refillBlocks_);
                refill_.store(Refill::ready, std::memory_order_release);
            } catch (...) {
                // Latch failures instead of repeatedly attempting allocations
                // under memory pressure. Already prepared blocks remain usable.
                refill_.store(Refill::failed, std::memory_order_release);
            }
        }
        // The audio callback only publishes an atomic request. It never calls
        // a mutex-based queue, condition variable, or operating-system wakeup.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void SampleStoragePool::serviceCapture(const bool recording) noexcept {
    if (refill_.load(std::memory_order_acquire) == Refill::ready) {
        acceptChunk(*preparedChunk_);
        refill_.store(Refill::idle, std::memory_order_release);
    }
    if (recording) requestRefill();
}

bool SampleStoragePool::captureAllocationFailed() const noexcept {
    return refill_.load(std::memory_order_acquire) == Refill::failed;
}

void SampleStoragePool::retryCaptureAllocation() noexcept {
    auto expected = Refill::failed;
    // Recovery after exhaustion must work even before another frame can be
    // captured. Ordinary Arm with an adequate reserve does not request growth.
    const auto next = freeBlockCount_ <= refillThresholdBlocks() ? Refill::requested : Refill::idle;
    static_cast<void>(refill_.compare_exchange_strong(expected, next,
        std::memory_order_release, std::memory_order_relaxed));
}

SampleStoragePool::CaptureBlock*& SampleStoragePool::mapping(Storage& storage,
    const std::uint32_t block) noexcept {
    return storage.mapping[block / kMappingPageBlocks].load(std::memory_order_acquire)
        ->blocks[block % kMappingPageBlocks];
}

const SampleStoragePool::CaptureBlock* SampleStoragePool::mappedBlock(const Storage& storage,
    const std::uint32_t block) const noexcept {
    const auto page = block / kMappingPageBlocks;
    if (page >= storage.mappingPages) return nullptr;
    const auto* entry = storage.mapping[page].load(std::memory_order_acquire);
    return entry ? entry->blocks[block % kMappingPageBlocks] : nullptr;
}

void SampleStoragePool::collectImported() {
    std::erase_if(imported_, [](const auto& storage) {
        return storage->references.load(std::memory_order_acquire) == 0U;
    });
}

SampleStoragePool::Storage* SampleStoragePool::acquireCapture() noexcept {
    for (auto& storage : capture_) {
        if (storage.allocatedBlocks != 0U) continue;
        std::uint32_t expected = 0U;
        if (storage.references.compare_exchange_strong(expected, 1U, std::memory_order_acq_rel))
            return &storage;
    }
    return nullptr;
}

bool SampleStoragePool::hasCaptureBlock(const Storage& storage,
    const std::uint32_t block) const noexcept {
    return mappedBlock(storage, block) != nullptr;
}

bool SampleStoragePool::ensureCaptureBlock(Storage& storage,
    const std::uint32_t block) noexcept {
    if (block >= maxCaptureBlocks_) return false;
    if (hasCaptureBlock(storage, block)) return true;
    requestRefill();
    if (freeBlockCount_ == 0U ||
        !storage.mapping[block / kMappingPageBlocks].load(std::memory_order_acquire)) return false;
    auto& mapped = mapping(storage, block);
    mapped = freeBlocks_;
    freeBlocks_ = freeBlocks_->next;
    --freeBlockCount_;
    publishedFreeBlocks_.store(freeBlockCount_, std::memory_order_release);
    requestRefill();
    storage.allocatedBlocks = std::max(storage.allocatedBlocks, block + 1U);
    return true;
}

std::uint32_t SampleStoragePool::trimCapture(Storage& storage,
    const std::uint32_t keepBlocks) noexcept {
    const auto before = storage.allocatedBlocks;
    while (storage.allocatedBlocks > keepBlocks) {
        auto& mapped = mapping(storage, --storage.allocatedBlocks);
        mapped->next = freeBlocks_;
        freeBlocks_ = mapped;
        ++freeBlockCount_;
        mapped = nullptr;
    }
    publishedFreeBlocks_.store(freeBlockCount_, std::memory_order_release);
    return before - storage.allocatedBlocks;
}

void SampleStoragePool::reclaimCapture(std::uint32_t blockBudget) noexcept {
    for (auto& storage : capture_) {
        if (storage.references.load(std::memory_order_acquire) != 0U) continue;
        while (storage.allocatedBlocks != 0U && blockBudget != 0U) {
            auto& mapped = mapping(storage, --storage.allocatedBlocks);
            mapped->next = freeBlocks_;
            freeBlocks_ = mapped;
            ++freeBlockCount_;
            mapped = nullptr;
            --blockBudget;
        }
        if (blockBudget == 0U) break;
    }
    publishedFreeBlocks_.store(freeBlockCount_, std::memory_order_release);
}

void SampleStoragePool::writeCapture(Storage& storage, const std::uint32_t frame,
    const float left, const float right) noexcept {
    auto* mapped = mapping(storage, frame / kBlockFrames);
    const auto offset = (frame % kBlockFrames) * 2U;
    mapped->stereo[offset] = left;
    mapped->stereo[offset + 1U] = right;
}

float SampleStoragePool::sample(const Storage* storage, const std::uint32_t frame,
    const std::uint32_t channel) const noexcept {
    if (!storage || channel >= 2U) return 0.0f;
    if (!storage->isCapture())
        return frame < storage->frames ? storage->stereo[static_cast<std::size_t>(frame) * 2U + channel] : 0.0f;
    const auto* mapped = mappedBlock(*storage, frame / kBlockFrames);
    if (!mapped) return 0.0f;
    return mapped->stereo[(frame % kBlockFrames) * 2U + channel];
}

void SampleStoragePool::copySamples(const Storage* storage,
    const std::span<float> destination) const {
    if (storage && !storage->isCapture()) {
        std::copy_n(storage->stereo.data(), destination.size(), destination.data());
    } else {
        for (std::size_t i = 0; i < destination.size(); ++i)
            destination[i] = sample(storage, static_cast<std::uint32_t>(i / 2U),
                                    static_cast<std::uint32_t>(i % 2U));
    }
}

SampleStoragePool::Storage* SampleStoragePool::prepareImport(const PadData& source,
    const double hostSampleRate, const std::uint32_t maxFrames) {
    if (!std::isfinite(source.sampleRate) || source.sampleRate <= 1.0 || source.frames == 0U ||
        !std::isfinite(source.peak) || source.peak < 0.0f || !std::isfinite(source.rms) || source.rms < 0.0f ||
        source.stereo.size() < static_cast<std::size_t>(source.frames) * 2U ||
        !std::all_of(source.stereo.begin(), source.stereo.begin() + static_cast<std::size_t>(source.frames) * 2U,
            [](float sample) noexcept { return std::isfinite(sample); })) return nullptr;
    auto storage = std::make_unique<Storage>();
    storage->frames = source.frames;
    storage->sampleRate = source.sampleRate;
    storage->peak = source.peak;
    storage->rms = source.rms;
    if (source.frames <= maxFrames) {
        storage->stereo.assign(source.stereo.begin(), source.stereo.begin() + static_cast<std::size_t>(source.frames) * 2U);
    } else {
        const auto frames = static_cast<std::uint64_t>(std::llround(static_cast<double>(source.frames) * hostSampleRate / source.sampleRate));
        if (frames == 0U || frames > maxFrames) return nullptr;
        storage->frames = static_cast<std::uint32_t>(frames);
        storage->sampleRate = hostSampleRate;
        storage->stereo.resize(static_cast<std::size_t>(frames) * 2U);
        sms::dsp::SincResampler resampler;
        resampler.configure(source.sampleRate / hostSampleRate);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const double position = frame * source.sampleRate / hostSampleRate;
            const auto sample = resampler.read(position, 0U, source.frames, false,
                [&](const std::uint32_t index) noexcept {
                    const auto offset = static_cast<std::size_t>(index) * 2U;
                    return sms::dsp::StereoFrame{source.stereo[offset], source.stereo[offset + 1U]};
                });
            storage->stereo[static_cast<std::size_t>(frame) * 2U] = sample.left;
            storage->stereo[static_cast<std::size_t>(frame) * 2U + 1U] = sample.right;
        }
    }
    Storage* result = storage.get();
    imported_.push_back(std::move(storage));
    return result;
}

} // namespace midichopper
