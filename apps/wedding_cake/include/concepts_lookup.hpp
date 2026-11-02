#pragma once
#include <optional>

#include "types.hpp"

namespace wedding_cake {

/// Enforces that T can execute a ForwardLookup: GlobalPi -> std::optional<HashBytes<N>>
template<typename T, size_t HashBytesCount>
concept ForwardLookupable = requires(T& obj, GlobalPi global_pi, bool& found) {
    { obj.forward_lookup(global_pi, found) } -> std::same_as<std::optional<HashBytes<HashBytesCount>>>;
};

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
