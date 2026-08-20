// Purpose: Synchronous feeder simulating an upstream Tier -1 interface for Tier 0 ingestion.
// Purpose: Wraps incoming external data pushes in the universal IHasOutputBuffer interface.
// Purpose: Enables zero-copy, stack-based processing so Tier 0 doesn't know it is "special".

#pragma once
#include <utility>
#include <concepts>
#include <optional>

template <typename TDataPair>
class ExternalFeeder {
public:
    template <typename TTier0>
    void connect_tier0(TTier0* tier0) { tier0_ = tier0; }

    void insert_data(TDataPair data) {
        slot_ = std::move(data);
        if (tier0_) {
            tier0_->notify_input_buffer_full_and_ready(*this);
        }
        slot_.reset();
    }

    void with_full_contents_as_consumer_maybe_wipe(
        const std::invocable<const TDataPair&>& auto& consumer_lambda
    ) {
        if (!slot_.has_value()) return;
        if (consumer_lambda(*slot_)) {
            slot_.reset();
        }
    }

private:
    std::optional<TDataPair> slot_;
    void* tier0_ = nullptr;
};