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

#include "../../apps/wedding_cake/include/types.hpp"

namespace wedding_cake {

template <size_t HashBytesCount>
class LSSuffixEntriesFileWriter;

template <size_t HashBytesCount>
class LSSuffixEntriesFileReader;

template <size_t HashBytesCount>
class ReaderWriterFactory;

// Base class containing format metadata and entry representation
template <size_t HashBytesCount>
class LSSuffixEntriesFile {
    template <size_t N>
    friend class LSSuffixEntriesFileWriter;
    template <size_t N>
    friend class LSSuffixEntriesFileReader;

public:
    struct Entry {
        HashView hash;
        LocalPi local_pi{0};
    };

    static constexpr size_t hash_bytes_count = HashBytesCount;

    [[nodiscard]] size_t entry_size_bytes() const noexcept { return entry_bytes_; }
    [[nodiscard]] uint8_t ls_suffix_bytes_count() const noexcept { return ls_suffix_bytes_count_; }
    [[nodiscard]] uint8_t local_pi_bytes_count() const noexcept { return local_pi_bytes_count_; }

protected:
    LSSuffixEntriesFile(const uint8_t local_pi_bytes_count, const uint8_t ms_prefix_bits_count)
        : local_pi_bytes_count_(local_pi_bytes_count),
          ms_prefix_bits_count_(ms_prefix_bits_count)
    {
        static_assert(HashBytesCount >= 1 && HashBytesCount <= 64, "HashBytesCount must be between 1 and 64");

        const uint16_t total_hash_bits = static_cast<uint16_t>(HashBytesCount) * 8;
        if (ms_prefix_bits_count_ >= total_hash_bits) {
            throw std::invalid_argument("ms_prefix_bits_count must be strictly less than total hash bits");
        }
        if (local_pi_bytes_count_ == 0 || local_pi_bytes_count_ > 8) {
            throw std::invalid_argument("local_pi_bytes_count must be between 1 and 8");
        }

        const uint16_t ls_suffix_bits = total_hash_bits - ms_prefix_bits_count_;
        ls_suffix_bytes_count_ = static_cast<uint8_t>((ls_suffix_bits + 7) / 8);

        entry_bytes_ = ls_suffix_bytes_count_ + local_pi_bytes_count_;
        suffix_start_idx_ = HashBytesCount - ls_suffix_bytes_count_;
    }

    uint8_t local_pi_bytes_count_{0};
    uint8_t ms_prefix_bits_count_{0};
    uint8_t ls_suffix_bytes_count_{0};
    size_t  suffix_start_idx_{0};
    size_t  entry_bytes_{0};

    static constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024; // 64 KiB internal streaming buffer
};

// ============================================================================
// WRITER CLASS (Write-Only, Sequential Appending)
// ============================================================================
template <size_t HashBytesCount>
class LSSuffixEntriesFileWriter : public LSSuffixEntriesFile<HashBytesCount> {
    using Base = LSSuffixEntriesFile<HashBytesCount>;
public:
    // Token that only ReaderWriterFactory can construct
    class Key {
        friend class ReaderWriterFactory<HashBytesCount>;
        explicit Key() = default;
    };

    LSSuffixEntriesFileWriter(Key,
                              const std::filesystem::path& path,
                              uint8_t local_pi_bytes_count,
                              uint8_t ms_prefix_bits_count)
        : Base(local_pi_bytes_count, ms_prefix_bits_count),
          stream_buffer_(Base::STREAM_BUFFER_SIZE)
    {
        file_.rdbuf()->pubsetbuf(stream_buffer_.data(), stream_buffer_.size());
        file_.open(path, std::ios::out | std::ios::binary | std::ios::app);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open LSSuffixEntriesFile at: " + path.string());
        }
    }

    ~LSSuffixEntriesFileWriter() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Non-copyable, non-movable to prevent cross-thread sharing
    LSSuffixEntriesFileWriter(const LSSuffixEntriesFileWriter&) = delete;
    LSSuffixEntriesFileWriter& operator=(const LSSuffixEntriesFileWriter&) = delete;
    LSSuffixEntriesFileWriter(LSSuffixEntriesFileWriter&& other) = delete;
    LSSuffixEntriesFileWriter& operator=(LSSuffixEntriesFileWriter&& other) = delete;

    // Appends an entry's suffix bytes and LocalPi to disk
    void append(const typename Base::Entry& entry) {
        if (entry.hash.size() < HashBytesCount) {
            throw std::invalid_argument("Provided HashView is smaller than configured HashBytesCount");
        }

        // 1. Write suffix bytes starting from the low-order offset
        const uint8_t* suffix_ptr = entry.hash.bytes().data() + this->suffix_start_idx_;
        file_.write(reinterpret_cast<const char*>(suffix_ptr), this->ls_suffix_bytes_count_);

        // 2. Write LocalPi bytes (Little-Endian raw integer)
        uint64_t pi_raw = entry.local_pi.to_int();
        file_.write(reinterpret_cast<const char*>(&pi_raw), this->local_pi_bytes_count_);
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
template <size_t HashBytesCount>
class LSSuffixEntriesFileReader : public LSSuffixEntriesFile<HashBytesCount> {
    using Base = LSSuffixEntriesFile<HashBytesCount>;
public:
    class Key {
        friend class ReaderWriterFactory<HashBytesCount>;
        explicit Key() = default;
    };

    LSSuffixEntriesFileReader(Key,
                              const std::filesystem::path& path,
                              uint8_t local_pi_bytes_count,
                              uint8_t ms_prefix_bits_count)
        : Base(local_pi_bytes_count, ms_prefix_bits_count),
          stream_buffer_(Base::STREAM_BUFFER_SIZE)
    {
        file_.rdbuf()->pubsetbuf(stream_buffer_.data(), stream_buffer_.size());
        file_.open(path, std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open LSSuffixEntriesFile at: " + path.string());
        }
    }

    ~LSSuffixEntriesFileReader() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Non-copyable, non-movable
    LSSuffixEntriesFileReader(const LSSuffixEntriesFileReader&) = delete;
    LSSuffixEntriesFileReader& operator=(const LSSuffixEntriesFileReader&) = delete;
    LSSuffixEntriesFileReader(LSSuffixEntriesFileReader&& other) = delete;
    LSSuffixEntriesFileReader& operator=(LSSuffixEntriesFileReader&& other) = delete;

    // Seeks to zero-based entry index
    bool seek_entry(uint64_t index) {
        auto pos = static_cast<std::streamoff>(index * this->entry_bytes_);
        file_.clear();
        file_.seekg(pos, std::ios::beg);
        return file_.good();
    }

    [[nodiscard]] bool has_more() {
        if (!file_ || this->entry_bytes_ == 0) return false;

        auto next_char = file_.peek();
        if (next_char == std::ifstream::traits_type::eof()) {
            file_.clear();
            return false;
        }
        return true;
    }

    // Reads the next suffix directly into out_hash_buf and populates out_local_pi
    bool read_next(std::span<uint8_t> out_hash_buf, LocalPi& out_local_pi) {
        if (!file_ || this->entry_bytes_ == 0) return false;
        if (out_hash_buf.size() < HashBytesCount) {
            throw std::invalid_argument("Destination buffer is smaller than configured HashBytesCount");
        }

        // 1. Read suffix bytes directly into out_hash_buf
        uint8_t* suffix_ptr = out_hash_buf.data() + this->suffix_start_idx_;
        file_.read(reinterpret_cast<char*>(suffix_ptr), this->ls_suffix_bytes_count_);

        if (static_cast<size_t>(file_.gcount()) < this->ls_suffix_bytes_count_) {
            return false;
        }

        // 2. Read LocalPi bytes
        uint64_t pi_raw = 0;
        file_.read(reinterpret_cast<char*>(&pi_raw), this->local_pi_bytes_count_);

        if (static_cast<size_t>(file_.gcount()) < this->local_pi_bytes_count_) {
            return false;
        }

        uint64_t pi_mask = (this->local_pi_bytes_count_ == 8) ? ~0ULL : ((1ULL << (this->local_pi_bytes_count_ * 8)) - 1);
        out_local_pi = LocalPi(pi_raw & pi_mask);

        return true;
    }

private:
    std::vector<char> stream_buffer_;
    std::ifstream file_;
};

} // namespace wedding_cake