#include <gtest/gtest.h>
#include <vector>
#include <array>
#include <random>
#include <span>
#include <cstdint>
#include <algorithm>
#include <limits>

#include "../apps/wedding_cake/include/types.hpp"

// The code we are testing
#include "../include/wedding_cake/shallow_tree.hpp"

namespace wedding_cake {

// -----------------------------------------------------------------------------
// Constants & Sentinel Values
// -----------------------------------------------------------------------------
constexpr size_t REASSURANCE_BYTES_COUNT = 2;
constexpr uint64_t NO_MATCH = std::numeric_limits<uint64_t>::max();

// Fixed-seed PRNG for deterministic, reproducible test runs
class DeterministicHashGenerator {
    std::mt19937_64 rng_{42}; // Fixed seed 42
public:
    std::vector<uint8_t> make_random_hash(size_t num_bytes) {
        std::vector<uint8_t> bytes(num_bytes);
        for (size_t i = 0; i < num_bytes; ++i) {
            bytes[i] = static_cast<uint8_t>(rng_() & 0xFF);
        }
        return bytes;
    }

    template <size_t HashBytesCount>
    std::array<uint8_t, HashBytesCount> make_random_hash_array() {
        std::array<uint8_t, HashBytesCount> bytes;
        for (size_t i = 0; i < HashBytesCount; ++i) {
            bytes[i] = static_cast<uint8_t>(rng_() & 0xFF);
        }
        return bytes;
    }
};

// -----------------------------------------------------------------------------
// Templated Test Helper Functions (Compile-time Strategy)
// -----------------------------------------------------------------------------

template <size_t HashBytesCount>
void RunTestEmptyTree(DeterministicHashGenerator& gen) {
    for (size_t ls_tail_bits = 0; ls_tail_bits <= 16; ls_tail_bits += 4) {
        std::vector<HashLocalPresentation<HashBytesCount>> empty_entries;

        ShallowTree<HashBytesCount> st(empty_entries, ls_tail_bits, REASSURANCE_BYTES_COUNT);

        auto search_hash_bytes = gen.make_random_hash_array<HashBytesCount>();
        HashView search_hash(search_hash_bytes);

        EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(search_hash), LOCAL_PI_NO_MATCH)
            << "Lookup should return NO_MATCH for an empty tree (Hash size: " << HashBytesCount << "B)";
    }
}

template <size_t HashBytesCount>
void RunTestSingleHashAbsent(DeterministicHashGenerator& gen) {
    for (size_t ls_tail_bits = 0; ls_tail_bits <= 16; ls_tail_bits += 4) {
        auto inserted_hash = gen.make_random_hash_array<HashBytesCount>();

        std::vector<HashLocalPresentation<HashBytesCount>> entries = { {inserted_hash, LocalPi(0)} };
        ShallowTree<HashBytesCount> st(entries, ls_tail_bits, REASSURANCE_BYTES_COUNT);

        auto missing_hash = gen.make_random_hash_array<HashBytesCount>();
        EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(HashView(missing_hash)), LOCAL_PI_NO_MATCH)
            << "Expected NO_MATCH for absent hash (Hash size: " << HashBytesCount << "B)";
    }
}

template <size_t HashBytesCount>
void RunTestSingleHashPresent(DeterministicHashGenerator& gen) {
    for (size_t ls_tail_bits = 0; ls_tail_bits <= 16; ls_tail_bits += 4) {
        auto hash = gen.make_random_hash_array<HashBytesCount>();

        std::vector<HashLocalPresentation<HashBytesCount>> entries = { {hash, LocalPi(0)} };
        // Pass HashBytes as the 3rd argument
        ShallowTree<HashBytesCount> st(entries, ls_tail_bits, REASSURANCE_BYTES_COUNT);

        EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(HashView(hash)), LocalPi(0))
            << "Expected presentation index 0 for present hash (Hash size: " << HashBytesCount << "B)";
    }
}

template <size_t HashBytesCount>
void RunTest65535Hashes(DeterministicHashGenerator& gen) {
    const size_t count = 65535;
    const size_t ls_tail_bits = 8; // 1 byte already handled by LsTailIndex

    std::vector<HashLocalPresentation<HashBytesCount>> presentation_array;
    presentation_array.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        presentation_array.push_back({
            gen.make_random_hash_array<HashBytesCount>(),
            LocalPi(i)
        });
    }
    auto working_array = presentation_array; // Copy for tree generation
    ShallowTree<HashBytesCount> st(working_array, ls_tail_bits, REASSURANCE_BYTES_COUNT);

    // Verify all 65,535 inserted hashes match their presentation indices
    for (size_t i = 0; i < count; ++i) {
        const auto& expected_item = presentation_array[i];
        LocalPi local_pi = st.lookup_hash_unique_reassured_candidate(expected_item.view());
        uint64_t found_index = local_pi.to_int();

        ASSERT_NE(local_pi, LOCAL_PI_NO_MATCH)
            << "Lookup failed for item " << i << " (Hash size: " << HashBytesCount << "B)";

        EXPECT_EQ(local_pi, expected_item.presentation_index);
        EXPECT_EQ(presentation_array[found_index].hash.bytes, expected_item.hash.bytes);
    }

    // Verify random non-existent hash returns NO_MATCH
    auto absent_hash = gen.make_random_hash_array<HashBytesCount>();
    EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(HashView(absent_hash)), LOCAL_PI_NO_MATCH);
}

// -----------------------------------------------------------------------------
// GoogleTest Suite Drivers
// -----------------------------------------------------------------------------

TEST(ShallowTreeTest, TestEmptyTree) {
    DeterministicHashGenerator gen;
    RunTestEmptyTree<20>(gen);
    RunTestEmptyTree<32>(gen);
    RunTestEmptyTree<64>(gen);
}

TEST(ShallowTreeTest, TestSingleHashPresent) {
    DeterministicHashGenerator gen;
    RunTestSingleHashPresent<20>(gen);
    RunTestSingleHashPresent<32>(gen);
    RunTestSingleHashPresent<64>(gen);
}

TEST(ShallowTreeTest, TestSingleHashAbsent) {
    DeterministicHashGenerator gen;
    RunTestSingleHashAbsent<20>(gen);
    RunTestSingleHashAbsent<32>(gen);
    RunTestSingleHashAbsent<64>(gen);
}

TEST(ShallowTreeTest, Test65535Hashes) {
    DeterministicHashGenerator gen;
    RunTest65535Hashes<20>(gen);
    RunTest65535Hashes<32>(gen);
    RunTest65535Hashes<64>(gen);
}

} // namespace wedding_cake
