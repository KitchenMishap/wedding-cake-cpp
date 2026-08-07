#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <utility>

#include "wedding_cake/types.hpp"

namespace wedding_cake {

// HashesOrderFileWriter and HashesOrderFileReader, for thread safety, can only be constructed by ReaderWriterFactory
class HashesOrderFileWriter;
class HashesOrderFileReader;

// Private base class
class HashesOrderFile {
    friend HashesOrderFileWriter;
    friend HashesOrderFileReader;
public:
    struct Entry {
        LocalPi local_pi{LOCAL_PI_NO_MATCH};
        uint64_t tail_bits{0};
        uint8_t spare_bits{0}; // Top unused bits in the tail_bits field
    };

    [[nodiscard]] size_t entry_size_bytes() const noexcept { return entry_bytes_count_; }
    [[nodiscard]] uint8_t tail_bits_count() const noexcept { return tail_bits_count_; }
    [[nodiscard]] uint8_t spare_bits_count() const noexcept {
        return (tail_bits_bytes_count_ * 8) - tail_bits_count_;
    }

private:
    HashesOrderFile(const uint8_t local_pi_bytes_count,     // Ctor permitted when deriving from HashesOrderFileReader/Writer
                    const uint8_t tail_bits_count)
        : local_pi_bytes_count_(local_pi_bytes_count),
          tail_bits_count_(tail_bits_count),
          tail_bits_bytes_count_((tail_bits_count + 7) / 8),
          entry_bytes_count_(local_pi_bytes_count_ + tail_bits_bytes_count_) {
        if (local_pi_bytes_count_ == 0 || local_pi_bytes_count_ > 8) {
            throw std::invalid_argument("local_pi_bytes must be between 1 and 8");
        }
        if (tail_bits_count_ > 64) {
            throw std::invalid_argument("tail_bits_count cannot exceed 64");
        }
    }
protected:
    uint8_t local_pi_bytes_count_{0};
    uint8_t tail_bits_count_{0};
    uint8_t tail_bits_bytes_count_{0};
    size_t  entry_bytes_count_{0};
};


class HashesOrderFileWriter : public HashesOrderFile {
public:
    // Token that only ReaderWriterFactory can create
    class Key {
        friend class ReaderWriterFactory;
        explicit Key() = default;
    };

    // Creates a file managed internally by path
    HashesOrderFileWriter( Key,
                    const std::filesystem::path& path,
                    uint8_t local_pi_bytes_count,
                    uint8_t tail_bits_count)
    : HashesOrderFile(local_pi_bytes_count, tail_bits_count)
    {
        // Open in binary mode for writing (append)
        file_.open(path, std::ios::out | std::ios::app | std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open HashesOrderFile at: " + path.string());
        }
    }

    ~HashesOrderFileWriter() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Non-copyable, non-movable, to prevent cross-thread sharing
    HashesOrderFileWriter(const HashesOrderFileWriter&) = delete;
    HashesOrderFileWriter& operator=(const HashesOrderFileWriter&) = delete;
    HashesOrderFileWriter(HashesOrderFileWriter&& other) = delete;
    HashesOrderFileWriter& operator=(HashesOrderFileWriter&& other)  = delete;

    // Appends to file
    void append(const Entry& entry) {
        // 1. Prepare LocalPi bytes (Little-Endian)
        uint64_t pi_raw = 0;
        if (entry.local_pi.is_no_match()) {
            pi_raw = ~0ULL; // Write all ones (0xFF)
        } else {
            pi_raw = entry.local_pi.to_int();
        }
        file_.write(reinterpret_cast<const char*>(&pi_raw), local_pi_bytes_count_);

        // 2. Prepare tail_bits & spare_bits bytes (Little-Endian)
        if (tail_bits_bytes_count_ > 0) {
            uint64_t tail_field_raw = 0;

            if (tail_bits_count_ > 0) {
                uint64_t tail_mask = (tail_bits_count_ == 64) ? ~0ULL : ((1ULL << tail_bits_count_) - 1);
                tail_field_raw |= (entry.tail_bits & tail_mask);
            }
            
            uint8_t spare_count = spare_bits_count();
            if (spare_count > 0) {
                uint64_t spare_mask = (spare_count == 64) ? ~0ULL : ((1ULL << spare_count) - 1);
                tail_field_raw |= ((entry.spare_bits & spare_mask) << tail_bits_count_);
            }

            file_.write(reinterpret_cast<const char*>(&tail_field_raw), tail_bits_bytes_count_);
        }
    }

    void flush() {
        file_.flush();
    }

private:
    std::ofstream file_;
};

class ReaderWriterFactory;

class HashesOrderFileReader : public HashesOrderFile {
public:
    // Token only ReaderWriterFactory can access
    class Key {
        friend ReaderWriterFactory;
        explicit Key() = default;
    };

    // Opens (or creates) a file managed internally by path
    HashesOrderFileReader(Key,
                    const std::filesystem::path& path,
                    uint8_t local_pi_bytes_count,
                    uint8_t tail_bits_count)
        : HashesOrderFile(local_pi_bytes_count, tail_bits_count)
    {
        // Open in binary mode for reading
        file_.open(path, std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open HashesOrderFile at: " + path.string());
        }
    }

    ~HashesOrderFileReader() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Non-copyable, non-movable, to prevent cross-thread sharing
    HashesOrderFileReader(const HashesOrderFileReader&) = delete;
    HashesOrderFileReader& operator=(const HashesOrderFileReader&) = delete;
    HashesOrderFileReader(HashesOrderFileReader&& other) = delete;
    HashesOrderFileReader& operator=(HashesOrderFileReader&& other) = delete;

    // Seeks to zero-based entry index
    bool seek_entry(uint64_t index) {
        const auto pos = static_cast<std::streamoff>(index * entry_bytes_count_);
        file_.seekg(pos, std::ios::beg);
        return file_.good();
    }

    [[nodiscard]] bool has_more() {
        // peek() checks if the next byte exists without advancing the read position
        return file_.peek() != std::ifstream::traits_type::eof();
    }

    // Reads entry at current file position
    [[nodiscard]] Entry read_next() {
        Entry entry{};
        if (entry_bytes_count_ == 0) return entry;

        // 1. Read LocalPi bytes (Little-Endian)
        if (!has_more()) return entry;
        uint64_t pi_raw = 0;
        file_.read(reinterpret_cast<char*>(&pi_raw), local_pi_bytes_count_);

        // Check if all N bytes read are 0xFF (sentinel for LOCAL_PI_NO_MATCH)
        uint64_t max_pi_mask = (local_pi_bytes_count_ == 8) ? ~0ULL : ((1ULL << (local_pi_bytes_count_ * 8)) - 1);
        if ((pi_raw & max_pi_mask) == max_pi_mask) {
            entry.local_pi = LOCAL_PI_NO_MATCH;
        } else {
            entry.local_pi = LocalPi(pi_raw & max_pi_mask);
        }

        // 2. Read tail_bits & spare_bits bytes (if tail_bits_bytes_ > 0)
        if (tail_bits_bytes_count_ > 0) {
            uint64_t tail_field_raw = 0;
            file_.read(reinterpret_cast<char*>(&tail_field_raw), tail_bits_bytes_count_);

            if (tail_bits_count_ == 0) {
                entry.tail_bits = 0;
                entry.spare_bits = tail_field_raw;
            } else {
                uint64_t tail_mask = (tail_bits_count_ == 64) ? ~0ULL : ((1ULL << tail_bits_count_) - 1);
                entry.tail_bits = tail_field_raw & tail_mask;
                entry.spare_bits = tail_field_raw >> tail_bits_count_;
            }
        }

        return entry;
    }

private:
    std::ifstream file_;
};

} // namespace wedding_cake