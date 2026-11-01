#pragma once

#include "types.hpp"

#include <cstdint>
#include <span>
#include <vector>
#include <array>
#include <cstring>
#include <cassert>
#include <filesystem>
#include <format>

// Here we define what is meant by the prefix and suffix of an N bit hash is, for a chosen prefix_bits_count = n <= 64.
// A hash is a sequence of bytes, but we consider it to be a very large number, perhaps with N=256 bits.
// When considered this way, the sequence of bytes are stored Little Endian (LSB first).
// When a hash is displayed to a user, it is displayed Big Endian (MSB first).
// Thus we consider the prefix to be the most significant bits of the hash, and the suffix to be least significant.
// The prefix is at the beginning for the user display, but corresponds to the end of the hash in memory.
// Furthermore, n = prefix_bits_count is NOT limited to being a multiple of 8 bits (bytes), nor 4 bits (nibbles).
// We introduce PrefixBytes:
//      The whole bytes subset from the hash that are enough to contain the n bit prefix.
// We introduce SuffixBytes:
//      The whole bytes subset from the hash that are enough to contain the N-n bit suffix.
// When n is not a multiple of 8, there is a hash byte that is in both PrefixBytes and SuffixBytes.
// In such a case, len(PrefixBytes) + len(SuffixBytes) = N+1 instead of N.
// ...the PrefixBytes and SuffixBytes overlap by one byte.
// PrefixBytesMasked is PrefixBytes, but with the bits masked with zeros to only contain the n bit prefix.
// SuffixBytesMasked is SuffixBytes, but with the bits masked with zeros to only contain the N-n bit suffix.
// We introduce the concept of PrefixIndex:
//      An n bit number P, stored as a uint64_t, with 0 <= P < 2^prefix_bits_count.
//      PrefixIndex corresponds to PrefixBytesShifted zero-padded to a 64 bit number.

namespace wedding_cake {

// Strong, non-allocating view wrappers
struct MsPrefixBytes {
    explicit MsPrefixBytes(const std::span<const std::uint8_t> span) : bytes(span) {}
    std::span<const std::uint8_t> bytes;

    // Explicit conversions/accessors for convenience
    constexpr auto begin() const { return bytes.begin(); }
    constexpr auto end() const { return bytes.end(); }
    constexpr auto size() const { return bytes.size(); }
    constexpr auto operator[](const size_t idx) const { return bytes[idx]; }
};
struct LsSuffixBytes {
    explicit LsSuffixBytes(const std::span<const std::uint8_t> span) : bytes(span) {}
    std::span<const std::uint8_t> bytes;

    constexpr auto begin() const { return bytes.begin(); }
    constexpr auto end() const { return bytes.end(); }
    constexpr auto size() const { return bytes.size(); }
    constexpr auto operator[](const size_t idx) const { return bytes[idx]; }
};

// Masked byte buffers (using fixed stack buffers or std::vector depending on need)
struct MsPrefixBytesMasked {
    std::vector<std::uint8_t> bytes;

    // Explicit conversions/accessors for convenience
    constexpr auto begin() const { return bytes.begin(); }
    constexpr auto end() const { return bytes.end(); }
    constexpr auto size() const { return bytes.size(); }
    constexpr auto operator[](const size_t idx) const { return bytes[idx]; }
};
struct LsSuffixBytesMasked {
    std::vector<std::uint8_t> bytes;

    constexpr auto begin() const { return bytes.begin(); }
    constexpr auto end() const { return bytes.end(); }
    constexpr auto size() const { return bytes.size(); }
    constexpr auto operator[](const size_t idx) const { return bytes[idx]; }
};

struct MsPrefixIndex {
    explicit constexpr MsPrefixIndex(const uint64_t val) : value(val) {}
    uint64_t value;

    constexpr auto operator<=>(const MsPrefixIndex&) const = default;
};

// ============================================================================
// ZERO-COPY SPAN SLICERS
// ============================================================================

template<size_t HashBytesCount>
MsPrefixBytes ms_prefix_bytes(const HashBytes<HashBytesCount>& hash, const uint8_t prefix_bits_count) {
    const auto prefix_bytes_count = (prefix_bits_count + 7) / 8;
    return PrefixBytes(
        PrefixBytes(hash.bytes).bytes.subspan(HashBytesCount - prefix_bytes_count, prefix_bytes_count));
}

template<size_t HashBytesCount>
LsSuffixBytes ls_suffix_bytes(const HashBytes<HashBytesCount>& hash, const uint8_t prefix_bits_count) {
    const auto suffix_bits_count = HashBytesCount * 8 - prefix_bits_count;
    const auto suffix_bytes_count = (suffix_bits_count + 7) / 8;
    return SuffixBytes(
        SuffixBytes(hash.bytes).bytes.subspan(0, suffix_bytes_count));
}

// ============================================================================
// MASKED BYTE EXTRACTORS
// ============================================================================

template<size_t HashBytesCount>
MsPrefixBytesMasked ms_prefix_bytes_masked(const HashBytes<HashBytesCount>& hash, const uint8_t prefix_bits_count) {
    const auto prefix_bytes_count = (prefix_bits_count + 7) / 8;
    auto span = prefix_bytes(hash, prefix_bits_count);

    MsPrefixBytesMasked vec(span.begin(), span.end());
    const auto num_zero_bits = prefix_bytes_count * 8 - prefix_bits_count;
    if (num_zero_bits > 0) {
        // Zero out the lowest unused bits in the least significant byte of the prefix
        const uint8_t mask = static_cast<uint8_t>(0xFFu << num_zero_bits);
        vec.bytes[0] &= mask;
    }
    return vec;
}

template<size_t HashBytesCount>
LsSuffixBytesMasked ls_suffix_bytes_masked(const HashBytes<HashBytesCount>& hash, const uint8_t prefix_bits_count) {
    auto span = suffix_bytes(hash, prefix_bits_count);
    LsSuffixBytesMasked vec(span.begin(), span.end());

    const auto suffix_bits_count = HashBytesCount * 8 - prefix_bits_count;
    const auto suffix_bytes_count = vec.size();
    const auto num_zero_bits = suffix_bytes_count * 8 - suffix_bits_count;
    if (num_zero_bits > 0) {
        // Zero out the highest unused bits in the most significant byte of the suffix
        const uint8_t mask = static_cast<uint8_t>(0xFFu >> num_zero_bits);
        vec.bytes[suffix_bytes_count - 1] &= mask;
    }
    return vec;
}

// ============================================================================
// FAST O(1) ZERO-ALLOCATION PREFIX INDEX COMPUTATION
// ============================================================================
static_assert(std::endian::native == std::endian::little, "ms_prefix_index() requires a Little-Endian architecture");

template<size_t HashBytesCount>
MsPrefixIndex ms_prefix_index(const HashBytes<HashBytesCount>& hash, const uint8_t prefix_bits_count) {
    assert(prefix_bits_count <= 64 && "Prefix index cannot exceed 64 bits");
    if (prefix_bits_count == 0) return MsPrefixIndex(0);

    // Read up to 8 bytes from the tail (MSB side) of the Little-Endian hash array
    uint64_t msb_window = 0;
    const size_t bytes_to_copy = std::min<size_t>(HashBytesCount, 8);

    std::memcpy(&msb_window, hash.bytes + (HashBytesCount - bytes_to_copy), bytes_to_copy);

    // Shift window right to align the n-bit prefix to LSB of uint64_t
    const size_t unused_bits_in_window = (bytes_to_copy * 8) - prefix_bits_count;
    uint64_t result = msb_window >> unused_bits_in_window;

    return MsPrefixIndex(result);
}

constexpr MsPrefixIndex ms_prefix_index_max(const uint8_t prefix_bits_count)
{
    assert(prefix_bits_count <= 64 && "Prefix index cannot exceed 64 bits");
    if (prefix_bits_count == 0) return MsPrefixIndex(0);

    const uint64_t max_bits_mask = (prefix_bits_count == 64)
        ? ~0ULL
        : ((1ULL << prefix_bits_count) - 1);
    return MsPrefixIndex(max_bits_mask);
}

// Make a path such as MsPrefix/12/34/56_LsSuffixEntries.bin for an MsPrefixIndex such as 0x123456
[[nodiscard]] inline std::filesystem::path make_ms_prefix_path(
const std::filesystem::path& base_dir,
const uint8_t prefix_bits_count,
const MsPrefixIndex ms_prefix_index)
{
    if (prefix_bits_count > 64) {
        throw std::invalid_argument("prefix_bits_count cannot exceed 64");
    }

    // Case n=0: Uncompressed complete hashes stored at root of base_dir folder
    if (prefix_bits_count == 0) {
        return base_dir / "HashEntries.bin";
    }

    std::filesystem::path result = base_dir / "MsPrefix";

    // Determine total 8-bit levels needed to hold n bits (1..8 bits -> 1 level, 9..16 bits -> 2 levels)
    const size_t num_levels = (prefix_bits_count + 7) / 8;

    // Mask input to valid prefix bits for safety
    const uint64_t max_bits_mask = (prefix_bits_count == 64)
        ? ~0ULL
        : ((1ULL << prefix_bits_count) - 1);
    const uint64_t masked_index = ms_prefix_index.value & max_bits_mask;

    // Extract 8-bit chunks starting from MSB level down to LSB level
    const size_t total_allocated_bits = num_levels * 8;

    for (size_t level = 0; level < num_levels; ++level) {
        const size_t shift_amount = total_allocated_bits - ((level + 1) * 8);
        const uint8_t byte_val = static_cast<uint8_t>((masked_index >> shift_amount) & 0xFF);

        // Format as 2-digit uppercase Hex (e.g., 0A, EF, 80)
        std::string hex_str = std::format("{:02X}", byte_val);

        if (level == num_levels - 1) {
            // Leaf level file name
            result /= (hex_str + "_LsSuffixEntries.bin");
        } else {
            // Intermediate folder levels
            result /= hex_str;
        }
    }

    return result;
}

} // namespace wedding_cake