#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <cstring>
#include <cassert>

#include "types.hpp"
#include "hash_prefix_suffix.hpp"

namespace wedding_cake {

// For a particular donut in a particular tier,
// Calculates entries for the various files that comprise the donut.
// These are:
// HashesOrder.bin
// MsPrefix/12/34_LsSuffixEntries.bin
template<size_t HashBytesCount>
class DonutEntriesCalculator {
public:
    DonutEntriesCalculator(const size_t prefix_bits_count,
                           const PiOffset first_global_pi,
                           const size_t entries_per_donut)
    : prefix_bits_count_(prefix_bits_count)
    , suffix_bits_count_(HashBytesCount * 8 - prefix_bits_count)
    , prefix_bytes_count_((prefix_bits_count + 7) / 8)
    , suffix_bytes_count_((suffix_bits_count_ + 7) / 8)
    , first_global_pi_(first_global_pi)
    , entries_per_donut_(entries_per_donut)
    , bytes_per_local_pi_(calculate_bytes_per_local_pi(entries_per_donut))
    {
    }

    /// Returns the calculated byte size required to encode a LocalPi (including sentinel)
    [[nodiscard]] constexpr uint8_t bytes_per_local_pi() const noexcept {
        return bytes_per_local_pi_;
    }

    /// Calculates the byte size needed for a single entry in HashesOrder.bin
    [[nodiscard]] constexpr size_t hashes_order_entry_size(uint8_t bytes_per_suffix_index) const noexcept {
        return prefix_bytes_count_ + bytes_per_suffix_index;
    }

    /// Calculates the byte size needed for a single entry in XX_Suffixes.bin
    [[nodiscard]] constexpr size_t suffixes_entry_size() const noexcept {
        return suffix_bytes_count_ + bytes_per_local_pi_;
    }

    // =========================================================================
    // ZERO-ALLOCATION SPAN WRITER (Preferred for streaming file IO)
    // =========================================================================

    /// Encodes a single entry into a caller-provided byte destination span.
    /// @param out_buffer Output byte span. MUST have at least `hashes_order_entry_size()` bytes!
    /// @return The actual number of bytes written to `out_buffer`.
    size_t write_hashes_order_file_entry(const HashBytes<HashBytesCount>& hash,
                                         uint64_t suffix_index,
                                         uint8_t bytes_per_suffix_index,
                                         std::span<std::uint8_t> out_buffer) const
    {
        const size_t required_bytes = hashes_order_entry_size(bytes_per_suffix_index);
        assert(out_buffer.size() >= required_bytes && "write_hashes_order_file_entry(): Output buffer too small for entry!");
        assert(bytes_per_suffix_index <= 8 && "write_hashes_order_file_entry(): Suffix index bytes cannot exceed sizeof(uint64_t)");

        // 1. Extract masked prefix bytes and copy directly into destination
        const auto prefix_masked = ms_prefix_bytes_masked(hash, static_cast<uint8_t>(prefix_bits_count_));
        std::memcpy(out_buffer.data(), prefix_masked.bytes.data(), prefix_bytes_count_);

        // 2. Encode uint64_t suffix_index into the next bytes_per_suffix_index bytes (Little-Endian)
        //    On Little-Endian architectures, memcpy'ing the first N bytes of uint64_t extracts LSBs directly!
        static_assert(std::endian::native == std::endian::little, "write_hashes_order_file_entry(): Requires Little-Endian architecture");
        std::memcpy(out_buffer.data() + prefix_bytes_count_, &suffix_index, bytes_per_suffix_index);

        return required_bytes;
    }

    // =========================================================================
    // CONVENIENCE VECTOR WRITER (For unit tests / standalone calls)
    // =========================================================================
    std::vector<std::uint8_t> hashes_order_file_entry(const HashBytes<HashBytesCount>& hash,
                                                      const uint64_t suffix_index,
                                                      const uint8_t bytes_per_suffix_index) const
    {
        std::vector<std::uint8_t> result(hashes_order_entry_size(bytes_per_suffix_index));
        write_hashes_order_file_entry(hash, suffix_index, bytes_per_suffix_index, result);
        return result;
    }

    /// Encodes a single entry into a caller-provided byte destination span.
    /// @param out_buffer Output byte span. MUST have at least `suffixes_entry_size()` bytes!
    /// @return The actual number of bytes written to `out_buffer`.
    size_t write_suffixes_file_entry(const HashBytes<HashBytesCount>& hash,
                                         GlobalPi global_pi,
                                         std::span<std::uint8_t> out_buffer) const
    {
        const size_t required_bytes = suffixes_entry_size();
        assert(out_buffer.size() >= required_bytes && "write_suffixes_file_entry(): Output buffer too small for entry!");

        // 1. Extract masked suffix bytes and copy directly into destination
        const auto suffix_masked = ls_suffix_bytes_masked(hash, static_cast<uint8_t>(prefix_bits_count_));
        std::memcpy(out_buffer.data(), suffix_masked.bytes.data(), suffix_bytes_count_);

        auto local_pi = global_pi - first_global_pi_;
        // 2. Encode uint64_t local_pi into the next bytes_per_local_pi_ bytes (Little-Endian)
        //    On Little-Endian architectures, memcpy'ing the first N bytes of uint64_t extracts LSBs directly!
        static_assert(std::endian::native == std::endian::little, "write_suffixes_file_entry(): Requires Little-Endian architecture");
        std::memcpy(out_buffer.data() + suffix_bytes_count_, &local_pi, bytes_per_local_pi_);

        return required_bytes;
    }

    // =========================================================================
    // CONVENIENCE VECTOR WRITER (For unit tests / standalone calls)
    // =========================================================================
    std::vector<std::uint8_t> suffixes_file_entry(const HashBytes<HashBytesCount>& hash,
                                                      const GlobalPi global_pi) const
    {
        std::vector<std::uint8_t> result(suffixes_entry_size());
        write_suffixes_file_entry(hash, global_pi, result);
        return result;
    }

    static constexpr uint8_t calculate_bytes_per_local_pi(const size_t entries_per_donut) {
        // Some bytes are needed to represent each LocalPi.
        // LocalPi can be 0 to entries_per_donut - 1, but we also need to represent the sentinel value "all ones".
        // We therefore need to count the bytes needed to represent numbers 0 to entries_per_donut.
        // Therefore, the maximum value to store is entries_per_donut.
        if (entries_per_donut == 0) {
            throw std::invalid_argument("entries_per_donut must be greater than 0");
        }

        // std::bit_width returns the number of bits required to store 'entries_per_donut'
        const unsigned int bits_needed = std::bit_width(entries_per_donut);

        // Convert total bits needed into whole byte count (1..8 bytes)
        const uint8_t bytes_needed = static_cast<uint8_t>((bits_needed + 7) / 8);

        assert(bytes_needed <= 8 && "LocalPi cannot exceed 64-bit / 8-byte representation");
        return bytes_needed;
    }

private:
    size_t prefix_bits_count_;
    size_t suffix_bits_count_;
    size_t prefix_bytes_count_;
    size_t suffix_bytes_count_;
    PiOffset first_global_pi_;
    size_t entries_per_donut_;
    uint8_t bytes_per_local_pi_;
};

} // namespace wedding_cake