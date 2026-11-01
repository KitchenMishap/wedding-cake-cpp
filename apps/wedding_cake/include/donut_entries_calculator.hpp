#pragma once
#include <cstdint>

#include "types.hpp"

namespace wedding_cake {

// For a particular donut in a particular tier,
// Calculates entries for the various files that comprise the donut.
// These are:
// HashesOrder.bin
// MsPrefix/12/34_LsSuffixEntries.bin
template<size_t HashBytesCount>
class DonutEntriesCalculator {
public:
    DonutEntriesCalculator( const size_t prefix_bits_count,
                            const PiOffset first_global_pi, const size_t entries_per_donut)
    : prefix_bits_count_(prefix_bits_count)
    , suffix_bits_count_(HashBytesCount * 8 - prefix_bits_count)
    , prefix_bytes_count_((prefix_bits_count + 7)/ 8)
    , suffix_bytes_count_((suffix_bits_count_ + 7)/ 8)
    , first_global_pi_(first_global_pi)
    , entries_per_donut_(entries_per_donut)
    {
    }

    // Note that HashesOrderFile.bin has an additional byte "bytes_per_suffix_index" at the start of the file,
    // this byte cannot be calculated until all hashes in the donut have been considered.
    std::vector<std::uint8_t> hashes_order_file_header(std::uint8_t bytes_per_suffix_index);
    std::vector<std::uint8_t> hashes_order_file_entry(HashBytes<HashBytesCount> hash, std::uint8_t bytes_per_suffix_index);

private:
    size_t prefix_bits_count_;
    size_t suffix_bits_count_;
    size_t prefix_bytes_count_;
    size_t suffix_bytes_count_;
    PiOffset first_global_pi_;
    size_t entries_per_donut_;
};

template<size_t HashBytesCount>
std::vector<std::uint8_t> DonutEntriesCalculator<HashBytesCount>::hashes_order_file_header(
        const std::uint8_t bytes_per_suffix_index) {
    std::vector<std::uint8_t> result;
    result.push_back(static_cast<std::uint8_t>(bytes_per_suffix_index));
    return result;
}

template<size_t HashBytesCount>
std::vector<std::uint8_t> DonutEntriesCalculator<HashBytesCount>::hashes_order_file_entry(
    HashBytes<HashBytesCount> hash, std::uint8_t bytes_per_suffix_index)
    {
        std::vector<std::uint8_t> result;
        result.insert(entry.end(), hash.begin(), hash.end());
        return result;
    }

}
