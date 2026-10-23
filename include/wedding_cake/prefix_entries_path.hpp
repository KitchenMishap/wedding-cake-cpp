#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>

namespace wedding_cake {

// =============================================================================
// HASH BIT-SPLITTING & DISPLAY CONVENTIONS
// =============================================================================
// - A hash is considered to be a very large N-bit number (e.g. N=256 for SHA-256).
// - A hash is represented in memory as Little-Endian (LSB first byte array).
// - A hash is displayed to the user (e.g. on block explorers) with MSB first.
// - In this codebase, a hash is split into an n-bit MSPrefix and an (N-n)-bit LSSuffix.
// - Note that n is not always a multiple of 8, nor even a multiple of 4.
// - Example: For n=10, a hash starting "EF9C..." in a block explorer splits into
//   10 bits (0x3BE), resulting in file path: "MSPrefix/EF/80_LSSuffixEntries".
// =============================================================================

/**
 * @brief Constructs a hierarchical folder/file path based on ms_prefix_bits_count and ms_prefix_index.
 *
 * - ms_prefix_bits_count == 0 ($T=0$):
 *     "HashEntries.bin"
 * - 1 <= ms_prefix_bits_count <= 8:
 *     "MSPrefix/XX_LSSuffixEntries.bin"
 * - 9 <= ms_prefix_bits_count <= 16:
 *     "MSPrefix/XX/YY_LSSuffixEntries.bin"
 *
 * @param base_dir Root directory where hierarchy (or HashesEntries.bin) will be created.
 * @param ms_prefix_bits_count Total routing prefix bits determining depth (0 to 64).
 * @param ms_prefix_index Integer value of the extracted n-bit prefix.
 * @return std::filesystem::path Formatted full path.
 */

[[nodiscard]] inline std::filesystem::path make_ms_prefix_path(
    const std::filesystem::path& base_dir,
    const uint8_t ms_prefix_bits_count,
    const uint64_t ms_prefix_index)
{
    if (ms_prefix_bits_count > 64) {
        throw std::invalid_argument("ms_prefix_bits_count cannot exceed 64");
    }

    // Case n=0: Uncompressed complete hashes stored at root of base_dir folder
    if (ms_prefix_bits_count == 0) {
        return base_dir / "HashEntries.bin";
    }

    std::filesystem::path result = base_dir / "MSPrefix";

    // Determine total 8-bit levels needed to hold n bits (1..8 bits -> 1 level, 9..16 bits -> 2 levels)
    const size_t num_levels = (ms_prefix_bits_count + 7) / 8;

    // Mask input to valid prefix bits for safety
    const uint64_t max_bits_mask = (ms_prefix_bits_count == 64)
        ? ~0ULL
        : ((1ULL << ms_prefix_bits_count) - 1);
    const uint64_t masked_index = ms_prefix_index & max_bits_mask;

    // Extract 8-bit chunks starting from MSB level down to LSB level
    const size_t total_allocated_bits = num_levels * 8;

    for (size_t level = 0; level < num_levels; ++level) {
        const size_t shift_amount = total_allocated_bits - ((level + 1) * 8);
        const uint8_t byte_val = static_cast<uint8_t>((masked_index >> shift_amount) & 0xFF);

        // Format as 2-digit uppercase Hex (e.g., 0A, EF, 80)
        std::string hex_str = std::format("{:02X}", byte_val);

        if (level == num_levels - 1) {
            // Leaf level file name
            result /= (hex_str + "_LSSuffixEntries.bin");
        } else {
            // Intermediate folder levels
            result /= hex_str;
        }
    }

    return result;
}

} // namespace wedding_cake
