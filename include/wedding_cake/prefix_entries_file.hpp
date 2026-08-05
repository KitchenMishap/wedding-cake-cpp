#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "wedding_cake/types.hpp"

namespace wedding_cake {

class PrefixEntriesFileWriter;
class PrefixEntriesFileReader;

// Base class containing format metadata and entry representation
class PrefixEntriesFile {
    friend class PrefixEntriesFileWriter;
    friend class PrefixEntriesFileReader;

public:
    struct Entry {
        HashView hash;
        LocalPi local_pi{0};
    };

    [[nodiscard]] size_t entry_size_bytes() const noexcept { return entry_bytes_; }
    [[nodiscard]] uint8_t prefix_bytes_count() const noexcept { return prefix_bytes_; }
    [[nodiscard]] uint8_t local_pi_bytes_count() const noexcept { return local_pi_bytes_; }

protected:
    PrefixEntriesFile(uint8_t hash_bytes, uint8_t tail_bits_count, uint8_t local_pi_bytes)
        : hash_bytes_(hash_bytes),
          tail_bits_count_(tail_bits_count),
          local_pi_bytes_(local_pi_bytes)
    {
        if (hash_bytes_ == 0 || hash_bytes_ > 64) {
            throw std::invalid_argument("hash_bytes must be between 1 and 64");
        }
        if (tail_bits_count_ >= static_cast<uint16_t>(hash_bytes_) * 8) {
            throw std::invalid_argument("tail_bits_count must be strictly less than total hash bits");
        }
        if (local_pi_bytes_ == 0 || local_pi_bytes_ > 8) {
            throw std::invalid_argument("local_pi_bytes must be between 1 and 8");
        }

        uint16_t total_hash_bits = static_cast<uint16_t>(hash_bytes_) * 8;
        uint16_t prefix_bits = total_hash_bits - tail_bits_count_;
        prefix_bytes_ = static_cast<uint8_t>((prefix_bits + 7) / 8);

        entry_bytes_ = prefix_bytes_ + local_pi_bytes_;
        prefix_start_idx_ = hash_bytes_ - prefix_bytes_;
    }

    uint8_t hash_bytes_{0};
    uint8_t tail_bits_count_{0};
    uint8_t local_pi_bytes_{0};
    uint8_t prefix_bytes_{0};
    size_t  prefix_start_idx_{0};
    size_t  entry_bytes_{0};

    static constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024; // 64 KiB internal streaming buffer
};

// ============================================================================
// WRITER CLASS (Write-Only, Sequential Appending, Truncates Existing)
// ============================================================================
class PrefixEntriesFileWriter : public PrefixEntriesFile {
public:
    PrefixEntriesFileWriter(const std::filesystem::path& path,
                            uint8_t hash_bytes,
                            uint8_t tail_bits_count,
                            uint8_t local_pi_bytes)
        : PrefixEntriesFile(hash_bytes, tail_bits_count, local_pi_bytes),
          stream_buffer_(STREAM_BUFFER_SIZE)
    {
        file_.rdbuf()->pubsetbuf(stream_buffer_.data(), stream_buffer_.size());
        file_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to create/overwrite PrefixEntriesFile at: " + path.string());
        }
    }

    ~PrefixEntriesFileWriter() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    PrefixEntriesFileWriter(const PrefixEntriesFileWriter&) = delete;
    PrefixEntriesFileWriter& operator=(const PrefixEntriesFileWriter&) = delete;

    PrefixEntriesFileWriter(PrefixEntriesFileWriter&& other) noexcept
        : PrefixEntriesFile(std::move(other)),
          stream_buffer_(std::move(other.stream_buffer_)),
          file_(std::move(other.file_)) {}

    PrefixEntriesFileWriter& operator=(PrefixEntriesFileWriter&& other) noexcept {
        if (this != &other) {
            PrefixEntriesFile::operator=(std::move(other));
            if (file_.is_open()) {
                file_.close();
            }
            stream_buffer_ = std::move(other.stream_buffer_);
            file_ = std::move(other.file_);
        }
        return *this;
    }

    // Appends an entry's prefix and LocalPi to disk
    void append(const Entry& entry) {
        if (entry.hash.size() < hash_bytes_) {
            throw std::invalid_argument("Provided HashView is smaller than configured hash_bytes");
        }

        // 1. Write prefix bytes from high-order end of HashView span
        const uint8_t* prefix_ptr = entry.hash.bytes().data() + prefix_start_idx_;
        file_.write(reinterpret_cast<const char*>(prefix_ptr), prefix_bytes_);

        // 2. Write LocalPi bytes (Little-Endian raw integer)
        uint64_t pi_raw = entry.local_pi.to_int();
        file_.write(reinterpret_cast<const char*>(&pi_raw), local_pi_bytes_);
    }

    void flush() {
        file_.flush();
    }

private:
    std::vector<char> stream_buffer_;
    std::ofstream file_;
};

// ============================================================================
// READER CLASS (Read-Only, Sequential + Random-Access Seeking)
// ============================================================================
class PrefixEntriesFileReader : public PrefixEntriesFile {
public:
    PrefixEntriesFileReader(const std::filesystem::path& path,
                            uint8_t hash_bytes,
                            uint8_t tail_bits_count,
                            uint8_t local_pi_bytes)
        : PrefixEntriesFile(hash_bytes, tail_bits_count, local_pi_bytes),
          stream_buffer_(STREAM_BUFFER_SIZE)
    {
        file_.rdbuf()->pubsetbuf(stream_buffer_.data(), stream_buffer_.size());
        file_.open(path, std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open PrefixEntriesFile at: " + path.string());
        }
    }

    ~PrefixEntriesFileReader() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    PrefixEntriesFileReader(const PrefixEntriesFileReader&) = delete;
    PrefixEntriesFileReader& operator=(const PrefixEntriesFileReader&) = delete;

    PrefixEntriesFileReader(PrefixEntriesFileReader&& other) noexcept
        : PrefixEntriesFile(std::move(other)),
          stream_buffer_(std::move(other.stream_buffer_)),
          file_(std::move(other.file_)) {}

    PrefixEntriesFileReader& operator=(PrefixEntriesFileReader&& other) noexcept {
        if (this != &other) {
            PrefixEntriesFile::operator=(std::move(other));
            if (file_.is_open()) {
                file_.close();
            }
            stream_buffer_ = std::move(other.stream_buffer_);
            file_ = std::move(other.file_);
        }
        return *this;
    }

    // Seeks to zero-based entry index
    bool seek_entry(uint64_t index) {
        auto pos = static_cast<std::streamoff>(index * entry_bytes_);
        file_.clear(); // Clear potential EOF/fail flags before seeking
        file_.seekg(pos, std::ios::beg);
        return file_.good();
    }

    [[nodiscard]] bool has_more() {
        if (!file_ || entry_bytes_ == 0) return false;

        auto next_char = file_.peek();
        if (next_char == std::ifstream::traits_type::eof()) {
            file_.clear(); // Clear eofbit set by peek()
            return false;
        }
        return true;
    }

    // Reads the next prefix directly into the high-order bytes of out_hash_buf
    // and populates out_local_pi.
    // out_hash_buf MUST have size >= hash_bytes_. Low-order tail bytes are left untouched.
    bool read_next(std::span<uint8_t> out_hash_buf, LocalPi& out_local_pi) {
        if (!file_ || entry_bytes_ == 0) return false;
        if (out_hash_buf.size() < hash_bytes_) {
            throw std::invalid_argument("Destination buffer is smaller than configured hash_bytes");
        }

        // 1. Read prefix bytes directly into the high-order section of out_hash_buf
        uint8_t* prefix_ptr = out_hash_buf.data() + prefix_start_idx_;
        file_.read(reinterpret_cast<char*>(prefix_ptr), prefix_bytes_);

        if (static_cast<size_t>(file_.gcount()) < prefix_bytes_) {
            return false;
        }

        // 2. Read LocalPi bytes
        uint64_t pi_raw = 0;
        file_.read(reinterpret_cast<char*>(&pi_raw), local_pi_bytes_);

        if (static_cast<size_t>(file_.gcount()) < local_pi_bytes_) {
            return false;
        }

        uint64_t pi_mask = (local_pi_bytes_ == 8) ? ~0ULL : ((1ULL << (local_pi_bytes_ * 8)) - 1);
        out_local_pi = LocalPi(pi_raw & pi_mask);

        return true;
    }

private:
    std::vector<char> stream_buffer_;
    std::ifstream file_;
};

} // namespace wedding_cake