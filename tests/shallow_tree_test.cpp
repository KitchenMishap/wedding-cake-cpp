#include <gtest/gtest.h>
#include <vector>
#include <array>
#include <random>
#include <span>
#include <cstdint>
#include <algorithm>
#include <limits>

#include "HashTypes.hpp"

// The code we are testing
#include "ShallowTree.hpp"

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
};

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

// Test sizes: 20 bytes (160-bit), 32 bytes (256-bit), 64 bytes (512-bit)
const std::array<size_t, 3> TEST_HASH_SIZES = {20, 32, 64};

TEST(ShallowTreeTest, TestEmptyTree) {
    DeterministicHashGenerator gen;

    for (size_t ls_tail_bits = 0; ls_tail_bits <= 16; ls_tail_bits += 4) {
        for (size_t hash_bytes : TEST_HASH_SIZES) {
            std::vector<HashPresentation> empty_entries;

            ShallowTree st(empty_entries, ls_tail_bits, hash_bytes, REASSURANCE_BYTES_COUNT);

            auto search_hash_bytes = gen.make_random_hash(hash_bytes);
            HashView search_hash(search_hash_bytes);

            EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(search_hash), LOCAL_PI_NO_MATCH)
                << "Lookup should return NO_MATCH for an empty tree (Hash size: " << hash_bytes << "B)";
        }
    }
}

TEST(ShallowTreeTest, TestSingleHashPresent) {
    DeterministicHashGenerator gen;

    for (size_t ls_tail_bits = 0; ls_tail_bits <= 16; ls_tail_bits += 4) {
        for (size_t hash_bytes : TEST_HASH_SIZES) {
            auto hash = gen.make_random_hash(hash_bytes);

            std::vector<HashPresentation> entries = { {hash, LocalPi(0)} };
            ShallowTree st(entries, ls_tail_bits, hash_bytes, REASSURANCE_BYTES_COUNT);

            EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(HashView(hash)), LocalPi(0))
                << "Expected presentation index 0 for present hash";
        }
    }
}

TEST(ShallowTreeTest, TestSingleHashAbsent) {
    DeterministicHashGenerator gen;

    for (size_t ls_tail_bits = 0; ls_tail_bits <= 16; ls_tail_bits += 4) {
        for (size_t hash_bytes : TEST_HASH_SIZES) {
            auto inserted_hash = gen.make_random_hash(hash_bytes);

            std::vector<HashPresentation> entries = { {inserted_hash, LocalPi(0)} };
            ShallowTree st(entries, ls_tail_bits, hash_bytes, REASSURANCE_BYTES_COUNT);

            auto missing_hash = gen.make_random_hash(hash_bytes);
            EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(HashView(missing_hash)), LOCAL_PI_NO_MATCH)
                << "Expected NO_MATCH for absent hash";
        }
    }
}

TEST(ShallowTreeTest, Test65535Hashes) {
    const size_t count = 65535;
    const size_t ls_tail_bits = 8; // 1 byte already handled by LsTailIndex

    DeterministicHashGenerator gen;

    for (size_t hash_bytes : TEST_HASH_SIZES) {
        std::vector<HashPresentation> presentation_array;
        presentation_array.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            presentation_array.push_back({
                gen.make_random_hash(hash_bytes),
                LocalPi(i)
            });
        }
        auto working_array = presentation_array; // Copy for tree generation
        ShallowTree st(working_array, ls_tail_bits, hash_bytes, REASSURANCE_BYTES_COUNT);

        // Verify all 65,535 inserted hashes match their presentation indices
        for (size_t i = 0; i < count; ++i) {
            const auto& expected_item = presentation_array[i];
            LocalPi local_pi = st.lookup_hash_unique_reassured_candidate(expected_item.view());
            uint64_t found_index = local_pi.to_int();

            ASSERT_NE(local_pi, LOCAL_PI_NO_MATCH)
                << "Lookup failed for item " << i << " (Hash size: " << hash_bytes << "B)";

            EXPECT_EQ(local_pi, expected_item.presentation_index);
            EXPECT_EQ(presentation_array[found_index].hash_bytes, expected_item.hash_bytes);
        }

        // Verify random non-existent hash returns NO_MATCH
        auto absent_hash = gen.make_random_hash(hash_bytes);
        EXPECT_EQ(st.lookup_hash_unique_reassured_candidate(HashView(absent_hash)), LOCAL_PI_NO_MATCH);
    }
}

} // namespace wedding_cake