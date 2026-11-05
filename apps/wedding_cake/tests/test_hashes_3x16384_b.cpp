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
#    // Append the size to the path to keep concurrent/sequential tests isolated
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
static void RunTestNx16384Hashes(int N, bool with_gaps) {
    std::cout << "Running test for Hash size: " << HashBytesCount << "B" << std::endl;
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);

    std::vector<std::optional<HashBytes<HashBytesCount>>> vec_all;
    DeterministicHashGenerator<HashBytesCount> gen;

    { // Braces so StoreB dtor called before remove_all()
        StoreB<HashBytesCount> store_b(temp_path);

        // Three blocks of 16384 hashes
        for ( size_t i = 0; i < N; ++i )
        {
            std::cout << "INSERTING 16384: Block height: " << i << std::endl;
            std::vector<std::optional<HashBytes<HashBytesCount>>> vec;

            for ( size_t j = 0; j < 16384; ++j )
            {
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
            }
            auto block_height = i;
            store_b.feed(block_height, vec);
        }

        auto missing_hash = gen.make_random_hash();

        EXPECT_EQ(store_b.reverse_lookup(missing_hash), GLOBAL_PI_NO_MATCH)
            << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";

        for (size_t i = 0; i < N; ++i) {
            std::cout << "READING 16384 (without closing): Block height: " << i << std::endl;
            for (size_t j = 0; j < 16384; ++j) {
                auto k = i * 16384 + j;

                // Forward lookup
                bool found;
                auto hash = store_b.forward_lookup(GlobalPi(k), found);
                EXPECT_TRUE(found)
                    << "Expected forward lookup to find GlobalPi's hash (Hash size: " << HashBytesCount << "B)";
                // CAN check the gaps, expect std::nullopt==std::nullopt
                if (vec_all[k].has_value()) {
                    EXPECT_EQ(hash, vec_all[k].value())
                        << "Expected forward lookup to match GlobalPi's hash (Hash size: " << HashBytesCount << "B)";
                }

                // Reverse lookup
                // Can't check the gaps, have no hash!
                if (vec_all[k].has_value()) {
                    EXPECT_EQ(store_b.reverse_lookup(vec_all[k].value()), GlobalPi(k))
                        << "Expected reverse lookup to match hash's presentation index (Hash size: " << HashBytesCount << "B)";
                }
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

        for (size_t i = 0; i < N; ++i) {
            std::cout << "READING 16384 (after closing and re-opening): Block height: " << i << std::endl;
            for (size_t j = 0; j < 16384; ++j) {
                auto k = i * 16384 + j;

                // Forward lookup
                bool found;
                auto hash = store_b.forward_lookup(GlobalPi(k), found);
                EXPECT_TRUE(found)
                    << "Expected forward lookup to find GlobalPi's hash (Hash size: " << HashBytesCount << "B)";
                // CAN check the gaps, expect std::nullopt==std::nullopt
                if (vec_all[k].has_value()) {
                    EXPECT_EQ(hash, vec_all[k].value())
                        << "Expected forward lookup to match GlobalPi's hash (Hash size: " << HashBytesCount << "B)";
                }

                // Reverse lookup
                // Can't check the gaps, have no hash!
                if (vec_all[k].has_value()) {
                    EXPECT_EQ(store_b.reverse_lookup(vec_all[k].value()), GlobalPi(k))
                        << "Expected reverse lookup to match hash's presentation index (Hash size: " << HashBytesCount << "B)";
                }
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
    TEST(WeddingCakeAppTest, RunTestHashes3x16384B) {
        RunTestNx16384Hashes<20>(3, false);
        RunTestNx16384Hashes<32>(3, false);
        RunTestNx16384Hashes<64>(3, false);
    }
    TEST(WeddingCakeAppTest, RunTestHashes4x16384B) {
        RunTestNx16384Hashes<20>(4, false);
        RunTestNx16384Hashes<32>(4, false);
        RunTestNx16384Hashes<64>(4, false);
    }
    TEST(WeddingCakeAppTest, RunTestHashes3x16384B_with_gaps) {
        RunTestNx16384Hashes<20>(3, true);
        RunTestNx16384Hashes<32>(3, true);
        RunTestNx16384Hashes<64>(3, true);
    }
    TEST(WeddingCakeAppTest, RunTestHashes4x16384B_with_gaps_) {
        RunTestNx16384Hashes<20>(4, true);
        RunTestNx16384Hashes<32>(4, true);
        RunTestNx16384Hashes<64>(4, true);
    }

} // namespace wedding_cake
