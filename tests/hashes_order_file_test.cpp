#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <cstdint>

#include "wedding_cake/hashes_order_file.hpp"

namespace wedding_cake {
namespace {

class HashesOrderFileTest : public ::testing::Test {
protected:
    std::filesystem::path test_file_path;

    void SetUp() override {
        // Use a temporary path for test files
        test_file_path = std::filesystem::temp_directory_path() / "test_hashes_order.bin";
        std::filesystem::remove(test_file_path); // Ensure clean state
    }

    void TearDown() override {
        // Cleanup after test completes
        std::filesystem::remove(test_file_path);
    }
};

TEST_F(HashesOrderFileTest, WriteAndReadVariousConfigurations) {
    // Combinations of (local_pi_bytes, tail_bits_count)
    const std::vector<uint8_t> local_pi_sizes = {1, 3, 4, 8};
    const std::vector<uint8_t> tail_bits_sizes = {0, 1, 7, 8, 12, 16};

    for (uint8_t pi_bytes : local_pi_sizes) {
        for (uint8_t tail_bits : tail_bits_sizes) {
            
            // Build a set of diverse test entries
            std::vector<HashesOrderFile::Entry> expected_entries;

            // 1. Minimum / Zero values
            expected_entries.push_back({
                .local_pi = LocalPi(0),
                .tail_bits = 0,
                .spare_bits = 0
            });

            // 2. Small values
            expected_entries.push_back({
                .local_pi = LocalPi(42),
                .tail_bits = (tail_bits > 0) ? 1ULL : 0ULL,
                .spare_bits = 2ULL
            });

            // 3. Values at edge of PI byte size
            uint64_t max_pi_val = (pi_bytes == 8) ? (UINT64_MAX - 1) : ((1ULL << (pi_bytes * 8)) - 2);
            expected_entries.push_back({
                .local_pi = LocalPi(max_pi_val),
                .tail_bits = (tail_bits > 0) ? ((1ULL << tail_bits) - 1) : 0ULL,
                .spare_bits = 5ULL
            });

            // 4. Sentinel value (LOCAL_PI_NO_MATCH) with spare_bits
            expected_entries.push_back({
                .local_pi = LOCAL_PI_NO_MATCH,
                .tail_bits = (tail_bits > 0) ? 0b1010101010101010ULL : 0ULL,
                .spare_bits = 0b111ULL
            });

            // --- WRITE PHASE ---
            {
                HashesOrderFileWriter writer(test_file_path, pi_bytes, tail_bits);
                for (const auto& entry : expected_entries) {
                    writer.append(entry);
                }
                writer.flush();
            }

            // --- READ PHASE (Sequential) ---
            {
                HashesOrderFileReader reader(test_file_path, pi_bytes, tail_bits);
                
                uint8_t expected_spare_count = (tail_bits == 0) ? 0 : reader.spare_bits_count();

                for (size_t i = 0; i < expected_entries.size(); ++i) {
                    auto read_entry = reader.read_next();
                    const auto& expected = expected_entries[i];

                    EXPECT_EQ(read_entry.local_pi, expected.local_pi)
                        << "Mismatch at index " << i << " (PI Bytes: " << (int)pi_bytes << ", Tail Bits: " << (int)tail_bits << ")";

                    // Mask expected values to available bit capacities for comparison
                    uint64_t tail_mask = (tail_bits == 64) ? ~0ULL : ((1ULL << tail_bits) - 1);
                    uint64_t expected_tail = (tail_bits > 0) ? (expected.tail_bits & tail_mask) : 0ULL;
                    EXPECT_EQ(read_entry.tail_bits, expected_tail)
                        << "Tail bits mismatch at index " << i << " (PI Bytes: " << (int)pi_bytes << ", Tail Bits: " << (int)tail_bits << ")";

                    if (expected_spare_count > 0) {
                        uint64_t spare_mask = (expected_spare_count == 64) ? ~0ULL : ((1ULL << expected_spare_count) - 1);
                        EXPECT_EQ(read_entry.spare_bits, expected.spare_bits & spare_mask)
                            << "Spare bits mismatch at index " << i << " (PI Bytes: " << (int)pi_bytes << ", Tail Bits: " << (int)tail_bits << ")";
                    }
                }
            }

            // --- READ PHASE (Random Access via seek_entry) ---
            {
                HashesOrderFileReader reader(test_file_path, pi_bytes, tail_bits);

                // Seek backwards from the last entry to the first
                for (int i = static_cast<int>(expected_entries.size()) - 1; i >= 0; --i) {
                    ASSERT_TRUE(reader.seek_entry(i)) << "seek_entry failed for index " << i;
                    
                    auto read_entry = reader.read_next();
                    const auto& expected = expected_entries[i];

                    EXPECT_EQ(read_entry.local_pi, expected.local_pi)
                        << "Seek read mismatch at index " << i;
                }
            }
        }
    }
}

TEST_F(HashesOrderFileTest, TruncateOnWriterCreation) {
    // 1. Write initial file with 5 entries
    {
        HashesOrderFileWriter writer(test_file_path, 4, 8);
        for (int i = 0; i < 5; ++i) {
            writer.append({.local_pi = LocalPi(i), .tail_bits = 0, .spare_bits = 0});
        }
    }

    // Check size on disk
    auto initial_size = std::filesystem::file_size(test_file_path);
    EXPECT_GT(initial_size, 0u);

    // 2. Open new writer over the same path (should truncate)
    {
        HashesOrderFileWriter writer(test_file_path, 4, 8);
        writer.append({.local_pi = LocalPi(99), .tail_bits = 0, .spare_bits = 0});
    }

    // 3. Verify file size was truncated down to only 1 entry
    HashesOrderFileReader reader(test_file_path, 4, 8);
    auto entry = reader.read_next();
    EXPECT_EQ(entry.local_pi, LocalPi(99));

    // Next read should hit EOF
    auto eof_entry = reader.read_next();
    EXPECT_TRUE(eof_entry.local_pi.is_no_match());
}

} // namespace
} // namespace wedding_cake