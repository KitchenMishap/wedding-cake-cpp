#pragma once

#include <shared_mutex>
#include <mutex>
#include <optional>
#include <memory>

#include "types.hpp"
#include "tier_n_folder.hpp"

namespace wedding_cake {

/// Thread-safe Synchronization Facade.
/// Straddles Tier N's BufferRoom (upper) and Tier N+1's Hatchling (lower).
template<size_t HashBytesCount>
class TierNTransitionProxy {
public:
    explicit TierNTransitionProxy(TierNFolder<HashBytesCount>& upper_tier,
        TierNFolder<HashBytesCount>& lower_tier);

    TierNTransitionProxy(const TierNTransitionProxy&) = delete;
    TierNTransitionProxy& operator=(const TierNTransitionProxy&) = delete;

    // --- Client Thread (A): Lookups (Shared Lock) ---
    [[nodiscard]] std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi) const;
    [[nodiscard]] GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const;

    // --- Worker Thread (B): Baking Orchestration (Exclusive / Transition Holds) ---
    void begin_baking_transition(TierNFolder<HashBytesCount>& downstream_tier);
    void complete_baking_transition();

    // Returns true if buffer room contents sent to lambda
    // Forwards the caller's lambda directly to the upper tier's BufferRoom
    template <typename Func>
    bool upper_tier_buffer_room_with_contents_when_filled(std::stop_token stop_token, Func&& worker_lambda) {
        return upper_tier_.buffer_room().raw_buffer().with_contents_when_filled(
            stop_token, std::forward<Func>(worker_lambda)
        );
    }

    // Returns true if hatchling contents sent to lambda
    // Forwards the caller's lambda directly to the lower tier
    template <typename Func>
    bool with_lower_tier_hatchling_for_write(Func&& worker_lambda) {
        return lower_tier_->with_hatchling_for_write(std::forward<Func>(worker_lambda));
    }

    // The caller has gathered data from the buffer room contents, and it is no longer required
    void upper_tier_buffer_room_discard();

    // --- Pin & Lock State Query ---
    [[nodiscard]] bool is_baking_in_progress() const noexcept { return baking_in_progress_; }

private:
    TierNFolder<HashBytesCount>& upper_tier_;
    TierNFolder<HashBytesCount>* lower_tier_{nullptr};

    mutable std::shared_mutex proxy_mutex_;
    bool baking_in_progress_{false};
};

// The caller has gathered data from the buffer room contents, and it is no longer required
template<size_t HashBytesCount>
void TierNTransitionProxy<HashBytesCount>::upper_tier_buffer_room_discard() {
    upper_tier_.buffer_room().Discard();
}

} // namespace wedding_cake