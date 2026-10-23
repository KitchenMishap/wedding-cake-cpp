#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "hashes_order_file.hpp"
#include "ls_suffix_entries_file.hpp"
#include "ms_prefix_path.hpp"

namespace wedding_cake {

template <size_t HashBytesCount>
class ReaderWriterFactory {
public:
    ReaderWriterFactory(std::filesystem::path path,
                        const uint8_t pi_bytes_count,
                        const uint8_t ms_prefix_bits_count)
        : path_(std::move(path)),
          pi_bytes_count_(pi_bytes_count),
          ms_prefix_bits_count_(ms_prefix_bits_count) {

        static_assert(HashBytesCount >= 1 && HashBytesCount <= 64, "HashBytesCount must be between 1 and 64");

        if (ms_prefix_bits_count_ >= static_cast<uint16_t>(HashBytesCount) * 8) {
            throw std::invalid_argument("ms_prefix_bits_count must be strictly less than total hash bits");
        }
        if (pi_bytes_count_ == 0 || pi_bytes_count_ > 8) {
            throw std::invalid_argument("pi_bytes_count must be between 1 and 8");
        }
    }

    // -------------------------------------------------------------------------
    // HashesOrder File Operations
    // -------------------------------------------------------------------------
    void create_empty_hashes_order_file() const {
        std::ofstream file;
        file.open(path_ / "HashesOrder.bin", std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create empty \"HashesOrder.bin\" file at: " + path_.string());
        }
        file.close();
    }

    [[nodiscard]] std::unique_ptr<HashesOrderFileWriter<HashBytesCount>> create_hashes_order_file_writer() const {
        return std::make_unique<HashesOrderFileWriter<HashBytesCount>>(
            typename HashesOrderFileWriter<HashBytesCount>::Key{}, path_ / "HashesOrder.bin", pi_bytes_count_, ms_prefix_bits_count_
        );
    }

    [[nodiscard]] std::unique_ptr<HashesOrderFileReader<HashBytesCount>> create_hashes_order_file_reader() const {
        return std::make_unique<HashesOrderFileReader<HashBytesCount>>(
            typename HashesOrderFileReader<HashBytesCount>::Key{}, path_ / "HashesOrder.bin", pi_bytes_count_, ms_prefix_bits_count_
        );
    }

    // -------------------------------------------------------------------------
    // LSSuffixEntries File Operations
    // -------------------------------------------------------------------------
    void create_empty_ls_suffix_entries_file(uint64_t ms_prefix_index) const {
        auto file_path = make_ms_prefix_path(path_, ms_prefix_bits_count_, ms_prefix_index);
        std::filesystem::create_directories(file_path.parent_path());

        std::ofstream file;
        file.open(file_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create empty LSSuffixEntries file at: " + file_path.string());
        }
        file.close();
    }

    [[nodiscard]] std::unique_ptr<LSSuffixEntriesFileWriter<HashBytesCount>>
    create_ls_suffix_entries_file_writer(uint64_t ms_prefix_index) const {
        auto file_path = make_ms_prefix_path(path_, ms_prefix_bits_count_, ms_prefix_index);
        std::filesystem::create_directories(file_path.parent_path());

        return std::make_unique<LSSuffixEntriesFileWriter<HashBytesCount>>(
            typename LSSuffixEntriesFileWriter<HashBytesCount>::Key{},
            file_path,
            pi_bytes_count_,
            ms_prefix_bits_count_
        );
    }

    [[nodiscard]] std::unique_ptr<LSSuffixEntriesFileReader<HashBytesCount>>
    create_ls_suffix_entries_file_reader(uint64_t ms_prefix_index) const {
        auto file_path = make_ms_prefix_path(path_, ms_prefix_bits_count_, ms_prefix_index);

        return std::make_unique<LSSuffixEntriesFileReader<HashBytesCount>>(
            typename LSSuffixEntriesFileReader<HashBytesCount>::Key{},
            file_path,
            pi_bytes_count_,
            ms_prefix_bits_count_
        );
    }

private:
    const std::filesystem::path path_;
    const uint8_t pi_bytes_count_;
    const uint8_t ms_prefix_bits_count_;
};

} // namespace wedding_cake