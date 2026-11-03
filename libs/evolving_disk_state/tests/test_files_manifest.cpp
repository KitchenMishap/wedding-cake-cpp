#include "evolving_disk_state/buffer_room.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <string>
#include <span>

#include "files_manifest/dummy_implementation.hpp"

using namespace evolving_disk_state;
using namespace std::chrono_literals;

std::span<const uint8_t> to_bytes(const std::string_view sv) noexcept {
    return {reinterpret_cast<const uint8_t*>(sv.data()), sv.size()};
}

// -----------------------------------------------------------------------------
// Test 1: Simple Constructor/Destructor test
// -----------------------------------------------------------------------------
TEST(FilesManifestTest, ConstructorDestructor) {
    auto constexpr dir = "TempTesting_Manifest";
    std::filesystem::create_directories(dir);
    files_manifest::FilesManifestDummy dummy(dir);
    std::filesystem::remove_all(dir);
}
// Test 2: Create, append, size test
TEST(FilesManifestTest, CreateWriteFiles) {
    auto constexpr dir = "TempTesting_Manifest";
    std::filesystem::create_directories(dir);
    files_manifest::FilesManifestDummy man(dir);
    auto handle = man.create_new_file("Hello/World.txt");
    man.append_to_file(handle, to_bytes(std::string_view("Hello World!")));
    EXPECT_EQ(man.get_file_size(handle), 12);
    man.close_file(handle);
    std::filesystem::remove_all(dir);
}