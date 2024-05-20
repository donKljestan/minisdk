#include "minisdk/BufferPool.hpp"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <thread>

namespace minisdk {

namespace {

constexpr std::size_t kAlignment = alignof(std::max_align_t);

// Hands each thread a stable slot on first use, so threads spread across shards instead of
// hashing onto the same one.
std::atomic<std::size_t> g_nextThreadSlot{0};
std::atomic<std::size_t> g_nextPoolId{0};

} // namespace

std::mutex& BufferPool::registryMutex() {
    static std::mutex instance;
    return instance;
}

std::vector<BufferPool::ThreadCaches*>& BufferPool::registry() {
    static std::vector<ThreadCaches*> instance;
    return instance;
}

BufferPool::ThreadCaches::ThreadCaches() {
    std::lock_guard<std::mutex> lock(registryMutex());
    registry().push_back(this);
}

BufferPool::ThreadCaches::~ThreadCaches() {
    // Hand parked blocks back before disappearing, or the arena would shrink with every thread
    // that exits.
    for (ThreadCacheSlot& slot : slots) {
        BufferPool* owner = slot.owner.load(std::memory_order_relaxed);
        const std::size_t held = slot.count.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < held; ++i) {
            owner->returnToShard(slot.blocks[i]);
        }
        slot.count.store(0, std::memory_order_relaxed);
        slot.owner.store(nullptr, std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> lock(registryMutex());
    auto& live = registry();
    live.erase(std::remove(live.begin(), live.end(), this), live.end());
}

BufferPool::ThreadCaches& BufferPool::threadCaches() noexcept {
    thread_local ThreadCaches caches;
    return caches;
}

BufferPool::BufferPool(std::size_t blockSize, std::size_t blockCount)
    : blockCount_(blockCount) {
    if (blockSize == 0 || blockCount == 0) {
        throw std::invalid_argument("BufferPool: block size and block count must be non-zero");
    }

    // Powers of two throughout: the hot path then locates a block with a shift and picks a shard
    // with a mask. With arbitrary sizes those become integer divisions, which measured slower
    // than the allocation they were meant to speed up.
    blockSize_ = std::bit_ceil(std::max({blockSize, sizeof(FreeNode), kAlignment}));
    blockShift_ = static_cast<std::size_t>(std::countr_zero(blockSize_));

    const auto hardwareThreads =
        static_cast<std::size_t>(std::max(1u, std::thread::hardware_concurrency()));
    shardCount_ = std::bit_floor(std::min(hardwareThreads, blockCount_));
    shardMask_ = shardCount_ - 1;
    shards_ = std::make_unique<Shard[]>(shardCount_);

    poolSlot_ = g_nextPoolId.fetch_add(1, std::memory_order_relaxed) % kMaxTrackedPools;

    arena_ = static_cast<std::byte*>(
        ::operator new(blockSize_ * blockCount_, std::align_val_t{kAlignment}));

    // Round-robin so every shard gets an even share even when the counts do not divide.
    for (std::size_t i = 0; i < blockCount_; ++i) {
        auto* node = reinterpret_cast<FreeNode*>(arena_ + i * blockSize_);
        Shard& shard = shards_[i & shardMask_];
        node->next = shard.freeList;
        shard.freeList = node;
        ++shard.available;
    }
}

BufferPool::~BufferPool() {
    // Drop this pool out of every live thread cache first; otherwise a thread exiting later
    // would hand blocks back to an arena that no longer exists.
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        for (ThreadCaches* caches : registry()) {
            ThreadCacheSlot& slot = caches->slots[poolSlot_];
            if (slot.owner.load(std::memory_order_relaxed) == this) {
                slot.count.store(0, std::memory_order_relaxed);
                slot.owner.store(nullptr, std::memory_order_relaxed);
            }
        }
    }
    ::operator delete(arena_, std::align_val_t{kAlignment});
}

std::size_t BufferPool::shardIndexForCaller() const noexcept {
    // Constant-initialised on purpose: a dynamically initialised thread_local costs a guard
    // check on every single access.
    thread_local std::size_t slotPlusOne = 0;
    if (slotPlusOne == 0) {
        slotPlusOne = g_nextThreadSlot.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    return (slotPlusOne - 1) & shardMask_;
}

std::size_t BufferPool::shardIndexForPointer(const void* pointer) const noexcept {
    const auto offset = static_cast<std::size_t>(static_cast<const std::byte*>(pointer) - arena_);
    return (offset >> blockShift_) & shardMask_;
}

bool BufferPool::ownsPointer(const void* pointer) const noexcept {
    const auto* byte = static_cast<const std::byte*>(pointer);
    return byte >= arena_ && byte < arena_ + blockSize_ * blockCount_;
}

void BufferPool::returnToShard(void* pointer) noexcept {
    Shard& shard = shards_[shardIndexForPointer(pointer)];
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto* node = static_cast<FreeNode*>(pointer);
    node->next = shard.freeList;
    shard.freeList = node;
    ++shard.available;
}

void* BufferPool::allocate(std::size_t bytes) {
    if (bytes <= blockSize_) {
        // Fast path: a block this thread parked earlier, no lock at all.
        ThreadCacheSlot& slot = threadCaches().slots[poolSlot_];
        if (slot.owner.load(std::memory_order_relaxed) == this) {
            const std::size_t held = slot.count.load(std::memory_order_relaxed);
            if (held > 0) {
                slot.count.store(held - 1, std::memory_order_relaxed);
                return slot.blocks[held - 1];
            }
        }

        // Own shard next; the remaining ones are only visited when it has run dry.
        const std::size_t start = shardIndexForCaller();
        for (std::size_t i = 0; i < shardCount_; ++i) {
            Shard& shard = shards_[(start + i) & shardMask_];
            std::lock_guard<std::mutex> lock(shard.mutex);
            if (shard.freeList != nullptr) {
                FreeNode* node = shard.freeList;
                shard.freeList = node->next;
                --shard.available;
                return node;
            }
        }
    }
    fallbacks_.fetch_add(1, std::memory_order_relaxed);
    // Oversized, or the whole arena ran dry: the caller still gets memory, just not from the pool.
    return ::operator new(bytes);
}

void BufferPool::deallocate(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    // The address itself says where the block came from, so blocks need no header.
    if (!ownsPointer(pointer)) {
        ::operator delete(pointer);
        return;
    }

    ThreadCacheSlot& slot = threadCaches().slots[poolSlot_];
    if (slot.owner.load(std::memory_order_relaxed) == nullptr) {
        slot.owner.store(this, std::memory_order_relaxed);
    }
    if (slot.owner.load(std::memory_order_relaxed) == this) {
        const std::size_t held = slot.count.load(std::memory_order_relaxed);
        if (held < kCacheCapacity) {
            slot.blocks[held] = pointer;
            slot.count.store(held + 1, std::memory_order_relaxed);
            return;
        }
    }

    // Back to the shard that owns the block, not the caller's shard, or shards would drain
    // into whichever thread happens to free the most.
    returnToShard(pointer);
}

std::size_t BufferPool::cachedBlockCount() const {
    std::lock_guard<std::mutex> lock(registryMutex());
    std::size_t total = 0;
    for (const ThreadCaches* caches : registry()) {
        const ThreadCacheSlot& slot = caches->slots[poolSlot_];
        if (slot.owner.load(std::memory_order_relaxed) == this) {
            total += slot.count.load(std::memory_order_relaxed);
        }
    }
    return total;
}

std::size_t BufferPool::available() const {
    std::size_t total = 0;
    for (std::size_t i = 0; i < shardCount_; ++i) {
        std::lock_guard<std::mutex> lock(shards_[i].mutex);
        total += shards_[i].available;
    }
    // Blocks parked in thread caches are free too, just not on a shard list.
    return total + cachedBlockCount();
}

std::size_t BufferPool::fallbackCount() const {
    return fallbacks_.load(std::memory_order_relaxed);
}

} // namespace minisdk
