#pragma once

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

    void CreateEmpty(const std::filesystem::path& base_path);
};

template <size_t HashBytesCount>
inline void Store<HashBytesCount>::CreateEmpty(const std::filesystem::path& base_path) {
    if( std::filesystem::exists(base_path) ) {
        throw std::runtime_error("Store already exists. Manually delete the folder and try again");
    }
    base_path_ = base_path;
    std::filesystem::create_directory(base_path_);
    tiers.clear();
    tiers.emplace_back(std::make_unique<Tier0<HashBytesCount>>(CreateNewOnDisk{}, base_path_, SackSize));
    tiers[0].get()->new_origin_evolver(0);
}

}