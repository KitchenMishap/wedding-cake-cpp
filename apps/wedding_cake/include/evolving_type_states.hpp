#pragma once

#include "types.hpp"
#include "evolving_disk_state/evolver_type_state.hpp"

// Evolution:
// Sack -> SealedSack

class SealedSack;

class CreateNewOnDisk{};        // Tag type to pass to constructor to indicate "create new on disk"
class LoadExistingFromDisk{};   // Tag type to pass to constructor to indicate "load existing from disk"

template<size_t HashBytesCount>
class Sack : public evolving_disk_state::EvolverOrigin {

    static constexpr std::string_view TAG = "ST00_Sack";

public:
    // Because an evolutionary path can start with a Sack, we can construct one "out of the blue"
    Sack(CreateNewOnDisk, const evolving_disk_state::Tier& parent_tier, const uint64_t start_offset) :
        EvolverOrigin(CreateOnDisk(), parent_tier, start_offset, TAG)
    {
        const auto path = evolving_disk_state::Evolver::object_path();
        create_directories(path);
    }

    Sack(LoadExistingFromDisk, const evolving_disk_state::Tier& parent_tier, const uint64_t start_offset) :
        EvolverOrigin(CreateOnDisk(), parent_tier, start_offset, TAG)
    {
        throw std::runtime_error("Sack::Sack(LoadExistingFromDisk, ...): Not yet implemented");
    }

    // -------------------------------------------------------------------------
    // ENFORCE MOVE-ONLY SEMANTICS (TypeState Safety)
    // -------------------------------------------------------------------------
    Sack(const Sack&) = delete;
    Sack& operator=(const Sack&) = delete;
    Sack(Sack&&) = default;

    // Duplicates not allowed nor checked
    void insert_hash_presentation(wedding_cake::HashGlobalPresentation<HashBytesCount>);

    // TypeState transition (evolution): Consumes 'this' (Sack) and returns a SealedSack
    [[nodiscard]] SealedSack evolve_to_SealedSack() &&;

protected:
    [[nodiscard]] std::filesystem::path tag() const noexcept {
        return TAG;
    }
};
static_assert(evolving_disk_state::MoveOnly<Sack<32>>, "Sack must be move-only!");

class SealedSack : public evolving_disk_state::EvolverDestiny {
    static constexpr std::string_view TAG = "ST01_SealedSack";
public:

    // -------------------------------------------------------------------------
    // ENFORCE MOVE-ONLY SEMANTICS (TypeState Safety)
    // -------------------------------------------------------------------------
    SealedSack(const SealedSack&) = delete;
    SealedSack& operator=(const SealedSack&) = delete;
    SealedSack(SealedSack&&) = default;

protected:
    [[nodiscard]] std::filesystem::path tag() const noexcept {
        return TAG;
    }
};

static_assert(evolving_disk_state::MoveOnly<SealedSack>, "SealedSack must be move-only!");
