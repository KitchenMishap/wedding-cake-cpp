#include <gtest/gtest.h>
#include <filesystem>
#include <stdexcept>
#include "wedding_cake/prefix_entries_path.hpp"

namespace wedding_cake {
namespace test {

class DigitsPathTest : public ::testing::Test {
protected:
    const std::filesystem::path base_dir_ = "/tmp/test_dir";
};

// ============================================================================
// BASIC DEPTH TESTS
// ============================================================================

TEST_F(DigitsPathTest, ZeroTailBitsReturnsSingleFile) {
    const auto path = make_prefix_entries_path(base_dir_, 0, 0x1234);
    EXPECT_EQ(path, base_dir_ / "TailIndices" / "PrefixEntries");
}

TEST_F(DigitsPathTest, UpTo8TailBitsCreatesSingleFolderLevel) {
    // 8 tail bits: 1 byte level
    const auto path1 = make_prefix_entries_path(base_dir_, 8, 0x0A);
    EXPECT_EQ(path1, base_dir_ / "TailIndices" / "0A_PrefixEntries");

    // 4 tail bits: still 1 level
    const auto path2 = make_prefix_entries_path(base_dir_, 4, 0x0F);
    EXPECT_EQ(path2, base_dir_ / "TailIndices" / "0F_PrefixEntries");
}

TEST_F(DigitsPathTest, UpTo16TailBitsCreatesTwoLevels) {
    // 12 tail bits -> 2 levels (e.g. 0x0ABC)
    const auto path1 = make_prefix_entries_path(base_dir_, 12, 0x0ABC);
    EXPECT_EQ(path1, base_dir_ / "TailIndices" / "0A" / "BC_PrefixEntries");

    // 16 tail bits -> 2 levels
    const auto path2 = make_prefix_entries_path(base_dir_, 16, 0x1234);
    EXPECT_EQ(path2, base_dir_ / "TailIndices" / "12" / "34_PrefixEntries");
}

TEST_F(DigitsPathTest, UpTo24TailBitsCreatesThreeLevels) {
    const auto path = make_prefix_entries_path(base_dir_, 24, 0x123456);
    EXPECT_EQ(path, base_dir_ / "TailIndices" / "12" / "34" / "56_PrefixEntries");
}

// ============================================================================
// EDGE CASES & BIT MASKING
// ============================================================================

TEST_F(DigitsPathTest, PadsSingleDigitHexWithZero) {
    // 0x05 should produce "05" instead of "5"
    const auto path = make_prefix_entries_path(base_dir_, 16, 0x0509);
    EXPECT_EQ(path, base_dir_ / "TailIndices" / "05" / "09_PrefixEntries");
}

TEST_F(DigitsPathTest, MasksUnusedUpperBitsOfTailIndex) {
    // For 12 tail bits, max valid index is 0xFFF.
    // Passing 0xFABC should ignore the top 'F' and treat as 0xABC.
    const auto path = make_prefix_entries_path(base_dir_, 12, 0xFABC);
    EXPECT_EQ(path, base_dir_ / "TailIndices" / "0A" / "BC_PrefixEntries");
}

TEST_F(DigitsPathTest, Handles64TailBits) {
    const uint64_t full_index = 0x0123456789ABCDEF;
    const auto path = make_prefix_entries_path(base_dir_, 64, full_index);

    const auto expected = base_dir_ / "TailIndices" 
                          / "01" / "23" / "45" / "67" 
                          / "89" / "AB" / "CD" / "EF_PrefixEntries";
    EXPECT_EQ(path, expected);
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

TEST_F(DigitsPathTest, ThrowsOnTailBitsExceeding64) {
    EXPECT_THROW(make_prefix_entries_path(base_dir_, 65, 0), std::invalid_argument);
    EXPECT_THROW(make_prefix_entries_path(base_dir_, 255, 0), std::invalid_argument);
}

} // namespace test
} // namespace wedding_cake