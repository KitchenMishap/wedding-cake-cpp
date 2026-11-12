#pragma once

#include <filesystem>
#include <memory>
#include <vector>
#include <string>

#include "types.hpp"
#include "lookupable_buffer_room.hpp"
#include "tier_n_hydrator.hpp"
#include "tier_n_st00_baking_donut.hpp"
#include "tier_n_st01_baked_donut.hpp"

namespace wedding_cake {

/// Represents physical storage lifecycle for Tier N (N >= 1)
template<size_t HashBytesCount>
class TierNFolder {
public:
    static constexpr auto FOLDER_PREFIX = "Tier_";

    // Ctor for brand new tier
    TierNFolder(size_t tier_index, const std::filesystem::path& base_path, GlobalPi first_pi_offset);
    // Ctor for Tier found on disk
    TierNFolder(size_t tier_index, const std::filesystem::path& base_path);

    TierNFolder(const TierNFolder&) = delete;
    TierNFolder& operator=(const TierNFolder&) = delete;
    TierNFolder(TierNFolder&&) noexcept = default;
    TierNFolder& operator=(TierNFolder&&) noexcept = default;

    // --- Accessors ---
    [[nodiscard]] size_t tier_index() const noexcept { return tier_index_; }
    [[nodiscard]] const std::filesystem::path& tier_path() const noexcept { return tier_path_; }

    /// Executes worker_lambda while holding an EXCLUSIVE lock on hatchling_
    template <typename Func>
    bool with_hatchling_for_write(Func&& worker_lambda) {
        // 1. Acquire exclusive write lock on the hatchling mutex
        std::unique_lock<std::mutex> lock(hatchling_mutex_);
        // 2. Pass a reference to hatchling_ into the caller's lambda
        std::forward<Func>(worker_lambda)(hatchling_);
        return true;
    }

    // ToDo I think I might delete these two accessors...
    [[nodiscard]] auto& hatchling() noexcept { return hatchling_; }
    [[nodiscard]] const auto& hatchling() const noexcept { return hatchling_; }

    [[nodiscard]] auto& buffer_room() noexcept { return buffer_room_; }
    [[nodiscard]] const auto& buffer_room() const noexcept { return buffer_room_; }

    // --- Timeline Boundaries ---
    [[nodiscard]] GlobalPi first_offset() const;
    [[nodiscard]] GlobalPi next_offset() const;

    // --- Storage Mutations (Called by Worker / Upstream) ---
    void promote_hatchling_and_spawn_next();
    void discard_buffer_room_contents();
    void flush_manifest_to_disk();

    template<typename Func>
    void set_on_full_callback(Func&& cb) {
        buffer_room_.set_on_full_callback(std::forward<Func>(cb));
    }

private:
    size_t tier_index_;
    std::filesystem::path tier_path_;
    const std::vector<uint64_t> sorted_offsets_found_at_startup_;

    mutable std::mutex hatchling_mutex_;
    TierNEvolver<HashBytesCount> hatchling_;
    LookupableBufferRoom<Tier_N_ST01_BakedDonut<HashBytesCount>, tier_fanout, HashBytesCount> buffer_room_;

    void hydrate_buffer_room(const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found);

    static std::filesystem::path make_folder_if_not_exist(const std::filesystem::path& base_path, size_t tier_index);
    static TierNEvolver<HashBytesCount> hydrate_hatchling(
        size_t tier_index, const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found);
};

// Ctor for brand new tier
template<size_t HashBytesCount>
TierNFolder<HashBytesCount>::TierNFolder(
    const size_t tier_index, const std::filesystem::path& base_path, GlobalPi first_pi_offset)
    : tier_index_(tier_index)
    , tier_path_(make_folder_if_not_exist(base_path, tier_index_))
    , hatchling_(Tier_N_ST00_BakingDonut<HashBytesCount>(
        tier_path_ / evolving_disk_state::object_folder_name(first_pi_offset.to_int()),
        tier_n_donut_capacity(tier_index_),
        ms_prefix_bits_for_tier(tier_index)))
{
}

// Ctor for hydration from disk
template<size_t HashBytesCount>
TierNFolder<HashBytesCount>::TierNFolder(
        const size_t tier_index, const std::filesystem::path& base_path)
    : tier_index_(tier_index)
    , tier_path_(base_path / (FOLDER_PREFIX + std::to_string(tier_index)))
    , sorted_offsets_found_at_startup_(evolving_disk_state::object_folder_offsets(tier_path_))
    , hatchling_(hydrate_hatchling(tier_index_, tier_path_, sorted_offsets_found_at_startup_))
    , buffer_room_()
{
    hydrate_buffer_room(tier_path_, sorted_offsets_found_at_startup_);
}

template<size_t HashBytesCount>
std::filesystem::path TierNFolder<HashBytesCount>::make_folder_if_not_exist(
    const std::filesystem::path& base_path, const size_t tier_index)
{
    auto path = base_path / (FOLDER_PREFIX + std::to_string(tier_index));
    std::filesystem::create_directories(path);
    return path;
}

template<size_t HashBytesCount>
TierNEvolver<HashBytesCount> TierNFolder<HashBytesCount>::hydrate_hatchling(
        const size_t tier_index,
        const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found) {
    // The hatchling has the highest (ie latest) offset
    if (sorted_offsets_found.empty()) {
        throw DatabaseCorruptException(tier_path, "No /Offset_NNN/ folders found in tier");
    }
    const auto highest_offset = sorted_offsets_found.back();
    const auto hatchling_object_path = tier_path / evolving_disk_state::object_folder_name(highest_offset);
    // The hatchling may have evolved, so we must hydrate it
    return tier_n_hydrate_evolver_from_disk<HashBytesCount>(hatchling_object_path, tier_n_donut_capacity(tier_index),
        ms_prefix_bits_for_tier(tier_index));
}

template<size_t HashBytesCount>
void TierNFolder<HashBytesCount>::hydrate_buffer_room(
    const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found) {
    // sorted_offsets_found should as a maximum contain IngestorFanout+1 entries (the +1 is for the hatchling)
    if (sorted_offsets_found.size() > tier_fanout + 1) {
        MISHAP_ASSERT(false, "hydrate_buffer_room(): Too many /Offset_NNN/ folders found in tier");
        throw DatabaseCorruptException(tier_path, "Too many /Offset_NNN/ folders found in tier");
    }
    if (sorted_offsets_found.size() <= 1) {
        // No buffer room entries on disk
        return;
    }
    // The buffer room objects are all except the highest offset
    for (size_t i = 0; i < sorted_offsets_found.size() - 1; ++i) {
        const auto offset = sorted_offsets_found[i];
        const auto object_path = tier_path / evolving_disk_state::object_folder_name(offset);
        auto object_variant = tier_n_hydrate_evolver_from_disk<HashBytesCount>(object_path, tier_n_donut_capacity(tier_index_), ms_prefix_bits_for_tier(tier_index_));
        bool correct_variant = std::holds_alternative<Tier_N_ST01_BakedDonut<HashBytesCount>>(object_variant);
        if (!correct_variant) {
            MISHAP_ASSERT( false , "hydrate_buffer_room(): BufferRoom on disk should only hold Tier_N_ST01_BakedDonut's" );
            throw DatabaseCorruptException(tier_path,
                "BufferRoom on disk should only hold Tier_N_ST01_BakedDonut's");
        }
        buffer_room_.InitializeAdd(std::get<Tier_N_ST01_BakedDonut<HashBytesCount>>(std::move(object_variant)));
    }
}

template<size_t HashBytesCount>
std::unique_ptr<TierNFolder<HashBytesCount>> hydrate_tier_if_exists(
        std::filesystem::path base_path, size_t tier_index) {
    auto tier_folder_path = base_path / (TierNFolder<HashBytesCount>::FOLDER_PREFIX + std::to_string(tier_index));
    if (!std::filesystem::exists(tier_folder_path)) {
        return nullptr;
    }
    auto sorted_object_offsets = evolving_disk_state::object_folder_offsets(tier_folder_path);
    // There should be at most tier_fanout + 1 object folders
    if (sorted_object_offsets.size() > tier_fanout + 1) {
        throw DatabaseCorruptException(tier_folder_path, "Too many /Offset_NNN/ folders");
    }
    if (sorted_object_offsets.empty()) {
        // No object folders is regarded as a recoverable error; it could be due to an ungraceful shutdown
        log_ungraceful_shutdown_recovered(tier_folder_path, "No /Offset_NNN/ folders");
        // Treated as "tier does not yet exist"
        return nullptr;
    }
    // Looks like non-empty tier exists
    return std::make_unique<TierNFolder<HashBytesCount>>(tier_index, base_path);
}

} // namespace wedding_cake