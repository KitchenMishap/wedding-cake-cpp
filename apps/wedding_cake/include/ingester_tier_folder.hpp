#pragma once

//#include "ingester_tier_hydrator.hpp"

#include <evolving_disk_state/buffer_room.hpp>
#include <evolving_disk_state/tier_folder.hpp>

#include "ingester_tier_hydrator.hpp"

namespace wedding_cake {

template<size_t HashBytesCount>
class IngesterTierFolder {
public:
    static constexpr auto Folder = "Ingester";
    // Create folder if doesn't exist, otherwise hydrate
    explicit IngesterTierFolder(const std::filesystem::path& base_path);

    IngesterTierFolder() = delete;

    // Non-const accessor (for mutating operations like append_hashes)
    [[nodiscard]] auto& hatchling() noexcept { return hatchling_; }
    // Const accessor (for read-only operations like remaining_capacity queries)
    [[nodiscard]] const auto& hatchling() const noexcept { return hatchling_; }
    [[nodiscard]] auto& buffer_room() noexcept {return buffer_room_;}
    [[nodiscard]] const auto& buffer_room() const noexcept {return buffer_room_;}

    void promote_hatchling_and_spawn_next();    // Worker's responsibility to call this when hatchling is full
    void flush_manifest_to_disk();

private:
    std::filesystem::path tier_path_;
    const std::vector<uint64_t> sorted_offsets_found_at_startup_;

    Ingester_ST00_RemainderDonut<HashBytesCount> hatchling_;
    evolving_disk_state::BufferRoom<Ingester_ST01_FilledDonut<HashBytesCount>, tier_fanout> buffer_room_;

    static std::vector<uint64_t> find_sorted_offsets(const std::filesystem::path& tier_path);
    static Ingester_ST00_RemainderDonut<HashBytesCount> create_or_hydrate_hatchling(
        const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found);
    void hydrate_buffer_room(const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found);
};

template<size_t HashBytesCount>
inline IngesterTierFolder<HashBytesCount>::IngesterTierFolder(const std::filesystem::path& base_path)
    : tier_path_(base_path / Folder)
    , sorted_offsets_found_at_startup_(find_sorted_offsets(tier_path_))
    , hatchling_(create_or_hydrate_hatchling(tier_path_, sorted_offsets_found_at_startup_))
    , buffer_room_()
{
    hydrate_buffer_room(tier_path_, sorted_offsets_found_at_startup_);
}

template<size_t HashBytesCount>
std::vector<uint64_t> IngesterTierFolder<HashBytesCount>::find_sorted_offsets(const std::filesystem::path &tier_path) {
    // This is our first chance to create the folder if it doesn't exist
    std::filesystem::create_directories(tier_path);
    // Finds and sorts offsets NNN based on subfolders named Offset_NNN/
    return evolving_disk_state::object_folder_offsets(tier_path);
}

template<size_t HashBytesCount>
inline Ingester_ST00_RemainderDonut<HashBytesCount> IngesterTierFolder<HashBytesCount>::create_or_hydrate_hatchling(
    const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found) {
    // The hatchling has the highest (ie latest) offset
    if (sorted_offsets_found.empty()) {
        // If no objects exist on disk, create a RemainderDonut with offset 0
        return Ingester_ST00_RemainderDonut<HashBytesCount>(
            tier_path / evolving_disk_state::object_folder_name(0), ingester_donut_capacity);
    }
    const auto highest_offset = sorted_offsets_found.back();
    const auto hatchling_object_path = tier_path / evolving_disk_state::object_folder_name(highest_offset);
    return Ingester_ST00_RemainderDonut<HashBytesCount>(hatchling_object_path, ingester_donut_capacity);
}

template<size_t HashBytesCount>
inline void IngesterTierFolder<HashBytesCount>::hydrate_buffer_room(
    const std::filesystem::path& tier_path, const std::vector<uint64_t>& sorted_offsets_found) {
    // sorted_offsets_found should as a maximum contain IngestorFanout+1 entries (the +1 is for the hatchling)
    if (sorted_offsets_found.size() > tier_fanout + 1) {
        throw std::runtime_error("create_or_hydrate_buffer_room(): Too many object folders found in tier");
    }
    if (sorted_offsets_found.size() <= 1) {
        // No buffer room entries on disk
        return;
    }
    // The buffer room objects are all except the highest offset
    for (size_t i = 0; i < sorted_offsets_found.size() - 1; ++i) {
        const auto offset = sorted_offsets_found[i];
        const auto object_path = tier_path / evolving_disk_state::object_folder_name(offset);
        auto object_variant = ingester_hydrate_evolver_from_disk<HashBytesCount>(object_path);
        assert(std::holds_alternative<Ingester_ST01_FilledDonut<HashBytesCount>>(object_variant) &&
                   "hydrate_buffer_room(): BufferRoom on disk should only hold Ingester_ST01_FilledDonut's");
        buffer_room_.InitializeAdd(std::get<Ingester_ST01_FilledDonut<HashBytesCount>>(std::move(object_variant)));
    }
}

template<size_t HashBytesCount>
inline void IngesterTierFolder<HashBytesCount>::promote_hatchling_and_spawn_next() {
    auto prev_offset = evolving_disk_state::object_folder_single_offset(hatchling_.object_path());
    auto new_offset = prev_offset + hatchling_.capacity();
    Ingester_ST01_FilledDonut<HashBytesCount> promoted_hatchling(std::move(hatchling_));
    // hatchling_ is now "valid but unspecified" and must be re-initialized soon!
    buffer_room_.Feed(std::move(promoted_hatchling));   // Might block (which is fine)
    Ingester_ST00_RemainderDonut<HashBytesCount> replacement_hatchling(
        evolving_disk_state::object_folder_name(new_offset), ingester_donut_capacity);
    // Phew! now re-initialized
    hatchling_ = std::move(replacement_hatchling);
}

template<size_t HashBytesCount>
inline void IngesterTierFolder<HashBytesCount>::flush_manifest_to_disk() {
    // ToDo
}

}