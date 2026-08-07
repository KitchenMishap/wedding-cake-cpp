#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "hashes_order_file.hpp"
#include "prefix_entries_file.hpp"
#include "prefix_entries_path.hpp"

namespace wedding_cake {

class PrefixEntriesFileReader;
class PrefixEntriesFileWriter;

class ReaderWriterFactory {
public:
    ReaderWriterFactory(std::filesystem::path  path,
        const uint8_t hash_bytes_count,
        const uint8_t pi_bytes_count,
        const uint8_t tail_bits_count)
            : path_(std::move(path)),
                hash_bytes_count_(hash_bytes_count),
                pi_bytes_count_(pi_bytes_count),
                tail_bits_count_(tail_bits_count) {
        if (hash_bytes_count_ == 0) {
            throw std::invalid_argument("hash_bytes_count must be greater than 0");
        }
        if (tail_bits_count_ >= static_cast<uint16_t>(hash_bytes_count_) * 8) {
            throw std::invalid_argument("tail_bits_count must be strictly less than total hash bits");
        }
        if (pi_bytes_count_ == 0 || pi_bytes_count_ > 8) {
            throw std::invalid_argument("pi_bytes_count must be between 1 and 8");
        }
    }

    // HashesOrder file
    void create_empty_hashes_order_file() const {
        std::ofstream file;
        file.open(path_ / "HashesOrder" , std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create empty \"HashesOrder\" file at: " + path_.string());
        }
        file.close();
    }
    [[nodiscard]] std::unique_ptr<HashesOrderFileWriter> create_hashes_order_file_writer() const {
        return std::make_unique<HashesOrderFileWriter>(
            HashesOrderFileWriter::Key{}, path_ / "HashesOrder", pi_bytes_count_, tail_bits_count_
        );
    }
    [[nodiscard]] std::unique_ptr<HashesOrderFileReader> create_hashes_order_file_reader() const {
        return std::make_unique<HashesOrderFileReader>(
            HashesOrderFileReader::Key{}, path_ / "HashesOrder", pi_bytes_count_, tail_bits_count_
        );
    }

    // PrefixEntries file
    void create_empty_prefix_entries_file(uint64_t tail_index) const {
        auto file_path = make_prefix_entries_path(path_, tail_bits_count_, tail_index);
        std::filesystem::create_directories(file_path.parent_path());

        std::ofstream file;
        file.open(file_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create empty \"PrefixEntries\" file at: " + file_path.string());
        }
        file.close();
    }

    [[nodiscard]] std::unique_ptr<PrefixEntriesFileWriter> create_prefix_entries_file_writer(uint64_t tail_index) const {
        auto file_path = make_prefix_entries_path(path_, tail_bits_count_, tail_index);
        std::filesystem::create_directories(file_path.parent_path());

        return std::make_unique<PrefixEntriesFileWriter>(
            PrefixEntriesFileWriter::Key{}, file_path, hash_bytes_count_, pi_bytes_count_, tail_bits_count_);
    }

    [[nodiscard]] std::unique_ptr<PrefixEntriesFileReader> create_prefix_entries_file_reader(uint64_t tail_index) const {
        auto file_path = make_prefix_entries_path(path_, tail_bits_count_, tail_index);

        return std::make_unique<PrefixEntriesFileReader>(
            PrefixEntriesFileReader::Key{}, file_path, hash_bytes_count_, pi_bytes_count_, tail_bits_count_);
    }

private:
    const std::filesystem::path path_;
    const uint8_t hash_bytes_count_;
    const uint8_t pi_bytes_count_;
    const uint8_t tail_bits_count_;
};

};
