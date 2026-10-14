#pragma once
#include <utility>
#include <concepts>
#include <optional>

template <typename TDataPair, typename TTier0>
class ExternalFeeder {
public:
    void connect_tier0(TTier0* tier0) { tier0_ = tier0; }

    void insert_data(TDataPair data) {
        slot_ = std::move(data);
        if (tier0_) {
            tier0_->notify_input_buffer_full_and_ready(*this);
        }
        slot_.reset();
    }

    void with_full_contents_as_consumer_maybe_wipe(
        std::invocable<const TDataPair&> auto&& consumer_lambda
    ) {
        if (!slot_.has_value()) return;
        if (consumer_lambda(*slot_)) {
            slot_.reset();
        }
    }

private:
    std::optional<TDataPair> slot_;
    TTier0* tier0_ = nullptr;
};