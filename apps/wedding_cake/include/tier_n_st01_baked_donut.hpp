#pragma once
#include <filesystem>
#include <map>
#include <optional>

#include "concepts_lookup.hpp"
#include "donut_entries_calculator.hpp"
#include "tier_n_st00_baking_donut.hpp"
#include "types.hpp"
#include "evolving_disk_state/tier_folder.hpp"
#include "files_manifest/dummy_implementation.hpp"

namespace wedding_cake {
//----------------------------------------------------------------------
// BakedDonut is the TypeState for a Donut that has been baked in Tier N
template<size_t HashBytesCount>
class Tier_N_ST01_BakedDonut {
public:
    static constexpr auto ST01 = "Tier_N_ST01_BakedDonut";
    // Create new from st00 or hydrate from disk
    explicit Tier_N_ST01_BakedDonut(Tier_N_ST00_BakingDonut<HashBytesCount>&& prior_state);

    // Move semantics for hatchling promotion/replacement
    Tier_N_ST01_BakedDonut(Tier_N_ST01_BakedDonut&&) noexcept = default;
    Tier_N_ST01_BakedDonut& operator=(Tier_N_ST01_BakedDonut&&) noexcept = default;
    Tier_N_ST01_BakedDonut(const Tier_N_ST01_BakedDonut&) = delete;
    Tier_N_ST01_BakedDonut& operator=(const Tier_N_ST01_BakedDonut&) = delete;

    const auto& prior_st00() const {return prior_ST00_;}
    [[nodiscard]] const std::filesystem::path& st01_path() const noexcept { return st01_path_; }

private:
    Tier_N_ST00_BakingDonut<HashBytesCount> prior_ST00_;
    std::filesystem::path st01_path_;
    files_manifest::FilesManifestDummy st01_manifest_;

    void hydrate_self_for_evolution() {}    // Nothing needs doing, this state evolves directly from disk
};

template<size_t HashBytesCount>
inline Tier_N_ST01_BakedDonut<HashBytesCount>::Tier_N_ST01_BakedDonut(
        Tier_N_ST00_BakingDonut<HashBytesCount>&& prior_state)
    : prior_ST00_(std::move(prior_state))
    , st01_path_(prior_ST00_.object_path() / ST01)
    , st01_manifest_(st01_path_)
{
    if (!std::filesystem::exists(st01_path_)) {
        // Evolving for the first time: create ST01 directory
        create_directories(st01_path_);
    } else {
        // Rehydrating existing ST01 state from disk
        hydrate_self_for_evolution();
    }
}

}