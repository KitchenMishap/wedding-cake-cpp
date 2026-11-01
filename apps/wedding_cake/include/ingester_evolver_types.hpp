#pragma once

#include <variant>
#include <evolving_disk_state/evolver_type_state.hpp>
#include <evolving_disk_state/tier_folder.hpp>

#include "ingester_buffer.hpp"

namespace wedding_cake {

//--------------------------------------------------------------------------------------------
// RemainderDonut is the EvolverOrigin TypeState for a Donut that has not yet reached capacity
template<size_t HashBytesCount>
class Ingester_ST00_RemainderDonut {
public:
static constexpr auto ST00 = "Ingester_ST00_RemainderDonut";
    // Create new or hydrate from disk
    Ingester_ST00_RemainderDonut(const std::filesystem::path &object_path, size_t capacity);

    [[nodiscard]] const std::filesystem::path& object_path() const noexcept { return object_path_; }
    [[nodiscard]] const std::filesystem::path& st00_path() const noexcept { return st00_path_; }

    // Interface used by ingestor_worker()
    // Returns the number of hashes consumed from ram_buffer
    size_t append_hashes(IngesterBuffer<HashBytesCount>::BatchType& ram_buffer); // Worker must then promote if full
    [[nodiscard]] size_t capacity() const {return capacity_;}
    [[nodiscard]] size_t remaining_capacity() const { return capacity_ - hashes_.size(); }

private:
    void hydrate_self_for_evolution() {}    // Nothing needs doing, this state evolves directly from disk
    void hydrate_self_for_lookup() {}       // ToDo

    // ToDo: member variables to support in-memory lookup
    std::vector<std::optional<HashBytes<HashBytesCount>>> hashes_;  // ToDo for the time being...

    size_t capacity_;
    std::filesystem::path object_path_;
    std::filesystem::path st00_path_;
};

template<size_t HashBytesCount>
inline Ingester_ST00_RemainderDonut<HashBytesCount>::Ingester_ST00_RemainderDonut(
                const std::filesystem::path &object_path, const size_t capacity)
: capacity_(capacity)
, object_path_(object_path)
, st00_path_(object_path / ST00)
{
    if (!std::filesystem::exists(st00_path_)) {
        // Create new
        std::filesystem::create_directories(st00_path_);
    } else {
        // Hydrate from disk
        hydrate_self_for_evolution();
        // Because the Ingester tier has no previous tier to act as an alternative for lookup during evolution,
        // Ingester_ST00_RemainderDonut must IMMEDIATELY be ready to support lookup
        hydrate_self_for_lookup();
    }
}

// Takes as many as available that will fit into remaining capacity
// It is the caller's responsibility to promote to buffer room if full
template<size_t HashBytesCount>
size_t Ingester_ST00_RemainderDonut<HashBytesCount>::append_hashes(
        typename IngesterBuffer<HashBytesCount>::BatchType& ram_buffer) {
    auto available = ram_buffer.size();
    auto taking = std::min(available, remaining_capacity());
    hashes_.insert(hashes_.end(), ram_buffer.begin(), ram_buffer.begin() + taking);
    return taking;
}

//---------------------------------------------------------------------------
// FilledDonut is the Evolver TypeState for a Donut that has reached capacity
template<size_t HashBytesCount>
class Ingester_ST01_FilledDonut {
public:
    static constexpr auto ST01 = "Ingester_ST01_FilledDonut";
    // Move-constructs from predecessor
    explicit Ingester_ST01_FilledDonut(Ingester_ST00_RemainderDonut<HashBytesCount>&& prior_state);

    // =========================================================================
    // MOVE & COPY SEMANTICS
    // =========================================================================
    Ingester_ST01_FilledDonut(Ingester_ST01_FilledDonut&&) noexcept = default;
    Ingester_ST01_FilledDonut& operator=(Ingester_ST01_FilledDonut&&) noexcept = default;
    // Delete copy operations to ensure strict single-ownership of disk state
    Ingester_ST01_FilledDonut(const Ingester_ST01_FilledDonut&) = delete;
    Ingester_ST01_FilledDonut& operator=(const Ingester_ST01_FilledDonut&) = delete;

    void hydrate_self_for_evolution() {}    // Nothing to do, this state evolves directly from disk
    void hydrate_self_for_lookup() {}       // Nothing to do, this state passes lookups to the contained prior_ST00_

    [[nodiscard]] const std::filesystem::path& st01_path() const noexcept { return st01_path_; }
private:
    Ingester_ST00_RemainderDonut<HashBytesCount> prior_ST00_;
    std::filesystem::path st01_path_;
};

template<size_t HashBytesCount>
inline Ingester_ST01_FilledDonut<HashBytesCount>::Ingester_ST01_FilledDonut(Ingester_ST00_RemainderDonut<HashBytesCount>&& prior_state)
: prior_ST00_(std::move(prior_state))
, st01_path_(prior_ST00_.object_path() / ST01)
{
    if (!std::filesystem::exists(st01_path_)) {
        // Evolving for the first time: create ST01 directory
        create_directories(st01_path_);
    } else {
        // Rehydrating existing ST01 state from disk
        hydrate_self_for_evolution();
        // Because the Ingester Tier has no previous tier to act as an alternative for lookup during evolution,
        // Ingester_ST00_RemainderDonut must IMMEDIATELY be ready to support lookup
        hydrate_self_for_lookup();
    }
}

}
