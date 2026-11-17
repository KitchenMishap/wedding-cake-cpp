#pragma once

#include <optional>

#include "types.hpp"
#include "tier_n_transition_proxy.hpp"

namespace wedding_cake {

/// Lightweight query dispatch delegate used by Lockchain during two-stage lookup
template<typename InputTierFolderType, size_t HashBytesCount>
class TierNTransitionLookup {
public:
    explicit TierNTransitionLookup(const TierNTransitionProxy<InputTierFolderType, HashBytesCount>* proxy);

    // --- Two-Stage Pinning & Locking Protocol ---

    /// Stage 1: Acquire Keep-Alive Pin (Prevents structural destruction during scan)
    void acquire_keep_alive_pin();
    void release_keep_alive_pin();

    /// Stage 2: Execute Shared Lock Scan across upper BufferRoom and lower Hatchling
    [[nodiscard]] std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi) const;
    [[nodiscard]] GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const;

    [[nodiscard]] GlobalPi first_offset() const;
    [[nodiscard]] GlobalPi next_offset() const;

private:
    const TierNTransitionProxy<InputTierFolderType, HashBytesCount>* proxy_;
    bool is_pinned_{false};
};

template<typename InputTierFolderType, size_t HashBytesCount>
TierNTransitionLookup<InputTierFolderType, HashBytesCount>::TierNTransitionLookup(
    const TierNTransitionProxy<InputTierFolderType, HashBytesCount>* proxy) : proxy_(proxy) {}

} // namespace wedding_cake