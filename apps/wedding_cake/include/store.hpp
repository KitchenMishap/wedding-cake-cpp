#pragma once

#include "tiers.hpp"

#include <vector>

struct Store {
    std::filesystem::path base_path_;
    std::vector<std::unique_ptr<evolving_disk_state::Tier>> tiers;

    void CreateEmpty(const std::filesystem::path& base_path);
};

inline void Store::CreateEmpty(const std::filesystem::path& base_path) {
    if( std::filesystem::exists(base_path) ) {
        throw std::runtime_error("Store already exists. Manually delete the folder and try again");
    }
    base_path_ = base_path;
    std::filesystem::create_directory(base_path_);
    tiers.clear();
    tiers.emplace_back(std::make_unique<Tier0>(Tier0::CreateNewOnDisk(), base_path_));
    tiers[0].get()->new_origin_evolver(0);
}