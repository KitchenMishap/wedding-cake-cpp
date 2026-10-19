#pragma once

#include "types.hpp"
#include "evolving_disk_state/evolver_type_state.hpp"

namespace wedding_cake {

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
    Sack(CreateNewOnDisk, const evolving_disk_state::Tier& parent_tier, const PiOffset start_offset,
        const size_t capacity) :
        EvolverOrigin(CreateOnDisk(), parent_tier, start_offset.to_int(), TAG)
    {
        hashes_.reserve(capacity);
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

    // Insert a hash at the next presentation index
    // Duplicates not allowed nor checked
    // Returns true if sack becomes full
    bool insert_hash(HashBytes<HashBytesCount>);
    // Insert a gap for a Presentation Index that you find to hold a duplicate of a previous index's hash
    // Returns true if sack becomes full
    bool insert_gap();

    // TypeState transition (evolution): Consumes 'this' (Sack) and returns a SealedSack
    [[nodiscard]] SealedSack evolve_to_SealedSack() &&;

    [[nodiscard]] uint64_t Count() const {return hashes_.size();}
    [[nodiscard]] PiOffset Offset() const {return PiOffset(start_offset());}
    [[nodiscard]] std::optional<HashBytes<HashBytesCount>> ForwardLookup(const GlobalPi gpi) const
        {return hashes_[(gpi - Offset()).to_int()];}

protected:
    [[nodiscard]] std::filesystem::path tag() const noexcept override {
        return TAG;
    }
private:
    std::vector<std::optional<HashBytes<HashBytesCount>>> hashes_;      // Optional due to gaps
    std::map<HashBytes<HashBytesCount>, LocalPi> hash_to_local_pi_;     // Reverse lookup
};
static_assert(evolving_disk_state::MoveOnly<Sack<32>>, "Sack must be move-only");
static_assert(SubStoreConcept<Sack<32>, 32>, "Sack must implement SubStoreConcept");

template <size_t HashBytesCount>
bool Sack<HashBytesCount>::insert_hash(HashBytes<HashBytesCount> hash) {
    if (hashes_.size() == hashes_.capacity()) {
        assert(false && "Sack::insert_hash(): Sack was already full");
        return true;
    }
    auto localPi = LocalPi{hashes_.size()};
    hashes_.push_back(hash);
    hash_to_local_pi_[hash] = localPi;
    return hashes_.size() == hashes_.capacity();
}

template <size_t HashBytesCount>
bool Sack<HashBytesCount>::insert_gap() {
    if (hashes_.size() == hashes_.capacity()) {
        assert(false && "Sack::insert_gap(): Sack was already full");
        return true;
    }
    hashes_.push_back(std::nullopt);
    return hashes_.size() == hashes_.capacity();
}

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

}