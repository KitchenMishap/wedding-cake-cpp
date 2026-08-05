#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <cstring>
#include <span>

#include "wedding_cake/prefix_entries_file.hpp"

namespace wedding_cake {
namespace {

class PrefixEntriesFileTest : public ::testing::Test {
protected:
    std::filesystem::path test_file_path;

    void SetUp() override {
        test_file_path = std::filesystem::temp_directory_path() / "test_prefix_entries.bin";
        std::filesystem::remove(test_file_path);
    }

    void TearDown() override {
        std::filesystem::remove(test_file_path);
    }
};

TEST_F(PrefixEntriesFileTest, RoundTripWriteReadSequentialAndSeek) {
    const uint8_t hash_bytes = 16;
    const uint8_t tail_bits = 12;      // Requires ceil((128 - 12) / 8) = 15 prefix bytes
    const uint8_t local_pi_bytes = 3;

    std::vector<uint8_t> hash_buf_1(hash_bytes, 0xAA); // Initial tail byte pattern 0xAA
    std::vector<uint8_t> hash_buf_2(hash_bytes, 0xBB); // Initial tail byte pattern 0xBB

    // Set MSB prefix data for Entry 1
    std::memset(hash_buf_1.data() + 1, 0x11, hash_bytes - 1);

    // Set MSB prefix data for Entry 2
    std::memset(hash_buf_2.data() + 1, 0x22, hash_bytes - 1);

    PrefixEntriesFile::Entry entry1{ .hash = HashView(hash_buf_1), .local_pi = LocalPi(100) };
    PrefixEntriesFile::Entry entry2{ .hash = HashView(hash_buf_2), .local_pi = LocalPi(20000) };

    // --- WRITE PHASE ---
    {
        PrefixEntriesFileWriter writer(test_file_path, hash_bytes, tail_bits, local_pi_bytes);
        writer.append(entry1);
        writer.append(entry2);
        writer.flush();
    }

    // --- READ PHASE (Sequential) ---
    {
        PrefixEntriesFileReader reader(test_file_path, hash_bytes, tail_bits, local_pi_bytes);

        // Pre-fill target hash buffer with dummy bytes (e.g., 0xFF) in lower tail bytes
        std::vector<uint8_t> read_buf(hash_bytes, 0xFF);
        LocalPi read_pi{0};

        // Read Entry 1
        ASSERT_TRUE(reader.has_more());
        ASSERT_TRUE(reader.read_next(read_buf, read_pi));

        EXPECT_EQ(read_pi, LocalPi(100));
        // Verify tail byte (index 0) was NOT overwritten and remains 0xFF
        EXPECT_EQ(read_buf[0], 0xFF);
        // Verify high-order prefix bytes were written
        EXPECT_EQ(read_buf[1], 0x11);

        // Verify we can inspect via HashView
        HashView read_view(read_buf);
        EXPECT_EQ(read_view[1], 0x11);

        // Read Entry 2
        ASSERT_TRUE(reader.has_more());
        ASSERT_TRUE(reader.read_next(read_buf, read_pi));

        EXPECT_EQ(read_pi, LocalPi(20000));
        EXPECT_EQ(read_buf[1], 0x22);

        // Verify EOF behavior
        EXPECT_FALSE(reader.has_more());
        EXPECT_FALSE(reader.read_next(read_buf, read_pi));
    }

    // --- READ PHASE (Random Access Seek) ---
    {
        PrefixEntriesFileReader reader(test_file_path, hash_bytes, tail_bits, local_pi_bytes);

        std::vector<uint8_t> read_buf(hash_bytes, 0x00);
        LocalPi read_pi{0};

        // Seek directly to index 1 (Entry 2)
        ASSERT_TRUE(reader.seek_entry(1));
        ASSERT_TRUE(reader.read_next(read_buf, read_pi));
        EXPECT_EQ(read_pi, LocalPi(20000));
        EXPECT_EQ(read_buf[1], 0x22);

        // Seek back to index 0 (Entry 1)
        ASSERT_TRUE(reader.seek_entry(0));
        ASSERT_TRUE(reader.read_next(read_buf, read_pi));
        EXPECT_EQ(read_pi, LocalPi(100));
        EXPECT_EQ(read_buf[1], 0x11);
    }
}

TEST_F(PrefixEntriesFileTest, TruncateOnWriterOpen) {
    // Write 2 entries
    {
        PrefixEntriesFileWriter writer(test_file_path, 8, 0, 4);
        std::vector<uint8_t> buf(8, 0x01);
        writer.append({ .hash = HashView(buf), .local_pi = LocalPi(1) });
        writer.append({ .hash = HashView(buf), .local_pi = LocalPi(2) });
    }

    // Opening a new writer on the same path must truncate
    {
        PrefixEntriesFileWriter writer(test_file_path, 8, 0, 4);
        std::vector<uint8_t> buf(8, 0x02);
        writer.append({ .hash = HashView(buf), .local_pi = LocalPi(99) });
    }

    // Read back and verify only 1 entry exists
    PrefixEntriesFileReader reader(test_file_path, 8, 0, 4);
    std::vector<uint8_t> read_buf(8, 0x00);
    LocalPi read_pi{0};

    ASSERT_TRUE(reader.read_next(read_buf, read_pi));
    EXPECT_EQ(read_pi, LocalPi(99));
    EXPECT_FALSE(reader.has_more());
}

TEST_F(PrefixEntriesFileTest, InvalidConstructorArguments) {
    // Zero hash bytes
    EXPECT_THROW(PrefixEntriesFileWriter(test_file_path, 0, 0, 4), std::invalid_argument);

    // tail_bits_count >= total hash bits (16 bytes * 8 = 128 bits)
    EXPECT_THROW(PrefixEntriesFileWriter(test_file_path, 16, 128, 4), std::invalid_argument);

    // Zero local_pi_bytes
    EXPECT_THROW(PrefixEntriesFileWriter(test_file_path, 16, 0, 0), std::invalid_argument);

    // local_pi_bytes > 8
    EXPECT_THROW(PrefixEntriesFileWriter(test_file_path, 16, 0, 9), std::invalid_argument);
}

} // namespace
} // namespace wedding_cake