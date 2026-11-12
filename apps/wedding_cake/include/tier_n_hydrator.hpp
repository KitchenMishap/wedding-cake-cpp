#pragma once
#include <filesystem>

#include "exceptions.hpp"
#include "tier_n_st00_baking_donut.hpp"
#include "tier_n_st01_baked_donut.hpp"

namespace wedding_cake {


// Type-erased variant representing the various evolutions of ObjectFolders in this Tier
template<size_t HashBytesCount>
using TierNEvolver = std::variant<
    Tier_N_ST00_BakingDonut<HashBytesCount>,
    Tier_N_ST01_BakedDonut<HashBytesCount>
>;

template<size_t HashBytesCount>
inline TierNEvolver<HashBytesCount> tier_n_hydrate_evolver_from_disk(
        const std::filesystem::path &object_path, size_t capacity, size_t prefix_bits_count) {
    // Always start by hydrating ST00
    Tier_N_ST00_BakingDonut<HashBytesCount> st00(object_path, capacity, prefix_bits_count);

    // Check if ST01 folder exists
    if (!std::filesystem::exists(object_path / Tier_N_ST01_BakedDonut<HashBytesCount>::ST01))
        // Just leave it as an ST00
            return st00;

    // An st01 "contains" the prior st00 by moving
    Tier_N_ST01_BakedDonut st01(std::move(st00));
    return st01;
}

}
