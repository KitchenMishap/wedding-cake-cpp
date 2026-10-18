#include <filesystem>
#include <gtest/gtest.h>
#include <store.hpp>

// A test suite for the wedding cake app
TEST(WeddingCakeAppTest, CreateEmpty) {
    std::filesystem::path temp_path = "TempTesting";
    std::filesystem::remove_all(temp_path);

    Store store;
    store.CreateEmpty(temp_path);
    EXPECT_TRUE(std::filesystem::exists(temp_path));
    EXPECT_TRUE(std::filesystem::exists(temp_path / "Tier0"));
    EXPECT_TRUE(std::filesystem::exists(temp_path / "Tier0" / "Offset_0"));
    EXPECT_TRUE(std::filesystem::exists(temp_path / "Tier0" / "Offset_0" / "ST00_Sack"));
}
