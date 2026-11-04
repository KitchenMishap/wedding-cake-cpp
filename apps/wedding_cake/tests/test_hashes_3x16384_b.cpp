#include <filesystem>
#include <random>
#include <gtest/gtest.h>

#include "store_b.hpp"

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
static void RunTest3x16384Hashes() {
    std::cout << "Running test for Hash size: " << HashBytesCount << "B" << std::endl;
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);

    { // Braces so StoreB dtor called before remove_all()
        StoreB<HashBytesCount> store_b(temp_path);

        DeterministicHashGenerator<HashBytesCount> gen;

        std::vector<HashBytes<HashBytesCount>> vec_all;

        // Three blocks of 16384 hashes
        for ( size_t i = 0; i < 3; ++i )
        {
            std::cout << "INSERTING 16384: Block height: " << i << std::endl;
            std::vector<std::optional<HashBytes<HashBytesCount>>> vec;

            for ( size_t j = 0; j < 16384; ++j )
            {
                auto inserted_hash = gen.make_random_hash();
                vec.push_back(inserted_hash);
                vec_all.push_back(inserted_hash);
            }
            auto block_height = i;
            store_b.feed(block_height, vec);
        }

        auto missing_hash = gen.make_random_hash();

        EXPECT_EQ(store_b.reverse_lookup(missing_hash), GLOBAL_PI_NO_MATCH)
            << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";

        for (size_t i = 0; i < 3; ++i) {
            std::cout << "READING 16384: Block height: " << i << std::endl;
            for (size_t j = 0; j < 16384; ++j) {
                auto k = i * 16384 + j;
                EXPECT_EQ(store_b.reverse_lookup(vec_all[k]), GlobalPi(k))
                    << "Expected hash to match its presentation index (Hash size: " << HashBytesCount << "B)";
            }
        }
    }
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
    RunTest3x16384Hashes<20>();
    RunTest3x16384Hashes<32>();
    RunTest3x16384Hashes<64>();
}

} // namespace wedding_cake
