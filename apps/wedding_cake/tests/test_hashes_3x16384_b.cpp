#include <filesystem>
#include <random>
#include <gtest/gtest.h>

#include "store_b.hpp"

// Tells GTEST to break on failure
static const bool enable_gtest_break = []() {
    ::testing::GTEST_FLAG(break_on_failure) = true;
    return true;
}();

namespace wedding_cake {
    namespace {
        // Fixed-seed PRNG for deterministic, reproducible test runs
        template <size_t HashBytesCount>
        class DeterministicHashGenerator {
            std::mt19937_64 rng_{42}; // Fixed seed 42
        public:
            std::array<uint8_t, HashBytesCount> make_random_hash() {
                std::array<uint8_t, HashBytesCount> bytes;
                for (size_t i = 0; i < HashBytesCount; ++i) {
                    bytes[i] = static_cast<uint8_t>(rng_() & 0xFF);
                }
                return bytes;
            }
        };
    }

// -----------------------------------------------------------------------------
// Templated Test Helper Functions
// -----------------------------------------------------------------------------

template <size_t HashBytesCount>
static void RunTestSingleHashAbsent() {
    // Append the size to the path to keep concurrent/sequential tests isolated
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);

    {
        // Braces so StoreB dtor called before remove_all()
        StoreB<HashBytesCount> store_b(temp_path);

        DeterministicHashGenerator<HashBytesCount> gen;
        auto inserted_hash = gen.make_random_hash();
        auto block_height = 0;
        std::vector<std::optional<HashBytes<HashBytesCount>>> vec;
        vec.push_back(inserted_hash);
        store_b.feed(block_height, vec);

        auto missing_hash = gen.make_random_hash();

        EXPECT_EQ(store_b.reverse_lookup(missing_hash), GLOBAL_PI_NO_MATCH)
            << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";
    }

    // Clean up directory after a successful test run
    std::filesystem::remove_all(temp_path);
}

template <size_t HashBytesCount>
static void RunTestSingleHashPresent() {
    // Append the size to the path to keep concurrent/sequential tests isolated
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);

    { // Braces so StoreB dtor called before remove_all()
        StoreB<HashBytesCount> store_b(temp_path);

        DeterministicHashGenerator<HashBytesCount> gen;
        auto inserted_hash = gen.make_random_hash();
        auto block_height = 0;
        std::vector<std::optional<HashBytes<HashBytesCount>>> vec;
        vec.push_back(inserted_hash);
        store_b.feed(block_height, vec);

        EXPECT_EQ(store_b.reverse_lookup(inserted_hash), GlobalPi(0))
            << "Expected GlobalPi(0) for inserted hash (Hash size: " << HashBytesCount << "B)";
    }

    // Clean up directory after a successful test run
    std::filesystem::remove_all(temp_path);
}

template <size_t HashBytesCount>
static void RunTestSingleHashGapB() {
    // Append the size to the path to keep concurrent/sequential tests isolated
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);

    { // Braces so StoreB dtor called before remove_all()
        StoreB<HashBytesCount> store_b(temp_path);

        DeterministicHashGenerator<HashBytesCount> gen;
        auto inserted_hash = gen.make_random_hash();
        auto block_height = 0;
        std::vector<std::optional<HashBytes<HashBytesCount>>> vec;
        vec.push_back(std::nullopt);      // A gap first
        vec.push_back(inserted_hash);       // Then the hash
        store_b.feed(block_height, vec);

        // Forward lookups
        bool found;
        auto opt_hash = store_b.forward_lookup(GlobalPi(0));
        EXPECT_EQ(opt_hash, std::nullopt) << "Expected opt_hash==std::nullopt because it's a gap (Hash size: " << HashBytesCount << "B)";
        opt_hash = store_b.forward_lookup(GlobalPi(1));
        EXPECT_EQ(opt_hash, inserted_hash) << "Expected inserted_hash at GlobalPi(1) (Hash size: " << HashBytesCount << "B)";
        // Reverse lookup
        EXPECT_EQ(store_b.reverse_lookup(inserted_hash), GlobalPi(1))
            << "Expected GlobalPi(1) for inserted hash (Hash size: " << HashBytesCount << "B)";
    }

    // Clean up directory after a successful test run
    std::filesystem::remove_all(temp_path);
}

template <size_t HashBytesCount>
static void RunTestNx16384Hashes(int hashes_count, int block_size, bool with_gaps) {
    std::cout << "Running test for Hash size: " << HashBytesCount << "B" << std::endl;
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);

    std::vector<std::optional<HashBytes<HashBytesCount>>> vec_all;
    DeterministicHashGenerator<HashBytesCount> gen;

    { // Braces so StoreB dtor called before remove_all()
        StoreB<HashBytesCount> store_b(temp_path);

        int block_height = 0;
        std::vector<std::optional<HashBytes<HashBytesCount>>> vec;
        for ( size_t i = 0; i < hashes_count; ++i )
        {
            if (i % 16384 == 0) {
                std::cout << "INSERTING hashindex " << i << " blockindex " << block_height << std::endl;
            }

            auto inserted_hash = gen.make_random_hash();
            if (with_gaps) {
                if (inserted_hash[1] == 0x00) {
                    vec.push_back(std::nullopt);        // Occasional gaps
                    vec_all.push_back(std::nullopt);
                } else {
                    vec.push_back(inserted_hash);
                    vec_all.push_back(inserted_hash);
                }
            } else {
                vec.push_back(inserted_hash);
                vec_all.push_back(inserted_hash);
            }
            if (i % block_size == 0) {
                store_b.feed(block_height, vec);
                block_height++;
                vec.clear();
            }
        }
        if (hashes_count % block_size != 0) {
            // Last drabs
            store_b.feed(block_height, vec);
        }

        auto missing_hash = gen.make_random_hash();

        EXPECT_EQ(store_b.reverse_lookup(missing_hash), GLOBAL_PI_NO_MATCH)
            << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";

        for (size_t i = 0; i < hashes_count; ++i) {
            if (i % 16384 == 0) {
                std::cout << "READING (before reload) hashindex " << i << std::endl;
            }

            // Forward lookup
            auto hash = store_b.forward_lookup(GlobalPi(i));
            // CAN check the gaps, expect std::nullopt==std::nullopt
            if (vec_all[i].has_value()) {
                EXPECT_EQ(hash, vec_all[i].value())
                    << "Expected forward lookup to match GlobalPi's hash (Hash size: " << HashBytesCount << "B)";
            }

            // Reverse lookup
            // Can't check the gaps, have no hash!
            if (vec_all[i].has_value()) {
                EXPECT_EQ(store_b.reverse_lookup(vec_all[i].value()), GlobalPi(i))
                    << "Expected reverse lookup to match hash's presentation index (Hash size: " << HashBytesCount << "B)";
            }
        }
        store_b.flush();
    } // Braces call destructor for store_b

    // Read back after closing store_b
    {
        StoreB<HashBytesCount> store_b(temp_path);

        auto missing_hash = gen.make_random_hash();

        EXPECT_EQ(store_b.reverse_lookup(missing_hash), GLOBAL_PI_NO_MATCH)
            << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";

        for (size_t i = 0; i < hashes_count; ++i) {
            if (i % 16384 == 0) {
                std::cout << "READING (after reload) hashindex " << i << std::endl;
            }

            // Forward lookup
            auto hash = store_b.forward_lookup(GlobalPi(i));
            // CAN check the gaps, expect std::nullopt==std::nullopt
            if (vec_all[i].has_value()) {
                EXPECT_EQ(hash, vec_all[i].value())
                    << "Expected forward lookup to match GlobalPi's hash (Hash size: " << HashBytesCount << "B)";
            }

            // Reverse lookup
            // Can't check the gaps, have no hash!
            if (vec_all[i].has_value()) {
                EXPECT_EQ(store_b.reverse_lookup(vec_all[i].value()), GlobalPi(i))
                    << "Expected reverse lookup to match hash's presentation index (Hash size: " << HashBytesCount << "B)";
            }
        }
    }   // store_b destructed again
    // Clean up directory after a successful test run
    std::filesystem::remove_all(temp_path);
}

// -----------------------------------------------------------------------------
// GoogleTest Suite Drivers
// -----------------------------------------------------------------------------

TEST(WeddingCakeAppTest, RunTestSingleHashAbsentB) {
    RunTestSingleHashAbsent<20>();
    RunTestSingleHashAbsent<32>();
    RunTestSingleHashAbsent<64>();
}
TEST(WeddingCakeAppTest, RunTestSingleHashPresentB) {
    RunTestSingleHashPresent<20>();
    RunTestSingleHashPresent<32>();
    RunTestSingleHashPresent<64>();
}
TEST(WeddingCakeAppTest, RunTestSingleHashGapB) {
    RunTestSingleHashGapB<20>();
    RunTestSingleHashGapB<32>();
    RunTestSingleHashGapB<64>();
}
// 3 * 16384 hashes, 16384 each block, only tests the ingester buffer
TEST(WeddingCakeAppTest, RunTestHashesIngester) {
    RunTestNx16384Hashes<20>(3 * 16384, 16384, false);
    RunTestNx16384Hashes<32>(3 * 16384, 16384, true);
    RunTestNx16384Hashes<64>(3 * 16384, 16384, false);
}
// 3 * 16384 hashes, 7 each block, only tests the ingester buffer
TEST(WeddingCakeAppTest, RunTestHashesIngesterSmallBlocks) {
    RunTestNx16384Hashes<20>(3 * 16384, 7, true);
    RunTestNx16384Hashes<32>(3 * 16384, 7, false);
    RunTestNx16384Hashes<64>(3 * 16384, 7, true);
}
// 3 * 16384 hashes, 66000 each block, only tests the ingester buffer
TEST(WeddingCakeAppTest, RunTestHashesIngesterBigBlocks) {
    RunTestNx16384Hashes<20>(3 * 16384, 66000, false);
    RunTestNx16384Hashes<32>(3 * 16384, 66000, true);
    RunTestNx16384Hashes<64>(3 * 16384, 66000, false);
}
// 65536 hashes, tests the ingester buffer and ingester_tier_folder's hatchling
TEST(WeddingCakeAppTest, RunTestHashesHatchling) {
    RunTestNx16384Hashes<20>(65536, 16384, true);
    RunTestNx16384Hashes<32>(65536, 16384, false);
    RunTestNx16384Hashes<64>(65536, 16384, true);
}
// 65535 * 4 hashes, tests the ingester buffer and ingester_tier_folder's buffer room
TEST(WeddingCakeAppTest, RunTestHashesIngesterBufferRoom) {
    RunTestNx16384Hashes<20>(65535 * 4, 66000, false);
    RunTestNx16384Hashes<32>(65535 * 4, 66000, true);
    RunTestNx16384Hashes<64>(65535 * 4, 66000, false);
}
// 65535 * 5 hashes, should block unless there is a tier after ingester tier
TEST(WeddingCakeAppTest, RunTestHashesIngesterNextTier) {
    RunTestNx16384Hashes<20>(65535 * 5, 66000, true);
    RunTestNx16384Hashes<32>(65535 * 5, 66000, false);
    RunTestNx16384Hashes<64>(65535 * 5, 66000, true);
}

} // namespace wedding_cake
