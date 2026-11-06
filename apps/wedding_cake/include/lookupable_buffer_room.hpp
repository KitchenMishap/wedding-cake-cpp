#pragma once

#include <cassert>
#include <optional>
#include <ranges>

#include "concepts_lookup.hpp"
#include "ingester_evolver_types.hpp"
#include "types.hpp"
#include "evolving_disk_state/buffer_room.hpp"

namespace wedding_cake {

/// Application-specific wrapper that adds Settler ForwardLookupable concept
/// compliance to the generic thread-safe library BufferRoom.
template <typename DonutType, std::size_t Capacity, size_t HashBytesCount>
class LookupableBufferRoom {
public:
    using GenericBuffer = evolving_disk_state::BufferRoom<DonutType, Capacity>;

    LookupableBufferRoom() = default;

    // Direct delegate to underlying library buffer initialization/feeding
    void InitializeAdd(DonutType donut) {
        buffer_.InitializeAdd(std::move(donut));
    }

    void Feed(DonutType donut) {
        buffer_.Feed(std::move(donut));
    }

    void Discard() {
        buffer_.Discard();
    }

    // Access underlying library buffer directly if needed by ingestion workers
    [[nodiscard]] GenericBuffer& raw_buffer() noexcept { return buffer_; }
    [[nodiscard]] const GenericBuffer& raw_buffer() const noexcept { return buffer_; }

    // -------------------------------------------------------------------------
    // 1. Settler Range Boundary Contract (First & Next Offset)
    // -------------------------------------------------------------------------
    [[nodiscard]] GlobalPi first_offset() const noexcept {
        GlobalPi result{GLOBAL_PI_NO_MATCH};
        // Inspect current contents safely under reader lock
        buffer_.with_current_contents([&result](std::span<const DonutType> donuts) {
            assert(!donuts.empty() && "Please don't ask an empty buffer room for first_offset");
            result = donuts.front().first_offset();
        });
        return result;
    }

    [[nodiscard]] GlobalPi next_offset() const noexcept {
        GlobalPi result{GLOBAL_PI_NO_MATCH};
        // Inspect current contents safely under reader lock
        buffer_.with_current_contents([&result](std::span<const DonutType> donuts) {
            assert (!donuts.empty() && "Please don't ask an empty buffer room for next_offset");
            result = donuts.back().next_offset();
        });
        return result;
    }

    // -------------------------------------------------------------------------
    // 2. Settler ForwardLookupable Contract
    // -------------------------------------------------------------------------
    /// Precondition (Enforced by Parent): contains_pi(*this, global_pi) == true
    std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi) const {
        std::optional<HashBytes<HashBytesCount>> result = std::nullopt;
        bool found = false;
        assert(contains_pi(*this, global_pi) && "Parent should have checked bounds");

        // Execute search thread-safely inside with_current_contents()
        buffer_.with_current_contents([global_pi, &result, &found](std::span<const DonutType> donuts) {
            // Linear search (Newest -> Oldest) across the tiny batch (capacity = 4)
            for (const auto& donut : donuts | std::views::reverse) {
                if (contains_pi(donut, global_pi)) {
                    result = donut.forward_lookup(global_pi);
                    found = true;
                    return; // Found target child, early exit!
                }
            }
        });
        assert(found && "Why wasn't global_pi represented by a child?");
        return result;
    }

    GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const {
        GlobalPi result = GLOBAL_PI_NO_MATCH;
        // Execute search thread-safely inside with_current_contents()
        buffer_.with_current_contents([hash, &result](std::span<const DonutType> donuts) {
            // Linear search across the tiny batch (capacity = 4)
            for (const auto& donut : donuts) {
                result = donut.reverse_lookup(hash);
                if (result != GLOBAL_PI_NO_MATCH) {
                    return; // Found a match, early exit!
                }
            }
        });
        return result;  // Whether or not GLOBAL_PI_NO_MATCH
    }

    bool empty() const {
        return buffer_.empty();
    }

private:
    GenericBuffer buffer_;
};

constexpr int fanout_capacity = 4;
constexpr int hash_bytes_count = 32;
static_assert(
    ForwardLookupable< LookupableBufferRoom<Ingester_ST01_FilledDonut<hash_bytes_count>,fanout_capacity,hash_bytes_count>,
        hash_bytes_count>, "LookupableBufferRoom must fulfill the ForwardLookupable concept");

}