#pragma once
#include <optional>

#include "types.hpp"
#include "mishap_assert.hpp"

namespace wedding_cake {

template<typename T>
concept RangeBounded = requires(const T& obj) {
    { obj.first_offset() } -> std::same_as<GlobalPi>;
    { obj.next_offset() } -> std::same_as<GlobalPi>;
};

template<typename T, size_t HashBytesCount>
concept ForwardLookupable = RangeBounded<T> && requires(const T& obj, GlobalPi global_pi) {
    { obj.forward_lookup(global_pi) } -> std::same_as<std::optional<HashBytes<HashBytesCount>>>;
};

constexpr bool contains_pi(const RangeBounded auto& container, GlobalPi global_pi) noexcept {
    return global_pi.to_int() >= container.first_offset().to_int() &&
           global_pi.to_int() < container.next_offset().to_int();
}

void assert_contains_pi(const RangeBounded auto& container, const GlobalPi global_pi) noexcept {
    const auto pi_int = global_pi.to_int();
    const auto first_int = container.first_offset().to_int();
    const auto next_int = container.next_offset().to_int();
    MISHAP_ASSERT(pi_int >= first_int, "pi is younger than container");
    MISHAP_ASSERT(pi_int < next_int, "pi is older than container");
    // The following code lets us debug into the sources of the first, next numbers after a MISHAP_ASSERT
    const auto first = container.first_offset();   // Redundant except after a MISHAP_ASSERT
    const auto next = container.next_offset();     // Redundant except after a MISHAP_ASSERT
}

/// Enforces that T can execute a ReverseLookup: HashBytes<N> -> GlobalPi
template<typename T, size_t HashBytesCount>
concept ReverseLookupable = requires(T& obj, const HashBytes<HashBytesCount>& hash) {
    { obj.reverse_lookup(hash) } -> std::same_as<GlobalPi>;
};

/// Composite concept requiring BOTH Forward and Reverse lookups
template<typename T, size_t HashBytesCount>
concept Lookupable = ForwardLookupable<T, HashBytesCount> &&
                     ReverseLookupable<T, HashBytesCount>;

}
