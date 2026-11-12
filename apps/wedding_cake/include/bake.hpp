#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "donut_entries_calculator.hpp"
#include "files_manifest/dummy_implementation.hpp"
#include "mishap_assert.hpp"
#include "types.hpp"

namespace wedding_cake {

namespace detail {

// Binary layout constants
constexpr char SUFFIXES_FILE[]             = "Suffixes.bin";
constexpr char PREFIX_OFFSETS_FILE[]       = "PrefixOffsets.bin";
constexpr char SUFFIX_INDEX_BYTES_FILE[]   = "SuffixIndexBytesCount.bin";
constexpr char HASHES_ORDER_FILE[]         = "HashesOrder.bin";
constexpr char FORWARDING_DATA_FILE[]      = "ForwardingData.bin";
constexpr char FORWARDING_OFFSETS_FILE[]   = "ForwardingOffsets.bin";

struct PrefixOffsetEntry {
    uint64_t start_byte_offset{0};
    uint64_t entry_count{0};
};

struct ForwardingOffsetEntry {
    uint64_t start_entry_index{0};
    uint64_t entry_count{0};
};

} // namespace detail

template <size_t HashBytesCount>
bool bake_donut(
    const std::stop_token& stop_token,
    const std::vector<std::filesystem::path>& source_folders,
    const std::filesystem::path& destination_folder,
    const size_t source_prefix_bits_count,
    const size_t delta_prefix_bits_count,
    const GlobalPi first_global_pi,
    const size_t source_entries_per_donut)
{
    using namespace files_manifest;
    using namespace detail;

    MISHAP_ASSERT(!source_folders.empty(), "bake_donut(): source_folders cannot be empty");
    const size_t num_sources = source_folders.size();

    // 1. Initialize Manifest System
    FilesManifestDummy manifest(destination_folder);

    if (manifest.is_finished_sealed()) {
        return true;
    }

    const size_t target_prefix_bits_count = source_prefix_bits_count + delta_prefix_bits_count;
    const size_t target_entries_per_donut = source_entries_per_donut * num_sources;
    const size_t num_target_prefixes      = 1ULL << target_prefix_bits_count;

    DonutEntriesCalculator<HashBytesCount> target_calc(
        target_prefix_bits_count, first_global_pi, target_entries_per_donut);

    DonutEntriesCalculator<HashBytesCount> source_calc(
        source_prefix_bits_count, first_global_pi, source_entries_per_donut);

    // =========================================================================
    // RECOVERY DISCOVERY & STAGE DETERMINATION
    // =========================================================================
    FilesManifestDummy::FileHandle suffixes_fh    = manifest.get_file_handle(SUFFIXES_FILE);
    FilesManifestDummy::FileHandle offsets_fh     = manifest.get_file_handle(PREFIX_OFFSETS_FILE);
    FilesManifestDummy::FileHandle fwd_data_fh    = manifest.get_file_handle(FORWARDING_DATA_FILE);
    FilesManifestDummy::FileHandle fwd_offsets_fh = manifest.get_file_handle(FORWARDING_OFFSETS_FILE);
    FilesManifestDummy::FileHandle suffix_width_fh= manifest.get_file_handle(SUFFIX_INDEX_BYTES_FILE);
    FilesManifestDummy::FileHandle hashes_order_fh= manifest.get_file_handle(HASHES_ORDER_FILE);

    bool stage1_complete = (suffix_width_fh != FilesManifestDummy::InvalidFileHandle);

    // =========================================================================
    // STAGE 1: BACKWARD SWEEP (Prefix 0xFFFF down to 0x0000)
    // =========================================================================
    if (!stage1_complete) {
        if (suffixes_fh == FilesManifestDummy::InvalidFileHandle) {
            suffixes_fh = manifest.create_new_file(SUFFIXES_FILE);
        } else {
            manifest.open_file(suffixes_fh);
        }

        if (offsets_fh == FilesManifestDummy::InvalidFileHandle) {
            offsets_fh = manifest.create_new_file(PREFIX_OFFSETS_FILE);
        } else {
            manifest.open_file(offsets_fh);
        }

        if (fwd_data_fh == FilesManifestDummy::InvalidFileHandle) {
            fwd_data_fh = manifest.create_new_file(FORWARDING_DATA_FILE);
        } else {
            manifest.open_file(fwd_data_fh);
        }

        if (fwd_offsets_fh == FilesManifestDummy::InvalidFileHandle) {
            fwd_offsets_fh = manifest.create_new_file(FORWARDING_OFFSETS_FILE);
        } else {
            manifest.open_file(fwd_offsets_fh);
        }

        // Determine how many prefixes have already been fully processed
        const size_t offsets_bytes_committed = manifest.get_file_size(offsets_fh);
        const size_t prefixes_completed       = offsets_bytes_committed / sizeof(PrefixOffsetEntry);

        // Process remaining prefixes backward: (num_target_prefixes - 1 - prefixes_completed) down to 0
        const int64_t start_prefix_idx = static_cast<int64_t>(num_target_prefixes) - 1 - static_cast<int64_t>(prefixes_completed);

        // Pre-allocate buffer for entry writes
        const size_t suffix_entry_bytes = target_calc.suffixes_entry_size();
        std::vector<uint8_t> suffix_write_buffer(suffix_entry_bytes);

        const size_t source_suffix_entry_bytes = source_calc.suffixes_entry_size();

        for (int64_t target_prefix = start_prefix_idx; target_prefix >= 0; --target_prefix) {
            if (stop_token.stop_requested()) {
                manifest.flush();
                return false;
            }

            struct SuffixItem {
                typename DonutEntriesCalculator<HashBytesCount>::LsSuffixEntriesEntry entry;
                size_t source_donut_idx;
                uint64_t original_suffix_idx;
            };
            std::vector<SuffixItem> merged_suffixes;

            // -----------------------------------------------------------------
            // READ SOURCE SUFFIXES FOR TARGET PREFIX RANGE
            // -----------------------------------------------------------------
            // The target prefix contains extra delta bits. Map it to the corresponding source prefix.
            const uint64_t source_prefix = target_prefix >> delta_prefix_bits_count;

            for (size_t src_idx = 0; src_idx < num_sources; ++src_idx) {
                const auto src_offsets_path = source_folders[src_idx] / PREFIX_OFFSETS_FILE;
                const auto src_suffixes_path = source_folders[src_idx] / SUFFIXES_FILE;

                if (!std::filesystem::exists(src_offsets_path) || !std::filesystem::exists(src_suffixes_path)) {
                    continue;
                }

                // Read prefix offset header for source_prefix
                std::ifstream offsets_file(src_offsets_path, std::ios::binary);
                offsets_file.seekg(source_prefix * sizeof(PrefixOffsetEntry));
                PrefixOffsetEntry src_p_off{};
                if (!offsets_file.read(reinterpret_cast<char*>(&src_p_off), sizeof(PrefixOffsetEntry))) {
                    continue;
                }

                if (src_p_off.entry_count == 0) {
                    continue;
                }

                // Stream entries for this source prefix and keep those matching target_prefix
                std::ifstream suffixes_file(src_suffixes_path, std::ios::binary);
                suffixes_file.seekg(src_p_off.start_byte_offset);

                const uint64_t base_suffix_idx = src_p_off.start_byte_offset / source_suffix_entry_bytes;
                std::vector<uint8_t> raw_entry_bytes(source_suffix_entry_bytes);

                for (uint64_t i = 0; i < src_p_off.entry_count; ++i) {
                    suffixes_file.read(reinterpret_cast<char*>(raw_entry_bytes.data()), source_suffix_entry_bytes);

                    typename DonutEntriesCalculator<HashBytesCount>::LsSuffixEntriesEntry parsed_entry{};
                    source_calc.read_ls_suffixes_entry_from_bytes(raw_entry_bytes, parsed_entry);

                    // Reconstruct hash to verify if its extra bits match the target prefix exact mask
                    const auto full_hash = source_calc.hash_from_prefix_suffix(
                        ms_prefix_bytes(HashBytes<HashBytesCount>{}, static_cast<uint8_t>(source_prefix_bits_count)),
                        parsed_entry.ls_suffix_bytes);

                    const auto target_prefix_masked = ms_prefix_bytes_masked(full_hash, static_cast<uint8_t>(target_prefix_bits_count));
                    const uint64_t extracted_target_prefix = target_prefix_masked.to_uint64(target_prefix_bits_count);

                    if (extracted_target_prefix == static_cast<uint64_t>(target_prefix)) {
                        merged_suffixes.push_back(SuffixItem{
                            .entry = parsed_entry,
                            .source_donut_idx = src_idx,
                            .original_suffix_idx = base_suffix_idx + i
                        });
                    }
                }
            }

            // Sort merged suffixes deterministically by suffix bytes
            std::ranges::sort(merged_suffixes, [](const SuffixItem& a, const SuffixItem& b) {
                return std::memcmp(a.entry.ls_suffix_bytes.bytes.data(),
                                   b.entry.ls_suffix_bytes.bytes.data(),
                                   a.entry.ls_suffix_bytes.bytes.size()) < 0;
            });

            // Stream merged suffixes & record forwarding entries
            const uint64_t current_suffix_file_offset = manifest.get_file_size(suffixes_fh);
            const uint64_t current_fwd_data_offset   = manifest.get_file_size(fwd_data_fh) / sizeof(uint64_t);

            for (size_t new_idx = 0; new_idx < merged_suffixes.size(); ++new_idx) {
                const auto& item = merged_suffixes[new_idx];

                // Write suffix record
                target_calc.write_suffixes_file_entry(
                    target_calc.hash_from_prefix_suffix(MsPrefixBytes{}, item.entry.ls_suffix_bytes),
                    item.entry.local_pi + first_global_pi,
                    suffix_write_buffer);
                manifest.append_to_file(suffixes_fh, suffix_write_buffer);

                // Write 64-bit forwarding target index
                const uint64_t global_new_suffix_idx = (current_suffix_file_offset / suffix_entry_bytes) + new_idx;
                const uint8_t* fwd_bytes = reinterpret_cast<const uint8_t*>(&global_new_suffix_idx);
                manifest.append_to_file(fwd_data_fh, std::span<const uint8_t>(fwd_bytes, sizeof(uint64_t)));
            }

            // Append metadata headers
            PrefixOffsetEntry p_off{
                .start_byte_offset = current_suffix_file_offset,
                .entry_count       = merged_suffixes.size()
            };
            manifest.append_to_file(offsets_fh, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&p_off), sizeof(PrefixOffsetEntry)));

            ForwardingOffsetEntry f_off{
                .start_entry_index = current_fwd_data_offset,
                .entry_count       = merged_suffixes.size()
            };
            manifest.append_to_file(fwd_offsets_fh, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&f_off), sizeof(ForwardingOffsetEntry)));

            manifest.commit();
        }

        manifest.close_file(suffixes_fh);
        manifest.close_file(offsets_fh);
        manifest.close_file(fwd_data_fh);
        manifest.close_file(fwd_offsets_fh);

        // Stage 1 Complete: Calculate highest suffix entry across PrefixOffsets.bin
        offsets_fh = manifest.get_file_handle(PREFIX_OFFSETS_FILE);
        manifest.open_file(offsets_fh);

        uint64_t total_valid_suffixes = 0;

        std::ifstream offsets_file(destination_folder / PREFIX_OFFSETS_FILE, std::ios::binary);
        PrefixOffsetEntry entry;
        while (offsets_file.read(reinterpret_cast<char*>(&entry), sizeof(PrefixOffsetEntry))) {
            total_valid_suffixes += entry.entry_count;
        }
        manifest.close_file(offsets_fh);

        // Determine bytes needed for suffix index (1..8)
        const uint8_t bytes_per_suffix_idx = DonutEntriesWriteCalculator<HashBytesCount>::calculate_bytes_per_local_pi(
            static_cast<size_t>(total_valid_suffixes));

        suffix_width_fh = manifest.create_new_file(SUFFIX_INDEX_BYTES_FILE);
        manifest.append_to_file(suffix_width_fh, std::span<const uint8_t>(&bytes_per_suffix_idx, 1));
        manifest.close_file(suffix_width_fh);

        manifest.commit();
        manifest.flush();
    }

    // Read the suffix width parameter
    uint8_t bytes_per_suffix_index = 0;
    {
        std::ifstream width_file(destination_folder / SUFFIX_INDEX_BYTES_FILE, std::ios::binary);
        width_file.read(reinterpret_cast<char*>(&bytes_per_suffix_index), 1);
    }

    // =========================================================================
    // STAGE 2: HASHES ORDER TRANSLATION
    // =========================================================================
    if (hashes_order_fh == FilesManifestDummy::InvalidFileHandle) {
        hashes_order_fh = manifest.create_new_file(HASHES_ORDER_FILE);
    } else {
        manifest.open_file(hashes_order_fh);
    }

    const size_t output_hashes_entry_size = target_calc.hashes_order_entry_size(bytes_per_suffix_index);
    const size_t hashes_order_committed   = manifest.get_file_size(hashes_order_fh);
    const size_t current_entry_index       = hashes_order_committed / output_hashes_entry_size;

    std::vector<uint8_t> hash_order_write_buffer(output_hashes_entry_size);

    for (size_t entry_idx = current_entry_index; entry_idx < target_entries_per_donut; ++entry_idx) {
        if (stop_token.stop_requested()) {
            manifest.flush();
            return false;
        }

        // -----------------------------------------------------------------
        // ZERO-ALLOCATION TRANSLATION (Source HashesOrder -> Output HashesOrder)
        // -----------------------------------------------------------------
        const size_t src_donut_idx = entry_idx / source_entries_per_donut;
        const size_t src_local_idx = entry_idx % source_entries_per_donut;

        const auto src_hashes_order_path = source_folders[src_donut_idx] / HASHES_ORDER_FILE;

        // Read width of source donut's suffix index from its HashesOrder.bin or metadata
        // For standard source calculation:
        const uint8_t src_bytes_per_suffix_idx = source_calc.bytes_per_local_pi();
        const size_t src_hashes_entry_bytes     = source_calc.hashes_order_entry_size(src_bytes_per_suffix_idx);

        std::ifstream src_hashes_file(src_hashes_order_path, std::ios::binary);
        src_hashes_file.seekg(src_local_idx * src_hashes_entry_bytes);

        std::vector<uint8_t> src_entry_bytes(src_hashes_entry_bytes);
        src_hashes_file.read(reinterpret_cast<char*>(src_entry_bytes.data()), src_hashes_entry_bytes);

        typename DonutEntriesCalculator<HashBytesCount>::HashesOrderEntry src_entry{};
        source_calc.read_hashes_order_entry_from_bytes(src_entry_bytes, src_bytes_per_suffix_idx, src_entry);

        if (src_entry.is_sentinel) {
            // Non-hash sentinel: write sentinel to destination HashesOrder.bin
            target_calc.write_hashes_order_file_entry(
                std::nullopt,
                0,
                bytes_per_suffix_index,
                hash_order_write_buffer);
        } else {
            // Read new_suffix_index from ForwardingData.bin using source suffix file index
            std::ifstream fwd_data_file(destination_folder / FORWARDING_DATA_FILE, std::ios::binary);
            fwd_data_file.seekg(src_entry.suffixes_file_index * sizeof(uint64_t));

            uint64_t translated_new_suffix_index = 0;
            fwd_data_file.read(reinterpret_cast<char*>(&translated_new_suffix_index), sizeof(uint64_t));

            // Construct hash or prefix payload and write translated entry
            const HashBytes<HashBytesCount> temp_hash{}; // Masked prefix bytes extracted from src_entry.ms_prefix_bytes
            target_calc.write_hashes_order_file_entry(
                temp_hash,
                translated_new_suffix_index,
                bytes_per_suffix_index,
                hash_order_write_buffer);
        }

        manifest.append_to_file(hashes_order_fh, hash_order_write_buffer);

        if (entry_idx % 10000 == 0) {
            manifest.commit();
        }
    }

    manifest.close_file(hashes_order_fh);

    // Clean up temporary forwarding files
    std::filesystem::remove(destination_folder / FORWARDING_DATA_FILE);
    std::filesystem::remove(destination_folder / FORWARDING_OFFSETS_FILE);

    // Finalize and seal the manifest
    manifest.finish_and_seal();
    return true;
}

} // namespace wedding_cake