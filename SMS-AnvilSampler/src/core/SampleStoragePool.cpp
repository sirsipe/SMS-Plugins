#include "SampleStoragePool.hpp"

#include <algorithm>
#include <cmath>

namespace midichopper {

SampleStoragePool::SampleStoragePool(const std::uint32_t logicalBlocks) {
    configure(logicalBlocks);
}

void SampleStoragePool::configure(const std::uint32_t logicalBlocks) {
    samples_.assign(static_cast<std::size_t>(logicalBlocks) * kBlockFrames * 4U, 0.0f);
    for (auto& storage : capture_) {
        storage.references.store(0U, std::memory_order_relaxed);
        storage.allocatedBlocks = 0U;
        storage.blocks.assign(logicalBlocks, kNoBlock);
    }
    freeBlocks_.resize(logicalBlocks * 2U);
    freeBlockCount_ = logicalBlocks * 2U;
    for (std::uint32_t block = 0; block < freeBlockCount_; ++block)
        freeBlocks_[block] = freeBlockCount_ - block - 1U;
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
    return block < storage.blocks.size() && storage.blocks[block] != kNoBlock;
}

bool SampleStoragePool::ensureCaptureBlock(Storage& storage,
    const std::uint32_t block) noexcept {
    if (block >= storage.blocks.size()) return false;
    auto& mapped = storage.blocks[block];
    if (mapped != kNoBlock) return true;
    if (freeBlockCount_ == 0U) return false;
    mapped = freeBlocks_[--freeBlockCount_];
    storage.allocatedBlocks = std::max(storage.allocatedBlocks, block + 1U);
    return true;
}

std::uint32_t SampleStoragePool::trimCapture(Storage& storage,
    const std::uint32_t keepBlocks) noexcept {
    const auto before = storage.allocatedBlocks;
    while (storage.allocatedBlocks > keepBlocks) {
        auto& mapped = storage.blocks[--storage.allocatedBlocks];
        freeBlocks_[freeBlockCount_++] = mapped;
        mapped = kNoBlock;
    }
    return before - storage.allocatedBlocks;
}

void SampleStoragePool::reclaimCapture(std::uint32_t blockBudget) noexcept {
    for (auto& storage : capture_) {
        if (storage.references.load(std::memory_order_acquire) != 0U) continue;
        while (storage.allocatedBlocks != 0U && blockBudget != 0U) {
            auto& mapped = storage.blocks[--storage.allocatedBlocks];
            freeBlocks_[freeBlockCount_++] = mapped;
            mapped = kNoBlock;
            --blockBudget;
        }
        if (blockBudget == 0U) break;
    }
}

void SampleStoragePool::writeCapture(Storage& storage, const std::uint32_t frame,
    const float left, const float right) noexcept {
    const auto mapped = storage.blocks[frame / kBlockFrames];
    const auto offset = (static_cast<std::size_t>(mapped) * kBlockFrames + frame % kBlockFrames) * 2U;
    samples_[offset] = left;
    samples_[offset + 1U] = right;
}

float SampleStoragePool::sample(const Storage* storage, const std::uint32_t frame,
    const std::uint32_t channel) const noexcept {
    if (!storage || channel >= 2U) return 0.0f;
    if (storage->blocks.empty())
        return frame < storage->frames ? storage->stereo[static_cast<std::size_t>(frame) * 2U + channel] : 0.0f;
    const auto block = frame / kBlockFrames;
    if (block >= storage->blocks.size()) return 0.0f;
    const auto mapped = storage->blocks[block];
    if (mapped == kNoBlock) return 0.0f;
    return samples_[(static_cast<std::size_t>(mapped) * kBlockFrames + frame % kBlockFrames) * 2U + channel];
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
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const double position = std::min(static_cast<double>(source.frames - 1U), frame * source.sampleRate / hostSampleRate);
            const auto first = static_cast<std::uint32_t>(position), second = std::min(first + 1U, source.frames - 1U);
            const auto fraction = static_cast<float>(position - first);
            for (std::uint32_t channel = 0; channel < 2U; ++channel) {
                const auto a = static_cast<std::size_t>(first) * 2U + channel, b = static_cast<std::size_t>(second) * 2U + channel;
                storage->stereo[static_cast<std::size_t>(frame) * 2U + channel] = source.stereo[a] + (source.stereo[b] - source.stereo[a]) * fraction;
            }
        }
    }
    Storage* result = storage.get();
    imported_.push_back(std::move(storage));
    return result;
}

} // namespace midichopper
