#pragma once

#include "evolving_disk_state/evolver_type_state.hpp"

// Evolution:
// Sack -> SealedSack

class Sack : public evolving_disk_state::EvolverOrigin {
    static constexpr std::string TAG = "ST00_Sack";

public:
    class CreateNewOnDisk{};        // Tag type to pass to constructor to indicate "create new on disk"
    class LoadExistingFromDisk{};   // Tag type to pass to constructor to indicate "load existing from disk"

    Sack(CreateNewOnDisk, const evolving_disk_state::Tier& parent_tier, const uint64_t start_offset) :
        evolving_disk_state::EvolverOrigin(CreateOnDisk(), parent_tier, start_offset, TAG)
    {
        const auto path = evolving_disk_state::Evolver::object_path();
        create_directories(path);
    }

    Sack(LoadExistingFromDisk, const evolving_disk_state::Tier& parent_tier, const uint64_t start_offset) :
        evolving_disk_state::EvolverOrigin(CreateOnDisk(), parent_tier, start_offset, TAG)
    {
        throw std::runtime_error("Sack::Sack(LoadExistingFromDisk, ...): Not yet implemented");
    }

protected:
    [[nodiscard]] std::filesystem::path tag() const noexcept {
        return TAG;
    }
};

class SealedSack : public evolving_disk_state::EvolverDestiny {
    constexpr static std::string TAG = "ST01_SealedSack";
protected:
    [[nodiscard]] std::filesystem::path tag() const noexcept {
        return TAG;
    }
};
