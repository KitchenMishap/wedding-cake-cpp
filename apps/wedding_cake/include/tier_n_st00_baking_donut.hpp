#pragma once
#include <filesystem>
#include <map>
#include <optional>

#include "concepts_lookup.hpp"
#include "donut_entries_calculator.hpp"
#include "types.hpp"
#include "evolving_disk_state/tier_folder.hpp"
#include "files_manifest/dummy_implementation.hpp"

namespace wedding_cake {
//----------------------------------------------------------------------
// RawDonut is the Hatchling TypeState for a Donut being baked in Tier N
template<size_t HashBytesCount>
class Tier_N_ST00_BakingDonut {
public:
    static constexpr auto ST00 = "Tier_N_ST00_RawDonut";
    // Create new or hydrate from disk
    Tier_N_ST00_BakingDonut(const std::filesystem::path &object_path,
        size_t capacity, size_t prefix_bits_count);

    // Move semantics for hatchling promotion/replacement
    Tier_N_ST00_BakingDonut(Tier_N_ST00_BakingDonut&&) noexcept = default;
    Tier_N_ST00_BakingDonut& operator=(Tier_N_ST00_BakingDonut&&) noexcept = default;
    Tier_N_ST00_BakingDonut(const Tier_N_ST00_BakingDonut&) = delete;
    Tier_N_ST00_BakingDonut& operator=(const Tier_N_ST00_BakingDonut&) = delete;

    [[nodiscard]] const std::filesystem::path& object_path() const noexcept { return object_path_; }
    [[nodiscard]] const std::filesystem::path& st00_path() const noexcept { return st00_path_; }

    [[nodiscard]] size_t prefix_bits_count() const {return prefix_bits_count_;}

    [[nodiscard]] GlobalPi first_offset() const {return first_global_pi_;}
    [[nodiscard]] GlobalPi next_offset() const {return first_global_pi_ + LocalPi(capacity_);}

    // A RawDonut does not support lookup (by definition it hasn't been baked completely yet)

private:
    static bool create_folder_if_needed(const std::filesystem::path& path);
    void hydrate_self_for_evolution() {}    // Nothing needs doing, this state evolves directly from disk

    size_t capacity_;
    size_t prefix_bits_count_;
    std::filesystem::path object_path_;
    GlobalPi first_global_pi_;
    std::filesystem::path st00_path_;
    bool hydration_required_;
    files_manifest::FilesManifestDummy st00_manifest_;
    DonutEntriesCalculator<HashBytesCount> donut_entries_calculator_;
    files_manifest::FilesManifestDummy::FileHandle file_handle_hashes_order_;
    //files_manifest::FilesManifestDummy::FileHandle file_handle_suffix_entries_; //ToDo will need several of these
};

template<size_t HashBytesCount>
inline Tier_N_ST00_BakingDonut<HashBytesCount>::Tier_N_ST00_BakingDonut(
                const std::filesystem::path &object_path,
                const size_t capacity, const size_t prefix_bits_count)
: capacity_(capacity)
, prefix_bits_count_(prefix_bits_count)
, object_path_(object_path)
, first_global_pi_(evolving_disk_state::object_folder_single_offset(object_path))
, st00_path_(object_path / ST00)
, hydration_required_(!create_folder_if_needed(st00_path_))
, st00_manifest_(st00_path_)
, donut_entries_calculator_(prefix_bits_count_,first_global_pi_,capacity_)
{
    if (hydration_required_) {
        // Hydrate from disk (because folder already existed)
        hydrate_self_for_evolution();
        // We don't hydrate_self_for_lookup() yet - we might not have all our data
        // Open some files and be ready to append to them
        file_handle_hashes_order_ = st00_manifest_.get_file_handle(HASHES_ORDER_FILENAME);
        // ToDo have a think about this
        //file_handle_suffix_entries_ = st00_manifest_.get_file_handle(
        //    make_ms_prefix_path("", 0, MsPrefixIndex(0)));
        hydration_required_ = false;
    } else {
        // Create two new files and be ready to append to them
        file_handle_hashes_order_ = st00_manifest_.create_new_file(HASHES_ORDER_FILENAME);
        // ToDo have a think about this
        //file_handle_suffix_entries_ = st00_manifest_.create_new_file(
        //    make_ms_prefix_path("", 0, MsPrefixIndex(0)));

        // We can't determine the 1-byte-header to HashesOrder.bin yet - don't know the value
    }
}

template<size_t HashBytesCount>
bool Tier_N_ST00_BakingDonut<HashBytesCount>::create_folder_if_needed(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        // Create new
        std::filesystem::create_directories(path);
        return true;
    }
    return false;
}

}