#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>

namespace wedding_cake {

/**
 * @brief Constructs a hierarchical folder/file path based on tail_bits_count and tail_index.
 * 
 * - tail_bits_count == 0:
 *     "TailIndices/PrefixEntries"
 * - 1 <= tail_bits_count <= 8:
 *     "TailIndices/XX_PrefixEntries"
 * - 9 <= tail_bits_count <= 16:
 *     "TailIndices/XX/YY_PrefixEntries"
 * - N tail bits (up to 64):
 *     "TailIndices/XX/.../ZZ_PrefixEntries"
 * 
 * @param base_dir Root directory where TailIndices hierarchy will be created.
 * @param tail_bits_count Total tail bits determining depth (0 to 64).
 * @param tail_index Index value whose bits determine subfolder/file hex names.
 * @return std::filesystem::path Formatted full path.
 */
[[nodiscard]] inline std::filesystem::path make_prefix_entries_path(
    const std::filesystem::path& base_dir,
    uint8_t tail_bits_count,
    uint64_t tail_index) 
{
    if (tail_bits_count > 64) {
        throw std::invalid_argument("tail_bits_count cannot exceed 64");
    }

    std::filesystem::path result = base_dir / "TailIndices";

    // Case 0: Single file at base folder
    if (tail_bits_count == 0) {
        return result / "PrefixEntries";
    }

    // Determine total levels needed (each level handles 8 bits)
    // 1..8 bits -> 1 level, 9..16 bits -> 2 levels, etc.
    const size_t num_levels = (tail_bits_count + 7) / 8;

    // Mask tail_index to only valid bits for safety
    const uint64_t max_bits_mask = (tail_bits_count == 64) 
        ? ~0ULL 
        : ((1ULL << tail_bits_count) - 1);
    const uint64_t masked_index = tail_index & max_bits_mask;

    // Extract 8-bit chunks starting from MSB down to LSB
    const size_t total_allocated_bits = num_levels * 8;

    for (size_t level = 0; level < num_levels; ++level) {
        const size_t shift_amount = total_allocated_bits - ((level + 1) * 8);
        const uint8_t byte_val = static_cast<uint8_t>((masked_index >> shift_amount) & 0xFF);

        // Format as 2-digit uppercase Hex (e.g., 0A, FF, 00)
        std::string hex_str = std::format("{:02X}", byte_val);

        if (level == num_levels - 1) {
            // Final level is the leaf filename
            result /= (hex_str + "_PrefixEntries");
        } else {
            // Intermediate levels are directory names
            result /= hex_str;
        }
    }

    return result;
}

} // namespace wedding_cake