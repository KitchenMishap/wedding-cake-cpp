#pragma once

#include "concepts.hpp"
#include "tiers.hpp"

#include <vector>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace wedding_cake {

constexpr size_t SackSize = 65535;

template <size_t HashBytesCount>
struct Store {
    std::filesystem::path base_path_;
    std::vector<std::unique_ptr<evolving_disk_state::Tier>> tiers;

    [[nodiscard]] static PiOffset Offset() {return PiOffset{0};}  // A store always represents offset 0
    [[nodiscard]] uint64_t Count() const {return tiers[0]->Count() + other_tiers_count;}

    void CreateEmpty(const std::filesystem::path& base_path);

private:
    uint64_t other_tiers_count{0};    // Count of tiers other than tier[0]
};

static_assert(SubStoreConcept<Store<32>, 32>, "Store must implement SubStoreConcept");

template <size_t HashBytesCount>
inline void Store<HashBytesCount>::CreateEmpty(const std::filesystem::path& base_path) {
    if( std::filesystem::exists(base_path) ) {
        throw std::runtime_error("Store already exists. Manually delete the folder and try again");
    }
    base_path_ = base_path;
    std::filesystem::create_directory(base_path_);
    tiers.clear();
    tiers.emplace_back(std::make_unique<Tier0<HashBytesCount>>(CreateNewOnDisk{}, base_path_, SackSize));
}

}