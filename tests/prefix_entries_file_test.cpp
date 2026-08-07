#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

#include "wedding_cake/prefix_entries_file.hpp"
#include "wedding_cake/reader_writer_factory.hpp"

namespace wedding_cake {
namespace {

class PrefixEntriesFileTest : public ::testing::Test {
protected:
    std::filesystem::path base_test_dir;

    void SetUp() override {
        base_test_dir = std::filesystem::temp_directory_path() / "prefix_entries_test_dir";
        std::filesystem::remove_all(base_test_dir);
        std::filesystem::create_directories(base_test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(base_test_dir);
    }
};

TEST_F(PrefixEntriesFileTest, RoundTripWriteReadSequentialAndSeek) {
    const uint8_t hash_bytes = 16;
    const uint8_t tail_bits = 12;      // Requires ceil((128 - 12) / 8) = 15 prefix bytes
    const uint8_t local_pi_bytes = 3;
    const uint64_t sample_tail_index = 0x0ABC;

    // Instantiate factory for this configuration
    ReaderWriterFactory factory(base_test_dir, hash_bytes, local_pi_bytes, tail_bits);

    std::vector<uint8_t> hash_buf_1(hash_bytes, 0xAA); // Initial tail byte pattern 0xAA
    std::vector<uint8_t> hash_buf_2(hash_bytes, 0xBB); // Initial tail byte pattern 0xBB

    // Set MSB prefix data for Entry 1
    std::memset(hash_buf_1.data() + 1, 0x11, hash_bytes - 1);

    // Set MSB prefix data for Entry 2
    std::memset(hash_buf_2.data() + 1, 0x22, hash_bytes - 1);

    PrefixEntriesFile::Entry entry1{ .hash = HashView(hash_buf_1), .local_pi = LocalPi(100) };
    PrefixEntriesFile::Entry entry2{ .hash = HashView(hash_buf_2), .local_pi = LocalPi(20000) };

    // --- INITIALIZATION & WRITE PHASE ---
    {
        // 1. Create the empty file via Factory first
        factory.create_empty_prefix_entries_file(sample_tail_index);

        // 2. Open writer and append entries
        auto writer = factory.create_prefix_entries_file_writer(sample_tail_index);
        writer->append(entry1);
        writer->append(entry2);
        writer->flush();
    }

    // --- READ PHASE (Sequential) ---
    {
        auto reader = factory.create_prefix_entries_file_reader(sample_tail_index);

        // Pre-fill target hash buffer with dummy bytes (e.g., 0xFF) in lower tail bytes
        std::vector<uint8_t> read_buf(hash_bytes, 0xFF);
        LocalPi read_pi{0};

        // Read Entry 1
        ASSERT_TRUE(reader->has_more());
        ASSERT_TRUE(reader->read_next(read_buf, read_pi));

        EXPECT_EQ(read_pi, LocalPi(100));
        // Verify tail byte (index 0) was NOT overwritten and remains 0xFF
        EXPECT_EQ(read_buf[0], 0xFF);
        // Verify high-order prefix bytes were written
        EXPECT_EQ(read_buf[1], 0x11);

        // Verify we can inspect via HashView
        HashView read_view(read_buf);
        EXPECT_EQ(read_view[1], 0x11);

        // Read Entry 2
        ASSERT_TRUE(reader->has_more());
        ASSERT_TRUE(reader->read_next(read_buf, read_pi));

        EXPECT_EQ(read_pi, LocalPi(20000));
        EXPECT_EQ(read_buf[1], 0x22);

        // Verify EOF behavior
        EXPECT_FALSE(reader->has_more());
        EXPECT_FALSE(reader->read_next(read_buf, read_pi));
    }

    // --- READ PHASE (Random Access Seek) ---
    {
        auto reader = factory.create_prefix_entries_file_reader(sample_tail_index);

        std::vector<uint8_t> read_buf(hash_bytes, 0x00);
        LocalPi read_pi{0};

        // Seek directly to index 1 (Entry 2)
        ASSERT_TRUE(reader->seek_entry(1));
        ASSERT_TRUE(reader->read_next(read_buf, read_pi));
        EXPECT_EQ(read_pi, LocalPi(20000));
        EXPECT_EQ(read_buf[1], 0x22);

        // Seek back to index 0 (Entry 1)
        ASSERT_TRUE(reader->seek_entry(0));
        ASSERT_TRUE(reader->read_next(read_buf, read_pi));
        EXPECT_EQ(read_pi, LocalPi(100));
        EXPECT_EQ(read_buf[1], 0x11);
    }
}

TEST_F(PrefixEntriesFileTest, TruncateOnCreateEmpty) {
    const uint8_t hash_bytes = 8;
    const uint8_t tail_bits = 8;
    const uint8_t local_pi_bytes = 4;
    const uint64_t sample_tail_index = 0xFE;

    ReaderWriterFactory factory(base_test_dir, hash_bytes, local_pi_bytes, tail_bits);

    // Write initial 2 entries
    factory.create_empty_prefix_entries_file(sample_tail_index);
    {
        auto writer = factory.create_prefix_entries_file_writer(sample_tail_index);
        std::vector<uint8_t> buf(hash_bytes, 0x01);
        writer->append({ .hash = HashView(buf), .local_pi = LocalPi(1) });
        writer->append({ .hash = HashView(buf), .local_pi = LocalPi(2) });
        writer->flush();
    }

    // Re-creating empty file on the same tail index must truncate/reset state
    factory.create_empty_prefix_entries_file(sample_tail_index);

    // Append 1 new entry
    {
        auto writer = factory.create_prefix_entries_file_writer(sample_tail_index);
        std::vector<uint8_t> buf(hash_bytes, 0x02);
        writer->append({ .hash = HashView(buf), .local_pi = LocalPi(99) });
        writer->flush();
    }

    // Read back and verify only the newly appended entry exists
    auto reader = factory.create_prefix_entries_file_reader(sample_tail_index);
    std::vector<uint8_t> read_buf(hash_bytes, 0x00);
    LocalPi read_pi{0};

    ASSERT_TRUE(reader->read_next(read_buf, read_pi));
    EXPECT_EQ(read_pi, LocalPi(99));
    EXPECT_FALSE(reader->has_more());
}

TEST_F(PrefixEntriesFileTest, AppendAcrossMultipleWriterSessions) {
    const uint8_t hash_bytes = 8;
    const uint8_t tail_bits = 8;
    const uint8_t local_pi_bytes = 4;
    const uint64_t sample_tail_index = 0x1A;

    ReaderWriterFactory factory(base_test_dir, hash_bytes, local_pi_bytes, tail_bits);

    // Single empty file initialization
    factory.create_empty_prefix_entries_file(sample_tail_index);

    // Session 1 write
    {
        auto writer = factory.create_prefix_entries_file_writer(sample_tail_index);
        std::vector<uint8_t> buf(hash_bytes, 0x01);
        writer->append({ .hash = HashView(buf), .local_pi = LocalPi(10) });
        writer->flush();
    }

    // Session 2 write (Appends to the same file)
    {
        auto writer = factory.create_prefix_entries_file_writer(sample_tail_index);
        std::vector<uint8_t> buf(hash_bytes, 0x02);
        writer->append({ .hash = HashView(buf), .local_pi = LocalPi(20) });
        writer->flush();
    }

    // Verify both entries exist sequentially
    auto reader = factory.create_prefix_entries_file_reader(sample_tail_index);
    std::vector<uint8_t> read_buf(hash_bytes, 0x00);
    LocalPi read_pi{0};

    ASSERT_TRUE(reader->has_more());
    ASSERT_TRUE(reader->read_next(read_buf, read_pi));
    EXPECT_EQ(read_pi, LocalPi(10));

    ASSERT_TRUE(reader->has_more());
    ASSERT_TRUE(reader->read_next(read_buf, read_pi));
    EXPECT_EQ(read_pi, LocalPi(20));

    EXPECT_FALSE(reader->has_more());
}

TEST_F(PrefixEntriesFileTest, InvalidConstructorArguments) {
    // Zero hash bytes
    EXPECT_THROW(ReaderWriterFactory(base_test_dir, 0, 0, 4), std::invalid_argument);

    // tail_bits_count >= total hash bits (16 bytes * 8 = 128 bits)
    EXPECT_THROW(ReaderWriterFactory(base_test_dir, 16, 128, 4), std::invalid_argument);

    // Zero local_pi_bytes
    EXPECT_THROW(ReaderWriterFactory(base_test_dir, 16, 0, 0), std::invalid_argument);

    // local_pi_bytes > 8
    EXPECT_THROW(ReaderWriterFactory(base_test_dir, 16, 0, 9), std::invalid_argument);
}

} // namespace
} // namespace wedding_cake