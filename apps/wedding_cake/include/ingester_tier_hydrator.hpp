#pragma once

#include "ingester_evolver_types.hpp"
#include "ingester_tier_folder.hpp"
#include <evolving_disk_state/evolver_type_state.hpp>
#include "tier_constants.hpp"
#include <memory>

namespace wedding_cake {

// Type-erased variant representing the various evolutions of ObjectFolders in this Tier
template<size_t HashBytesCount>
using IngesterEvolver = std::variant<
    Ingester_ST00_RemainderDonut<HashBytesCount>,
    Ingester_ST01_FilledDonut<HashBytesCount>
>;

template<size_t HashBytesCount>
inline IngesterEvolver<HashBytesCount> ingester_hydrate_evolver_from_disk(const std::filesystem::path &object_path) {
    // Always start by hydrating ST00
    Ingester_ST00_RemainderDonut<HashBytesCount> st00(object_path, ingester_donut_capacity);

    // Check if ST01 folder exists
    if (!std::filesystem::exists(object_path / Ingester_ST01_FilledDonut<HashBytesCount>::ST01))
        // Just leave it as an ST00
        return st00;

    // An st01 "contains" the prior st00 by moving
    Ingester_ST01_FilledDonut st01(std::move(st00));
    return st01;
}

};
