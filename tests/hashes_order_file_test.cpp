#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <system_error>

#include "wedding_cake/hashes_order_file.hpp"
#include "wedding_cake/reader_writer_factory.hpp"

namespace wedding_cake {
namespace {

class HashesOrderFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "hashes_order_file_tests";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
};

// -----------------------------------------------------------------------------
// Test 1: Standard Writer & Reader Cycle with create_empty
// -----------------------------------------------------------------------------
TEST_F(HashesOrderFileTest, WriteAndReadEntries) {
    constexpr uint8_t hash_bytes = 8;
    constexpr uint8_t tail_bits = 13;
    constexpr uint8_t pi_bytes = 3;

    ReaderWriterFactory factory(test_dir_, hash_bytes, pi_bytes, tail_bits);

    // 1. Create the empty file explicitly
    factory.create_empty_hashes_order_file();

    // 2. Obtain writer and write records
    auto writer = factory.create_hashes_order_file_writer();

    std::vector<HashesOrderFile::Entry> expected_entries = {
        { LocalPi(0x00123456ULL), 0x1F05ULL, 0x01 },
        { LocalPi(0x00ABCDEFULL), 0x0123ULL, 0x02 },
        { LOCAL_PI_NO_MATCH,      0x1FFFULL, 0x03 },
    };

    for (const auto& entry : expected_entries) {
        writer->append(entry);
    }
    writer->flush();

    // 3. Obtain reader and verify contents
    auto reader = factory.create_hashes_order_file_reader();

    for (size_t i = 0; i < expected_entries.size(); ++i) {
        ASSERT_TRUE(reader->has_more()) << "Expected entry at index " << i;
        auto actual = reader->read_next();

        EXPECT_EQ(actual.local_pi, expected_entries[i].local_pi) << "Mismatch at index " << i;
        EXPECT_EQ(actual.tail_bits, expected_entries[i].tail_bits) << "Mismatch at index " << i;
        EXPECT_EQ(actual.spare_bits, expected_entries[i].spare_bits) << "Mismatch at index " << i;
    }

    EXPECT_FALSE(reader->has_more());
}

// -----------------------------------------------------------------------------
// Test 2: Random Access / Seek using the Factory
// -----------------------------------------------------------------------------
TEST_F(HashesOrderFileTest, RandomAccessSeekEntry) {
    constexpr uint8_t hash_bytes = 8;
    constexpr uint8_t tail_bits = 8;
    constexpr uint8_t pi_bytes = 4;

    ReaderWriterFactory factory(test_dir_, hash_bytes, pi_bytes, tail_bits);

    factory.create_empty_hashes_order_file();
    auto writer = factory.create_hashes_order_file_writer();

    std::vector<HashesOrderFile::Entry> entries = {
        { LocalPi(100), 0xAA, 0x0 },
        { LocalPi(200), 0xBB, 0x0 },
        { LocalPi(300), 0xCC, 0x0 },
    };

    for (const auto& entry : entries) {
        writer->append(entry);
    }
    writer->flush();

    auto reader = factory.create_hashes_order_file_reader();

    // Seek to index 1 (second entry)
    ASSERT_TRUE(reader->seek_entry(1));
    auto entry1 = reader->read_next();
    EXPECT_EQ(entry1.local_pi, entries[1].local_pi);
    EXPECT_EQ(entry1.tail_bits, entries[1].tail_bits);

    // Seek back to index 0 (first entry)
    ASSERT_TRUE(reader->seek_entry(0));
    auto entry0 = reader->read_next();
    EXPECT_EQ(entry0.local_pi, entries[0].local_pi);
    EXPECT_EQ(entry0.tail_bits, entries[0].tail_bits);
}

// -----------------------------------------------------------------------------
// Test 3: Multiple Write Sessions via std::ios::app Behavior
// -----------------------------------------------------------------------------
TEST_F(HashesOrderFileTest, AppendAcrossMultipleWriters) {
    constexpr uint8_t hash_bytes = 8;
    constexpr uint8_t tail_bits = 6;
    constexpr uint8_t pi_bytes = 2;

    ReaderWriterFactory factory(test_dir_, hash_bytes, pi_bytes, tail_bits);

    // Initial empty file creation
    factory.create_empty_hashes_order_file();

    // Writer session 1
    {
        auto writer1 = factory.create_hashes_order_file_writer();
        writer1->append({ LocalPi(10), 0x01, 0 });
        writer1->flush();
    }

    // Writer session 2 (appends to existing file because mode is std::ios::app)
    {
        auto writer2 = factory.create_hashes_order_file_writer();
        writer2->append({ LocalPi(20), 0x02, 0 });
        writer2->flush();
    }

    // Verify both records exist sequentially
    auto reader = factory.create_hashes_order_file_reader();

    ASSERT_TRUE(reader->has_more());
    auto entry1 = reader->read_next();
    EXPECT_EQ(entry1.local_pi, LocalPi(10));

    ASSERT_TRUE(reader->has_more());
    auto entry2 = reader->read_next();
    EXPECT_EQ(entry2.local_pi, LocalPi(20));

    EXPECT_FALSE(reader->has_more());
}

// -----------------------------------------------------------------------------
// Test 4: Re-initialization via create_empty Wipes Previous State
// -----------------------------------------------------------------------------
TEST_F(HashesOrderFileTest, ExplicitResetViaCreateEmpty) {
    constexpr uint8_t hash_bytes = 8;
    constexpr uint8_t tail_bits = 6;
    constexpr uint8_t pi_bytes = 2;

    ReaderWriterFactory factory(test_dir_, hash_bytes, pi_bytes, tail_bits);

    // First write sequence
    factory.create_empty_hashes_order_file();
    {
        auto writer = factory.create_hashes_order_file_writer();
        writer->append({ LocalPi(100), 0x05, 0 });
        writer->append({ LocalPi(200), 0x0A, 0 });
        writer->flush();
    }

    // Explicitly reset/truncate via factory method
    factory.create_empty_hashes_order_file();

    // Write new content
    {
        auto writer = factory.create_hashes_order_file_writer();
        writer->append({ LocalPi(999), 0x0F, 0 });
        writer->flush();
    }

    // Read back: only the new record must remain
    auto reader = factory.create_hashes_order_file_reader();

    ASSERT_TRUE(reader->has_more());
    auto entry = reader->read_next();
    EXPECT_EQ(entry.local_pi, LocalPi(999));

    EXPECT_FALSE(reader->has_more());
}

} // namespace
} // namespace wedding_cake