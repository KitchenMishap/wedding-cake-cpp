#include <filesystem>
#include <random>
#include <vector>
#include <optional>
#include <array>
#include <iostream>
#include <gtest/gtest.h>

#include "store_b.hpp"

namespace wedding_cake {
namespace {

    // Fixed-seed PRNG for deterministic, reproducible ingestion tests
    template <size_t HashBytesCount>
    class DeterministicHashGenerator {
        std::mt19937_64 rng_{42};
    public:
        std::array<uint8_t, HashBytesCount> make_random_hash() {
            std::array<uint8_t, HashBytesCount> bytes;
            for (size_t i = 0; i < HashBytesCount; ++i) {
                bytes[i] = static_cast<uint8_t>(rng_() & 0xFF);
            }
            return bytes;
        }
    };

    // -------------------------------------------------------------------------
    // Core Ingestion-Only Helper
    // -------------------------------------------------------------------------
    template <size_t HashBytesCount>
    void RunTestIngestOnly(const std::string& test_tag,
                           size_t hashes_count,
                           size_t block_size,
                           bool with_gaps,
                           bool test_reload = true) {
        const std::filesystem::path temp_path =
            "TempIngest_" + test_tag + "_" + std::to_string(HashBytesCount);
        std::filesystem::remove_all(temp_path);

        DeterministicHashGenerator<HashBytesCount> gen;

        // 1. Open StoreB, feed hashes in blocks, flush, and close cleanly
        {
            StoreB<HashBytesCount> store_b(temp_path);

            int block_height = 0;
            std::vector<std::optional<HashBytes<HashBytesCount>>> block;
            block.reserve(block_size);

            for (size_t i = 0; i < hashes_count; ++i) {
                auto h = gen.make_random_hash();
                if (with_gaps && (h[1] == 0x00)) {
                    block.push_back(std::nullopt);
                } else {
                    block.push_back(h);
                }

                if (block.size() == block_size) {
                    EXPECT_NO_THROW(store_b.feed(block_height, block))
                        << "Failed feeding block " << block_height
                        << " (Hash size: " << HashBytesCount << "B)";
                    ++block_height;
                    block.clear();
                }
            }

            if (!block.empty()) {
                EXPECT_NO_THROW(store_b.feed(block_height, block))
                    << "Failed feeding final partial block " << block_height
                    << " (Hash size: " << HashBytesCount << "B)";
            }

            EXPECT_NO_THROW(store_b.flush());
            EXPECT_TRUE(std::filesystem::exists(temp_path));
        } // ~StoreB() runs: verifies clean worker stop & join without hangs

        // 2. Re-open existing store from disk to verify crash-free startup/recovery
        if (test_reload) {
            StoreB<HashBytesCount> reopened_store(temp_path);
            EXPECT_NO_THROW(reopened_store.flush());
        }

        std::filesystem::remove_all(temp_path);
    }

} // namespace

// -----------------------------------------------------------------------------
// Test 1: Small Ingest & Reload (Stays inside Ingester Buffer)
// Expected: PASSES
// -----------------------------------------------------------------------------
TEST(WeddingCakeIngestOnlyTest, SmallIngestAndReload) {
    // 3 * 16384 = 49,152 hashes (< 65,536 donut size), with and without gaps
    RunTestIngestOnly<20>("Small", 3 * 16384, 16384, false);
    RunTestIngestOnly<32>("Small", 3 * 16384, 7, true);
    RunTestIngestOnly<64>("Small", 3 * 16384, 66000, false);
}

// -----------------------------------------------------------------------------
// Test 2: Hatchling Seal & BufferRoom Fill (Exact 4 Donuts = 4 * 65536 Hashes)
// Expected: PASSES (or surfaces any issue when BufferRoom hits capacity 4)
// -----------------------------------------------------------------------------
TEST(WeddingCakeIngestOnlyTest, FillIngesterBufferRoom) {
    // Exactly 1 donut (65,536) -> seals hatchling and feeds BufferRoom[0]
    RunTestIngestOnly<20>("OneDonut", 65536, 16384, false);

    // 4 donuts (4 * 65,536 = 262,144) -> fills BufferRoom to capacity 4 and triggers on_full_cb_
    RunTestIngestOnly<20>("FourDonuts", 65536 * 4, 32768, true);
    RunTestIngestOnly<32>("FourDonuts", 65536 * 4, 32768, false);
}

// -----------------------------------------------------------------------------
// Test 3: Overflow Into Tier 1 (5 Donuts = 5 * 65536 Hashes)
// Expected: FAILS / BLOCKS until Tier 1 transition worker & baking are fully wired
// -----------------------------------------------------------------------------
TEST(WeddingCakeIngestOnlyTest, OverflowTriggersNextTierCreation) {
    // 5th donut forces BufferRoom::Feed() to wait until Tier 1 worker bakes & discards the first 4
    RunTestIngestOnly<20>("FiveDonuts", 65536 * 5, 32768, false);
}

} // namespace wedding_cake