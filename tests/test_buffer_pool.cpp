#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "minisdk/BufferPool.hpp"
#include "minisdk/buffer.hpp"

using minisdk::Buffer;
using minisdk::BufferPool;

TEST_CASE("a fresh pool has every block available", "[pool]") {
    BufferPool pool(128, 8);

    REQUIRE(pool.blockCount() == 8);
    REQUIRE(pool.available() == 8);
    REQUIRE(pool.fallbackCount() == 0);
    REQUIRE(pool.blockSize() >= 128); // rounded up for alignment
}

TEST_CASE("zero sized pools are rejected", "[pool]") {
    REQUIRE_THROWS_AS(BufferPool(0, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(BufferPool(128, 0), std::invalid_argument);
}

TEST_CASE("allocate takes a block and deallocate returns it", "[pool]") {
    BufferPool pool(128, 4);

    void* block = pool.allocate(64);
    REQUIRE(block != nullptr);
    REQUIRE(pool.available() == 3);

    pool.deallocate(block);
    REQUIRE(pool.available() == 4);
    REQUIRE(pool.fallbackCount() == 0);
}

TEST_CASE("a freed block is handed out again", "[pool]") {
    BufferPool pool(128, 4);

    void* first = pool.allocate(64);
    pool.deallocate(first);
    void* second = pool.allocate(64);

    REQUIRE(second == first); // the free list is LIFO, so the block comes straight back
}

TEST_CASE("blocks do not overlap", "[pool]") {
    constexpr std::size_t blockCount = 16;
    BufferPool pool(64, blockCount);

    std::vector<void*> blocks;
    for (std::size_t i = 0; i < blockCount; ++i) {
        void* block = pool.allocate(64);
        std::memset(block, static_cast<int>(i), 64);
        blocks.push_back(block);
    }

    std::set<void*> unique(blocks.begin(), blocks.end());
    REQUIRE(unique.size() == blockCount);

    for (std::size_t i = 0; i < blockCount; ++i) {
        const auto* bytes = static_cast<const unsigned char*>(blocks[i]);
        REQUIRE(bytes[0] == static_cast<unsigned char>(i));
        REQUIRE(bytes[63] == static_cast<unsigned char>(i)); // writing one block did not touch another
    }

    for (void* block : blocks) {
        pool.deallocate(block);
    }
    REQUIRE(pool.available() == blockCount);
}

TEST_CASE("an exhausted arena falls back instead of failing", "[pool]") {
    BufferPool pool(64, 2);

    void* a = pool.allocate(64);
    void* b = pool.allocate(64);
    REQUIRE(pool.available() == 0);
    REQUIRE(pool.fallbackCount() == 0);

    void* overflow = pool.allocate(64);
    REQUIRE(overflow != nullptr);
    REQUIRE(pool.fallbackCount() == 1);
    REQUIRE(pool.available() == 0); // the fallback did not come out of the arena

    std::memset(overflow, 0xAB, 64); // and it is real, usable memory

    pool.deallocate(overflow);
    REQUIRE(pool.available() == 0); // returning it must not inflate the arena
    pool.deallocate(a);
    pool.deallocate(b);
    REQUIRE(pool.available() == 2);
}

TEST_CASE("a request larger than the block size bypasses the pool", "[pool]") {
    BufferPool pool(64, 4);

    void* big = pool.allocate(4096);
    REQUIRE(big != nullptr);
    REQUIRE(pool.fallbackCount() == 1);
    REQUIRE(pool.available() == 4); // no block was consumed

    std::memset(big, 0x5A, 4096);
    pool.deallocate(big);
    REQUIRE(pool.available() == 4);
}

TEST_CASE("deallocating nullptr is a no-op", "[pool]") {
    BufferPool pool(64, 2);

    REQUIRE_NOTHROW(pool.deallocate(nullptr));
    REQUIRE(pool.available() == 2);
}

TEST_CASE("blocks are suitably aligned", "[pool]") {
    BufferPool pool(1, 8); // rounded up to at least alignof(max_align_t)

    for (std::size_t i = 0; i < 8; ++i) {
        void* block = pool.allocate(1);
        auto address = reinterpret_cast<std::uintptr_t>(block);
        REQUIRE(address % alignof(std::max_align_t) == 0);
    }
}

// Exercised under TSan in CI: the free list is the shared state the pool has to protect.
TEST_CASE("concurrent allocate and deallocate keep the free list consistent", "[pool][concurrency]") {
    constexpr std::size_t blockCount = 32;
    constexpr int threadCount = 8;
    constexpr int roundsPerThread = 2000;

    BufferPool pool(128, blockCount);

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&pool] {
            for (int round = 0; round < roundsPerThread; ++round) {
                void* block = pool.allocate(128);
                std::memset(block, 1, 128);
                pool.deallocate(block);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(pool.available() == blockCount); // every block found its way home
}

TEST_CASE("a buffer without a pool is unaffected", "[pool][buffer]") {
    BufferPool pool(2048, 4);

    Buffer plain(128);

    REQUIRE(plain.allocator() == nullptr);
    REQUIRE(pool.available() == 4); // the pool was never consulted
}

TEST_CASE("a pooled buffer borrows a block and gives it back", "[pool][buffer]") {
    BufferPool pool(2048, 4);

    {
        Buffer payload(1500, &pool);
        REQUIRE(payload.allocator() == &pool);
        REQUIRE(payload.getSize() == 1500);
        REQUIRE(pool.available() == 3);
    }

    REQUIRE(pool.available() == 4);
    REQUIRE(pool.fallbackCount() == 0);
}

TEST_CASE("a pooled payload is still zero-initialised", "[pool][buffer]") {
    BufferPool pool(2048, 2);

    // Dirty a block, release it, then take it again: pool memory is recycled, not fresh.
    {
        Buffer scribble(2048, &pool);
        std::memset(&scribble[0], 0xFF, 2048);
    }

    Buffer fresh(2048, &pool);
    for (size_t i = 0; i < fresh.getSize(); ++i) {
        REQUIRE(fresh[i] == 0);
    }
}

TEST_CASE("copying a pooled buffer takes a second block from the same pool", "[pool][buffer]") {
    BufferPool pool(2048, 4);

    Buffer original(1000, &pool);
    original[0] = 9;

    Buffer copy(original);

    REQUIRE(copy.allocator() == &pool);
    REQUIRE(copy[0] == 9);
    REQUIRE(pool.available() == 2);
}

TEST_CASE("moving a pooled buffer does not touch the pool", "[pool][buffer]") {
    BufferPool pool(2048, 4);

    Buffer original(1000, &pool);
    REQUIRE(pool.available() == 3);

    Buffer moved(std::move(original));

    REQUIRE(moved.allocator() == &pool);
    REQUIRE(pool.available() == 3); // ownership changed hands, no new block was taken
}

TEST_CASE("a payload larger than the block size falls back", "[pool][buffer]") {
    BufferPool pool(512, 4);

    Buffer jumbo(9000, &pool);

    REQUIRE(jumbo.getSize() == 9000);
    REQUIRE(pool.fallbackCount() == 1);
    REQUIRE(pool.available() == 4); // the arena was left alone
}

TEST_CASE("repeated pooled buffers recycle the same blocks", "[pool][buffer]") {
    BufferPool pool(2048, 2);

    for (int i = 0; i < 1000; ++i) {
        Buffer payload(1500, &pool);
        payload[0] = 1;
    }

    REQUIRE(pool.available() == 2);
    REQUIRE(pool.fallbackCount() == 0); // 1000 buffers, zero trips to the global allocator
}
