#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <cstring>
#include <cassert>
#include <bit>
#include <optional>
#include <stdexcept>

#include "types.hpp"
#include "hash_prefix_suffix.hpp"

namespace wedding_cake {

// For a particular donut in a particular tier,
// Calculates entries for the various files that comprise the donut.
// These are:
// HashesOrder.bin
// MsPrefix/12/34_LsSuffixEntries.bin
template<size_t HashBytesCount>
class DonutEntriesWriteCalculator {
public:
    DonutEntriesWriteCalculator(const size_t prefix_bits_count,
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
    size_t write_hashes_order_file_entry(const std::optional<HashBytes<HashBytesCount>>& optional_hash,
                                         uint64_t suffix_index,
                                         uint8_t bytes_per_suffix_index,
                                         std::span<std::uint8_t> out_buffer) const
    {
        const size_t required_bytes = hashes_order_entry_size(bytes_per_suffix_index);
        assert(out_buffer.size() >= required_bytes && "write_hashes_order_file_entry(): Output buffer too small for entry!");
        assert(bytes_per_suffix_index <= 8 && "write_hashes_order_file_entry(): Suffix index bytes cannot exceed sizeof(uint64_t)");

        // 0. Special case. If no hash is specified
        if (optional_hash==std::nullopt) {
            // Write all ones (FF...). For both suffix index field, and for prefix field. To represent "No Hash"
            std::memset(out_buffer.data(), 0xFF, required_bytes);
            return required_bytes;
        }

        // 1. Extract masked prefix bytes and copy directly into destination
        const auto prefix_masked = ms_prefix_bytes_masked(optional_hash.value(), static_cast<uint8_t>(prefix_bits_count_));
        std::memcpy(out_buffer.data(), prefix_masked.bytes.data(), prefix_bytes_count_);

        // 2. Encode uint64_t suffix_index into the next bytes_per_suffix_index bytes (Little-Endian)
        //    On Little-Endian architectures, memcpy'ing the first N bytes of uint64_t extracts LSBs directly!
        static_assert(std::endian::native == std::endian::little, "write_hashes_order_file_entry(): Requires Little-Endian architecture");
        std::memcpy(out_buffer.data() + prefix_bytes_count_, &suffix_index, bytes_per_suffix_index);

        // --- DEBUG ASSERTION ---
        // Verify that a valid suffix_index did not inadvertently encode as all 0xFF (sentinel conflict)
        {
            bool suffix_bytes_all_ones = true;
            for (size_t i = 0; i < bytes_per_suffix_index; ++i) {
                if (out_buffer[prefix_bytes_count_ + i] != 0xFF) {
                    suffix_bytes_all_ones = false;
                    break;
                }
            }
            if (suffix_bytes_all_ones) {
                std::cout << "Breakpoint here";
            }
            assert(!suffix_bytes_all_ones &&
                   "write_hashes_order_file_entry(): Valid suffix_index collision with 0xFF... sentinel! "
                   "bytes_per_suffix_index is too small to represent this capacity + sentinel.");
        }
        return required_bytes;
    }

    // =========================================================================
    // CONVENIENCE VECTOR WRITER (For unit tests / standalone calls)
    // =========================================================================
    std::vector<std::uint8_t> hashes_order_file_entry(const std::optional<HashBytes<HashBytesCount>>& optional_hash,
                                                      const uint64_t suffix_index,
                                                      const uint8_t bytes_per_suffix_index) const
    {
        std::vector<std::uint8_t> result(hashes_order_entry_size(bytes_per_suffix_index));
        auto count = write_hashes_order_file_entry(optional_hash, suffix_index, bytes_per_suffix_index, result);
        assert(count == result.size() && "write_hashes_order_file_entry(): Failed to write all bytes to output buffer!");
        return result;
    }

    /// Encodes a single entry into a caller-provided byte destination span.
    /// @param out_buffer Output byte span. MUST have at least `suffixes_entry_size()` bytes!
    /// @return The actual number of bytes written to `out_buffer`.
    size_t write_suffixes_file_entry(const std::optional<HashBytes<HashBytesCount>>& optional_hash,
                                         const GlobalPi global_pi,
                                         std::span<std::uint8_t> out_buffer) const
    {
        const size_t required_bytes = suffixes_entry_size();
        assert(out_buffer.size() >= required_bytes && "write_suffixes_file_entry(): Output buffer too small for entry!");

        // 0. Special case. If no hash is provided, there is no output at all
        if (optional_hash==std::nullopt) {
            return 0;
        }

        // 1. Extract masked suffix bytes and copy directly into destination
        const auto suffix_masked = ls_suffix_bytes_masked(optional_hash.value(), static_cast<uint8_t>(prefix_bits_count_));
        std::memcpy(out_buffer.data(), suffix_masked.bytes.data(), suffix_bytes_count_);

        auto local_pi_int = (global_pi - first_global_pi_).to_int();
        // 2. Encode uint64_t local_pi into the next bytes_per_local_pi_ bytes (Little-Endian)
        //    On Little-Endian architectures, memcpy'ing the first N bytes of uint64_t extracts LSBs directly!
        static_assert(std::endian::native == std::endian::little, "write_suffixes_file_entry(): Requires Little-Endian architecture");
        std::memcpy(out_buffer.data() + suffix_bytes_count_, &local_pi_int, bytes_per_local_pi_);

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

protected:
    size_t prefix_bits_count_;
    size_t suffix_bits_count_;
    size_t prefix_bytes_count_;
    size_t suffix_bytes_count_;
    PiOffset first_global_pi_;
    size_t entries_per_donut_;
    uint8_t bytes_per_local_pi_;
};

// Now we turn our attention to reading entries of the HashesOrder.bin and XX_LsSuffixEntries.bin files.
template<size_t HashBytesCount>
class DonutEntriesCalculator : public DonutEntriesWriteCalculator<HashBytesCount> {
    using Base = DonutEntriesWriteCalculator<HashBytesCount>;

public:
    DonutEntriesCalculator(size_t prefix_bits_count, PiOffset first_global_pi, size_t entries_per_donut)
        : Base(prefix_bits_count, first_global_pi, entries_per_donut)
    {
    }

    // Zero-allocation span-based struct for HashesOrder.bin entries
    struct HashesOrderEntry {
        MsPrefixBytes ms_prefix_bytes;
        uint64_t suffixes_file_index{0};
        bool is_sentinel{false}; // True if entry was non-hash 0xFF...
    };

    // Zero-allocation span-based struct for XX_LsSuffixEntries.bin entries
    struct LsSuffixEntriesEntry {
        LsSuffixBytes ls_suffix_bytes;
        LocalPi local_pi{0};
    };

    /// Reconstructs the original full HashBytes<HashBytesCount> from masked prefix and suffix bytes.
    /// Perfectly handles overlapping byte when prefix_bits_count is not a multiple of 8.
    HashBytes<HashBytesCount> hash_from_prefix_suffix(
        MsPrefixBytes ms_prefix,
        LsSuffixBytes ls_suffix) const
    {
        HashBytes<HashBytesCount> full_hash{};

        // 1. Copy suffix bytes to LSB side (starts at index 0)
        std::memcpy(full_hash.bytes.data(), ls_suffix.bytes.data(), this->suffix_bytes_count_);

        // 2. Determine offset where prefix bytes begin (MSB side)
        const size_t prefix_start_idx = HashBytesCount - this->prefix_bytes_count_;

        if (this->prefix_bits_count_ % 8 == 0) {
            // Clean alignment: Prefix bytes copy directly to top bytes without byte overlap
            std::memcpy(full_hash.bytes.data() + prefix_start_idx, ms_prefix.bytes.data(), this->prefix_bytes_count_);
        } else {
            // Overlapping byte case:
            // High bytes of prefix copy directly
            if (this->prefix_bytes_count_ > 1) {
                std::memcpy(full_hash.bytes.data() + prefix_start_idx + 1,
                            ms_prefix.bytes.data() + 1,
                            this->prefix_bytes_count_ - 1);
            }

            // The overlap byte combines suffix MSBs and prefix LSBs via bitwise OR
            full_hash.bytes[prefix_start_idx] |= ms_prefix.bytes[0];
        }

        return full_hash;
    }

    /// Reads a single entry from HashesOrder.bin bytes without allocations.
    void read_hashes_order_entry_from_bytes(
        std::span<const uint8_t> bytes,
        uint8_t bytes_per_suffix_index,
        HashesOrderEntry& target) const
    {
        const size_t required = this->hashes_order_entry_size(bytes_per_suffix_index);
        assert(bytes.size() >= required && "read_hashes_order_entry_from_bytes(): Span too short!");

        // 0. Check for Sentinel "No Hash" state (0xFF across whole entry)
        bool all_ones = true;
        for (size_t i = 0; i < required; ++i) {
            if (bytes[i] != 0xFF) {
                all_ones = false;
                break;
            }
        }

        if (all_ones) {
            target.is_sentinel = true;
            target.ms_prefix_bytes = MsPrefixBytes(bytes.subspan(0, this->prefix_bytes_count_));
            target.suffixes_file_index = ~0ULL; // Max uint64
            return;
        }

        target.is_sentinel = false;

        // 1. Bind prefix view
        target.ms_prefix_bytes = MsPrefixBytes(bytes.subspan(0, this->prefix_bytes_count_));

        // 2. Decode Little-Endian suffix file index
        static_assert(std::endian::native == std::endian::little, "Requires Little-Endian architecture");
        uint64_t val = 0;
        std::memcpy(&val, bytes.data() + this->prefix_bytes_count_, bytes_per_suffix_index);
        target.suffixes_file_index = val;
    }

    /// Reads a single entry from XX_LsSuffixEntries.bin bytes without allocations.
    void read_ls_suffixes_entry_from_bytes(
        std::span<const uint8_t> bytes,
        LsSuffixEntriesEntry& target) const
    {
        const size_t required = this->suffixes_entry_size();
        assert(bytes.size() >= required && "read_ls_suffixes_entry_from_bytes(): Span too short!");

        // 1. Bind suffix view
        target.ls_suffix_bytes = LsSuffixBytes(bytes.subspan(0, this->suffix_bytes_count_));

        // 2. Decode Little-Endian local_pi
        static_assert(std::endian::native == std::endian::little, "Requires Little-Endian architecture");
        size_t local_pi_val = 0;
        std::memcpy(&local_pi_val, bytes.data() + this->suffix_bytes_count_, this->bytes_per_local_pi_);
        target.local_pi = LocalPi{local_pi_val};
    }
};

} // namespace wedding_cake