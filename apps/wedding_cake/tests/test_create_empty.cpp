#include <filesystem>
#include <gtest/gtest.h>
#include <store.hpp>

namespace wedding_cake {

    // -----------------------------------------------------------------------------
    // Templated Test Helper Functions
    // -----------------------------------------------------------------------------

    template <size_t HashBytesCount>
    static void RunTestCreateEmpty() {
        // Append the size to the path to keep concurrent/sequential tests isolated
        std::filesystem::path temp_path = "TempTesting_" + std::to_string(HashBytesCount);
        std::filesystem::remove_all(temp_path);

        Store<HashBytesCount> store(CreateNewOnDisk{}, temp_path);

        EXPECT_TRUE(std::filesystem::exists(temp_path));
        EXPECT_TRUE(std::filesystem::exists(temp_path / "Tier0"));
        EXPECT_TRUE(std::filesystem::exists(temp_path / "Tier0" / "Offset_0"));
        EXPECT_TRUE(std::filesystem::exists(temp_path / "Tier0" / "Offset_0" / "ST00_Sack"));

        EXPECT_TRUE(store.ReverseLookupCandidate(HashBytes<HashBytesCount>{}) == GLOBAL_PI_NO_MATCH);

        // Clean up directory after a successful test run
        std::filesystem::remove_all(temp_path);
    }

    // -----------------------------------------------------------------------------
    // GoogleTest Suite Drivers
    // -----------------------------------------------------------------------------

    TEST(WeddingCakeAppTest, CreateEmpty) {
        RunTestCreateEmpty<20>();
        RunTestCreateEmpty<32>();
        RunTestCreateEmpty<64>();
    }

} // namespace wedding_cake
