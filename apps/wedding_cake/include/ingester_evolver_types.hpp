#pragma once

#include <variant>
#include <evolving_disk_state/evolver_type_state.hpp>
#include <evolving_disk_state/tier_folder.hpp>

#include "donut_entries_calculator.hpp"
#include "ingester_buffer.hpp"
#include "tier_constants.hpp"
#include "files_manifest/dummy_implementation.hpp"

namespace wedding_cake {

//--------------------------------------------------------------------------------------------
// RemainderDonut is the EvolverOrigin TypeState for a Donut that has not yet reached capacity
template<size_t HashBytesCount>
class Ingester_ST00_RemainderDonut {
public:
static constexpr auto ST00 = "Ingester_ST00_RemainderDonut";
    // Create new or hydrate from disk
    Ingester_ST00_RemainderDonut(const std::filesystem::path &object_path, size_t capacity);

    // Move semantics for hatchling promotion/replacement
    Ingester_ST00_RemainderDonut(Ingester_ST00_RemainderDonut&&) noexcept = default;
    Ingester_ST00_RemainderDonut& operator=(Ingester_ST00_RemainderDonut&&) noexcept = default;
    Ingester_ST00_RemainderDonut(const Ingester_ST00_RemainderDonut&) = delete;
    Ingester_ST00_RemainderDonut& operator=(const Ingester_ST00_RemainderDonut&) = delete;

    [[nodiscard]] const std::filesystem::path& object_path() const noexcept { return object_path_; }
    [[nodiscard]] const std::filesystem::path& st00_path() const noexcept { return st00_path_; }

    // Interface used by ingestor_worker()
    // Returns the number of hashes consumed from ram_buffer
    size_t append_hashes(IngesterBuffer<HashBytesCount>::BatchType& ram_buffer); // Worker must then promote if full
    [[nodiscard]] size_t capacity() const {return capacity_;}
    [[nodiscard]] size_t remaining_capacity() const { return capacity_ - forward_lookup_vector_.size(); }

    [[nodiscard]] PiOffset next_offset() const;

    std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi, bool& found) const;
    GlobalPi reverse_lookup(HashBytes<HashBytesCount> hash) const;

private:
    static bool create_folder_if_needed(const std::filesystem::path& path);
    void hydrate_self_for_evolution() {}    // Nothing needs doing, this state evolves directly from disk
    void hydrate_self_for_lookup() {}       // ToDo

    std::vector<std::optional<HashBytes<HashBytesCount>>> forward_lookup_vector_;
    std::map<HashBytes<HashBytesCount>,LocalPi> reverse_lookup_map_;

    size_t capacity_;
    std::filesystem::path object_path_;
    PiOffset first_global_pi_;
    std::filesystem::path st00_path_;
    bool hydration_required_;
    files_manifest::FilesManifestDummy st00_manifest_;
    DonutEntriesCalculator<HashBytesCount> donut_entries_calculator_;
    files_manifest::FilesManifestDummy::FileHandle file_handle_hashes_order_;
    files_manifest::FilesManifestDummy::FileHandle file_handle_suffix_entries_;
};

template<size_t HashBytesCount>
inline Ingester_ST00_RemainderDonut<HashBytesCount>::Ingester_ST00_RemainderDonut(
                const std::filesystem::path &object_path, const size_t capacity)
: capacity_(capacity)
, object_path_(object_path)
, first_global_pi_(evolving_disk_state::object_folder_single_offset(object_path))
, st00_path_(object_path / ST00)
, hydration_required_(!create_folder_if_needed(st00_path_))
, st00_manifest_(st00_path_)
, donut_entries_calculator_(0,first_global_pi_,capacity)
{
    if (hydration_required_) {
        // Hydrate from disk (because folder already existed)
        hydrate_self_for_evolution();
        // Because the Ingester tier has no previous tier to act as an alternative for lookup during evolution,
        // Ingester_ST00_RemainderDonut must IMMEDIATELY be ready to support lookup
        hydrate_self_for_lookup();
        // Open two files and be ready to append to them
        file_handle_hashes_order_ = st00_manifest_.get_file_handle(HASHES_ORDER_FILENAME);
        file_handle_suffix_entries_ = st00_manifest_.get_file_handle(
            make_ms_prefix_path("", 0, MsPrefixIndex(0)));
        hydration_required_ = false;
    } else {
        // Create two new files and be ready to append to them
        file_handle_hashes_order_ = st00_manifest_.create_new_file(HASHES_ORDER_FILENAME);
        file_handle_suffix_entries_ = st00_manifest_.create_new_file(
            make_ms_prefix_path("", 0, MsPrefixIndex(0)));
    }
}

template<size_t HashBytesCount>
bool Ingester_ST00_RemainderDonut<HashBytesCount>::create_folder_if_needed(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        // Create new
        std::filesystem::create_directories(path);
        return true;
    }
    return false;
}

// Takes as many as available that will fit into remaining capacity
// It is the caller's responsibility to promote to buffer room if full
template<size_t HashBytesCount>
size_t Ingester_ST00_RemainderDonut<HashBytesCount>::append_hashes(
        typename IngesterBuffer<HashBytesCount>::BatchType& ram_buffer) {
    auto available = ram_buffer.size();
    auto taking = std::min(available, remaining_capacity());

    // Somewhere to prepare the suffixes file entry
    // Some bytes for the suffix (whole hash!) plus some bytes for the local_pi
    constexpr size_t max_plausible_suffix_entry = HashBytesCount + ingester_donut_local_pi_bytes;
    assert(max_plausible_suffix_entry >= donut_entries_calculator_.suffixes_entry_size()
        && "append_hashes(): Suffix buffer too small somehow");
    std::array<uint8_t, max_plausible_suffix_entry> suffix_buffer;

    // Somewhere to prepare the hashes order file entry
    // 0 bytes for the prefix, plus some bytes for the index into the suffix file
    constexpr size_t max_plausible_hashes_order_entry = 0 + ingester_donut_local_pi_bytes;
    assert(max_plausible_hashes_order_entry >= donut_entries_calculator_.hashes_order_entry_size(ingester_donut_local_pi_bytes)
        && "append_hashes(): Hashes order buffer too small somehow");
    std::array<uint8_t, max_plausible_hashes_order_entry> hashes_order_buffer;

    // Look at every entry in the batch
    auto local_pi_int = forward_lookup_vector_.size();
    for (auto entry_it = ram_buffer.begin(); entry_it != ram_buffer.begin() + taking; ++entry_it) {
        // Calculate the bytes
        auto count = donut_entries_calculator_.write_suffixes_file_entry(
            *entry_it, LocalPi(local_pi_int) + first_global_pi_, suffix_buffer);
        // Append count bytes to file (count can be zero if no hash is specified)
        if (count > 0) {
            st00_manifest_.append_to_file(file_handle_suffix_entries_, std::span(suffix_buffer).first(count));
        }

        // Calculate the bytes
        count = donut_entries_calculator_.write_hashes_order_file_entry(
            *entry_it, (LocalPi(local_pi_int) + first_global_pi_).to_int(), ingester_donut_local_pi_bytes, hashes_order_buffer);
        // Append count bytes to file
        st00_manifest_.append_to_file(file_handle_hashes_order_, std::span(hashes_order_buffer).first(count));
        ++local_pi_int;
    }

    // Add to the reverse lookup map (in memory)
    local_pi_int = forward_lookup_vector_.size();
    for (auto it = ram_buffer.begin(); it != ram_buffer.begin() + taking; ++it) {
        if (it->has_value()) {
            reverse_lookup_map_.insert({it->value(), LocalPi(local_pi_int)});
        }
        ++local_pi_int;
    }

    // Add to the forward lookup vector (in memory)
    forward_lookup_vector_.insert(forward_lookup_vector_.end(), ram_buffer.begin(), ram_buffer.begin() + taking);

    assert(forward_lookup_vector_.size() <= capacity_
        && "append_hashes(): forward_lookup_vector_ overflowed somehow");
    if (forward_lookup_vector_.size() == capacity_) {
        st00_manifest_.close_file(file_handle_suffix_entries_);
        st00_manifest_.close_file(file_handle_hashes_order_);
        st00_manifest_.finish_and_seal();
    }

    return taking;
}

// next_offset() is the global offset of the next hash to be ingested
template<size_t HashBytesCount>
PiOffset Ingester_ST00_RemainderDonut<HashBytesCount>::next_offset() const {
    return PiOffset{LocalPi(forward_lookup_vector_.size()) + first_global_pi_};
}

template<size_t HashBytesCount>
std::optional<HashBytes<HashBytesCount>> Ingester_ST00_RemainderDonut<HashBytesCount>::forward_lookup(
    GlobalPi global_pi, bool& found) const {
    if (global_pi.to_int() < first_global_pi_.to_int()) {
        found = false;
        return std::nullopt;
    }
    if (global_pi.to_int() >= first_global_pi_.to_int() + forward_lookup_vector_.size()) {
        assert(false && "Shouldn't be asking me?");
        found = false;
        return std::nullopt;
    }
    found = true;
    return forward_lookup_vector_[global_pi.to_int() - first_global_pi_.to_int()];
};

template<size_t HashBytesCount>
GlobalPi Ingester_ST00_RemainderDonut<HashBytesCount>::reverse_lookup(HashBytes<HashBytesCount> hash) const {
    auto it = reverse_lookup_map_.find(hash);
    if (it == reverse_lookup_map_.end()) {
        return GLOBAL_PI_NO_MATCH;
    }
    return it->second + first_global_pi_;
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

    std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi, bool& found) const;
    GlobalPi reverse_lookup(HashBytes<HashBytesCount> hash) const;

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

template<size_t HashBytesCount>
std::optional<HashBytes<HashBytesCount>> Ingester_ST01_FilledDonut<HashBytesCount>::forward_lookup(GlobalPi global_pi, bool& found) const {
    return prior_ST00_.forward_lookup(global_pi, found);
}
    template<size_t HashBytesCount>
GlobalPi Ingester_ST01_FilledDonut<HashBytesCount>::reverse_lookup(HashBytes<HashBytesCount> hash) const {
    return prior_ST00_.reverse_lookup(hash);
}

}
