#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace minisdk {

// Fixed-block pool for packet payloads.
//
// Payloads are variable sized while a free list needs uniform blocks, so the pool serves every
// request up to blockSize from a pre-allocated arena and forwards the rest to the global
// allocator. That mirrors how packet pools are sized around an MTU, and it means the pool helps
// small frequent buffers and does nothing for large ones - which is the point worth measuring.
//
// Two measured iterations shaped the rest. A single free list behind a single mutex ran 8x
// slower than new[] at 8 threads, so the arena was split into cache-line separated shards. That
// fixed the threaded case but still lost to glibc malloc on small payloads, because its tcache
// serves those from a lock-free per-thread cache; hence the thread cache in front of the shards.
//
// Lifetime: the pool must outlive every Buffer that was given it, and every thread that
// allocated from it.
class BufferPool {
public:
    BufferPool(std::size_t blockSize, std::size_t blockCount);
    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    void* allocate(std::size_t bytes);
    void deallocate(void* pointer) noexcept;

    // Rounded up to a power of two, so the request is served by a block at least this large.
    std::size_t blockSize() const noexcept { return blockSize_; }
    std::size_t blockCount() const noexcept { return blockCount_; }
    std::size_t shardCount() const noexcept { return shardCount_; }

    std::size_t available() const;     // blocks not handed out, wherever they are parked
    std::size_t fallbackCount() const; // requests the arena could not serve

private:
    // The free list lives inside the unused blocks themselves, so an empty block costs nothing.
    struct FreeNode {
        FreeNode* next;
    };

    // One cache line each: unpadded, the shard heads would share a line and the cores would
    // bounce it between themselves, which is the contention being removed.
    struct alignas(64) Shard {
        std::mutex mutex;
        FreeNode* freeList = nullptr;
        std::size_t available = 0;
    };

    static constexpr std::size_t kCacheCapacity = 4;
    static constexpr std::size_t kMaxTrackedPools = 4;

    // owner and count are read by available() from other threads, so they are atomic even
    // though the blocks themselves are touched only by the owning thread.
    struct ThreadCacheSlot {
        std::atomic<BufferPool*> owner{nullptr};
        std::atomic<std::size_t> count{0};
        void* blocks[kCacheCapacity] = {};
    };

    struct ThreadCaches {
        ThreadCaches();
        ~ThreadCaches();
        ThreadCacheSlot slots[kMaxTrackedPools];
    };

    static ThreadCaches& threadCaches() noexcept;
    static std::mutex& registryMutex();
    static std::vector<ThreadCaches*>& registry();

    void returnToShard(void* pointer) noexcept;
    std::size_t cachedBlockCount() const;
    std::size_t shardIndexForCaller() const noexcept;
    std::size_t shardIndexForPointer(const void* pointer) const noexcept;
    bool ownsPointer(const void* pointer) const noexcept;

    std::size_t blockSize_;
    std::size_t blockShift_;  // log2(blockSize_), so locating a block is a shift, not a division
    std::size_t blockCount_;
    std::size_t shardCount_;
    std::size_t shardMask_;   // shardCount_ - 1, so picking a shard is a mask, not a modulo
    std::size_t poolSlot_;    // which thread-cache slot this pool uses
    std::byte* arena_ = nullptr;
    std::unique_ptr<Shard[]> shards_;
    std::atomic<std::size_t> fallbacks_{0};
};

} // namespace minisdk
