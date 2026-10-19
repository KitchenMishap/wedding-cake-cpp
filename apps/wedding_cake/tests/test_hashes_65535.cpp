#include <filesystem>
#include <random>
#include <gtest/gtest.h>
#include <store.hpp>

namespace wedding_cake {

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

// -----------------------------------------------------------------------------
// Templated Test Helper Functions
// -----------------------------------------------------------------------------

template <size_t HashBytesCount>
static void RunTestSingleHashAbsent() {
    // Append the size to the path to keep concurrent/sequential tests isolated
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    Store<HashBytesCount> store(CreateNewOnDisk{}, temp_path);

    DeterministicHashGenerator<HashBytesCount> gen;
    auto inserted_hash = gen.make_random_hash();
    store.insert_hash(inserted_hash);

    auto missing_hash = gen.make_random_hash();

    EXPECT_EQ(store.ReverseLookupCandidate(missing_hash), GLOBAL_PI_NO_MATCH)
        << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";

    // Clean up directory after a successful test run
    std::filesystem::remove_all(temp_path);
}

template <size_t HashBytesCount>
static void RunTestSingleHashPresent() {
#    // Append the size to the path to keep concurrent/sequential tests isolated
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);
    Store<HashBytesCount> store(CreateNewOnDisk{}, temp_path);

    DeterministicHashGenerator<HashBytesCount> gen;
    auto inserted_hash = gen.make_random_hash();
    store.insert_hash(inserted_hash);

    EXPECT_EQ(store.ReverseLookupCandidate(inserted_hash), GlobalPi(0))
        << "Expected GlobalPi 0 for inserted hash (Hash size: " << HashBytesCount << "B)";

    // Clean up directory after a successful test run
    std::filesystem::remove_all(temp_path);
}

template <size_t HashBytesCount>
static void RunTest65535Hashes() {
    std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
    std::filesystem::remove_all(temp_path);
    Store<HashBytesCount> store(CreateNewOnDisk{}, temp_path);

    const size_t count = 65535;
    std::vector<HashBytes<HashBytesCount>> hash_array;
    hash_array.reserve(count);

    DeterministicHashGenerator<HashBytesCount> gen;

    for (size_t i = 0; i < count; ++i) {
        auto h = gen.make_random_hash();
        hash_array.push_back(h);
        store.insert_hash(h);
    }

    // Verify all 65,535 inserted hashes match their presentation indices
    for (size_t i = 0; i < count; ++i) {
        const auto& expected_has = hash_array[i];

        auto h = store.ForwardLookup(GlobalPi(i));
        auto ii = store.ReverseLookupCandidate(hash_array[i]);

        EXPECT_FALSE(ii.to_int() == GLOBAL_PI_NO_MATCH.to_int())
            << "Lookup failed for item " << i << " (Hash size: " << HashBytesCount << "B)";

        EXPECT_TRUE(h.value().bytes == hash_array[i].bytes);
        EXPECT_TRUE(ii.to_int() == i);
    }

    // Verify random non-existent hash returns NO_MATCH
    auto absent_hash = gen.make_random_hash();
    EXPECT_EQ(store.ReverseLookupCandidate(absent_hash), GLOBAL_PI_NO_MATCH);
}

// -----------------------------------------------------------------------------
// GoogleTest Suite Drivers
// -----------------------------------------------------------------------------

TEST(WeddingCakeAppTest, RunTestSingleHashAbsent) {
    RunTestSingleHashAbsent<20>();
    RunTestSingleHashAbsent<32>();
    RunTestSingleHashAbsent<64>();
}
TEST(WeddingCakeAppTest, RunTestSingleHashPresent) {
    RunTestSingleHashPresent<20>();
    RunTestSingleHashPresent<32>();
    RunTestSingleHashPresent<64>();
}
TEST(WeddingCakeAppTest, RunTestHashes65535) {
    RunTest65535Hashes<20>();
    RunTest65535Hashes<32>();
    RunTest65535Hashes<64>();
}

} // namespace wedding_cake
