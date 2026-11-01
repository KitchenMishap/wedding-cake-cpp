#pragma once
#include <bit>
#include <cstddef>

// How many hashes can a tier 0 donut hold?
constexpr std::size_t ingester_donut_capacity = 65535;
// How many tier N donuts make a tier N+1 donut?
constexpr std::size_t tier_fanout_bit_count = 2;
constexpr std::size_t tier_fanout = 1 << tier_fanout_bit_count;

// How many bytes needed to represent 0..n?
constexpr std::size_t bytes_for(auto n) noexcept {
    if (n == 0) return 0; // or return 1 depending on whether 0 needs 1 byte
    int bits = std::bit_width(static_cast<unsigned long long>(n));
    return (bits + 7) / 8;
}

// How many bytes to represent a LocalPi in tier n?
constexpr std::size_t pi_bytes_count_for_tier(auto n) noexcept {
    size_t total_fanout = 1;
    for (size_t i = 0; i < n; ++i) {
        total_fanout *= tier_fanout;
    }
    return bytes_for(total_fanout * ingester_donut_capacity);
}

// How many bits in the hash ms_prefix used for tier n?
constexpr std::size_t ms_prefix_bits_for_tier(auto n) noexcept {
    return n * tier_fanout;
}
