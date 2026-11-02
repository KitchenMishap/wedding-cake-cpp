#include <filesystem>
#include <gtest/gtest.h>

#include "store_b.hpp"

namespace wedding_cake {

    // -----------------------------------------------------------------------------
    // Templated Test Helper Functions
    // -----------------------------------------------------------------------------

    template <size_t HashBytesCount>
    static void RunTestCreateEmptyB() {
        // Append the size to the path to keep concurrent/sequential tests isolated
        std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
        std::filesystem::remove_all(temp_path);

        StoreB<HashBytesCount> store_b(temp_path);

        EXPECT_TRUE(std::filesystem::exists(temp_path));
        EXPECT_TRUE(std::filesystem::exists(temp_path / "Ingester"));
        EXPECT_TRUE(std::filesystem::exists(temp_path / "Ingester" / "Offset_0"));
        EXPECT_TRUE(std::filesystem::exists(temp_path / "Ingester" / "Offset_0" / "Ingester_ST00_RemainderDonut"));

        EXPECT_TRUE(store_b.reverse_lookup(HashBytes<HashBytesCount>{}) == GLOBAL_PI_NO_MATCH);

        // Clean up directory after a successful test run
        std::filesystem::remove_all(temp_path);
    }

    // -----------------------------------------------------------------------------
    // GoogleTest Suite Drivers
    // -----------------------------------------------------------------------------

    TEST(WeddingCakeAppTest, CreateEmpty) {
        RunTestCreateEmptyB<20>();
        RunTestCreateEmptyB<32>();
        RunTestCreateEmptyB<64>();
    }

} // namespace wedding_cake
