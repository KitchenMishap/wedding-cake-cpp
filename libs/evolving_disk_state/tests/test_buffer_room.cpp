#include "evolving_disk_state/buffer_room.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <string>
#include <span>

using namespace evolving_disk_state;
using namespace std::chrono_literals;

namespace {
    // -----------------------------------------------------------------------------
    // Helper Move-Only Type State
    // -----------------------------------------------------------------------------
    struct MockMoveOnlyTypeState {
        int id{0};
        bool* purged_flag{nullptr};

        explicit MockMoveOnlyTypeState(int id_val, bool* purged = nullptr)
            : id(id_val), purged_flag(purged) {}

        ~MockMoveOnlyTypeState() = default;

        // Strict move-only enforcement
        MockMoveOnlyTypeState(const MockMoveOnlyTypeState&) = delete;
        MockMoveOnlyTypeState& operator=(const MockMoveOnlyTypeState&) = delete;

        MockMoveOnlyTypeState(MockMoveOnlyTypeState&& other) noexcept
            : id(other.id), purged_flag(other.purged_flag) {
            other.id = -1;
            other.purged_flag = nullptr;
        }

        MockMoveOnlyTypeState& operator=(MockMoveOnlyTypeState&& other) noexcept {
            if (this != &other) {
                id = other.id;
                purged_flag = other.purged_flag;
                other.id = -1;
                other.purged_flag = nullptr;
            }
            return *this;
        }

        // Disk purge hook invoked on Discard()
        void purge_from_disk() const {
            if (purged_flag) {
                *purged_flag = true;
            }
        }
    };
}

// -----------------------------------------------------------------------------
// Test 1: Basic Producer & Consumer Blocking Behavior
// -----------------------------------------------------------------------------
TEST(BufferRoomTest, ProducerConsumerHandshake) {
    constexpr std::size_t Capacity = 4;
    BufferRoom<MockMoveOnlyTypeState, Capacity> room;

    std::atomic<bool> consumer_executed{false};
    std::atomic<int> items_seen_count{0};

    // Consumer thread starts first and immediately blocks waiting for Capacity items
    std::jthread consumer_thread([&]() {
        const bool result = room.with_contents_when_filled([&](const std::span<const MockMoveOnlyTypeState, Capacity> items) {
            items_seen_count = static_cast<int>(items.size());
            for (std::size_t i = 0; i < items.size(); ++i) {
                EXPECT_EQ(items[i].id, static_cast<int>(i + 1));
            }
            consumer_executed = true;
        });
        EXPECT_TRUE(result);
    });

    // Short sleep to ensure consumer is actively waiting on condition variable
    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(consumer_executed);

    // Producer feeds items 1..3 -> Consumer should still be waiting
    for (int i = 1; i < static_cast<int>(Capacity); ++i) {
        room.Feed(MockMoveOnlyTypeState(i));
        EXPECT_FALSE(consumer_executed);
    }

    // Producer feeds the 4th item -> Hits capacity, unblocks consumer!
    room.Feed(MockMoveOnlyTypeState(static_cast<int>(Capacity)));

    // Wait for consumer thread to join
    consumer_thread.join();

    EXPECT_TRUE(consumer_executed);
    EXPECT_EQ(items_seen_count, static_cast<int>(Capacity));
}

// -----------------------------------------------------------------------------
// Test 2: Concurrent Multi-Threaded Readers (shared_mutex access)
// -----------------------------------------------------------------------------
TEST(BufferRoomTest, ConcurrentReadersAllowed) {
    constexpr std::size_t Capacity = 2;
    BufferRoom<MockMoveOnlyTypeState, Capacity> room;

    // Fill buffer to capacity
    room.Feed(MockMoveOnlyTypeState(101));
    room.Feed(MockMoveOnlyTypeState(102));

    constexpr int NumReaders = 4;
    std::atomic<int> concurrent_active_readers{0};
    std::atomic<int> max_simultaneous_readers{0};
    std::atomic<int> successful_reads{0};

    auto reader_work = [&]() {
        const bool ok = room.with_current_contents([&](const std::span<const MockMoveOnlyTypeState> items) {
            const int current = ++concurrent_active_readers;
            
            // Record max concurrent readers observed
            int max_val = max_simultaneous_readers.load();
            while (current > max_val && !max_simultaneous_readers.compare_exchange_weak(max_val, current)) {}

            // Hold read lock briefly to allow overlapping threads
            std::this_thread::sleep_for(30ms);

            EXPECT_EQ(items[0].id, 101);
            EXPECT_EQ(items[1].id, 102);

            --concurrent_active_readers;
        });

        if (ok) {
            ++successful_reads;
        }
    };

    std::vector<std::jthread> readers;
    readers.reserve(NumReaders);
    for (int i = 0; i < NumReaders; ++i) {
        readers.emplace_back(reader_work);
    }

    for (auto& t : readers) {
        t.join();
    }

    EXPECT_EQ(successful_reads, NumReaders);
    EXPECT_GT(max_simultaneous_readers, 1) << "Expected multiple threads to hold with_contents shared_lock simultaneously";
}

// -----------------------------------------------------------------------------
// Test 3: Discard Mechanics & Physical Purge Callback
// -----------------------------------------------------------------------------
TEST(BufferRoomTest, DiscardPurgesDiskStateAndUnblocksProducers) {
    constexpr std::size_t Capacity = 2;
    BufferRoom<MockMoveOnlyTypeState, Capacity> room;

    bool item1_purged = false;
    bool item2_purged = false;

    // 1. Fill buffer to capacity
    room.Feed(MockMoveOnlyTypeState(1, &item1_purged));
    room.Feed(MockMoveOnlyTypeState(2, &item2_purged));

    // Verify items not purged yet
    EXPECT_FALSE(item1_purged);
    EXPECT_FALSE(item2_purged);

    // 2. Producer thread attempts to Feed 3rd item -> Blocks because capacity is full!
    std::atomic<bool> producer_unblocked{false};
    std::jthread producer_thread([&]() {
        room.Feed(MockMoveOnlyTypeState(3)); // Blocks until Discard()
        producer_unblocked = true;
    });

    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(producer_unblocked);

    // 3. Execute Discard()
    room.Discard();

    // Verify physical purge hooks were invoked
    EXPECT_TRUE(item1_purged);
    EXPECT_TRUE(item2_purged);

    // Producer should now unblock as buffer space was freed
    producer_thread.join();
    EXPECT_TRUE(producer_unblocked);
}

// -----------------------------------------------------------------------------
// Test 4: Reader Fallback during Discard (with_contents returns false)
// -----------------------------------------------------------------------------
TEST(BufferRoomTest, ReaderFallbackWhenDiscarding) {
    constexpr std::size_t Capacity = 3;
    BufferRoom<MockMoveOnlyTypeState, Capacity> room;

    // Reader thread waits for 3 items
    std::atomic<bool> reader_finished{false};
    std::atomic<bool> with_contents_result{true};

    std::jthread reader_thread([&]() {
        bool res = room.with_contents_when_filled([&](auto /*items*/) {
            FAIL() << "Lambda should NOT execute during fallback/discard";
        });
        with_contents_result = res;
        reader_finished = true;
    });

    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(reader_finished);

    // Trigger Discard while reader is waiting -> Wakes reader and forces fallback (returns false)
    room.Discard();

    reader_thread.join();

    EXPECT_TRUE(reader_finished);
    EXPECT_FALSE(with_contents_result) << "with_contents must return false on discard to trigger tier fallback";
}