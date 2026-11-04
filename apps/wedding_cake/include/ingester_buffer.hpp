#pragma once

#include "types.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <utility>

#include "concepts_lookup.hpp"

namespace wedding_cake {

template<size_t HashBytesCount>
class IngesterBuffer {
public:
    using HashItem = std::optional<HashBytes<HashBytesCount>>;
    using BatchType = std::vector<HashItem>;

    /// @param initial_downstream_remaining_capacity Query from downstream Remainder at startup (e.g., remainder.remaining_capacity())
    /// @param initial_first_global_pi Query from downstream Remainder at startup (e.g., remainder.next_offset())
    explicit IngesterBuffer(size_t initial_downstream_remaining_capacity, PiOffset initial_first_global_pi)
        : downstream_remaining_capacity_(initial_downstream_remaining_capacity)
        , first_global_pi_(initial_first_global_pi) {
        assert(downstream_remaining_capacity_ > 0
            && "IngestorBuffer: We should always find some downstream capacity at startup");
    }

    IngesterBuffer(const IngesterBuffer&) = delete;
    IngesterBuffer& operator=(const IngesterBuffer&) = delete;

    // =========================================================================
    // PASSIVE INPUT INTERFACE (For Client Feed Thread)
    // =========================================================================

    /// Feeds a block of hashes into the RAM buffer.
    /// BLOCKS when buffer_.size() >= downstream_remaining_capacity_ (Backpressure).
    void feed_block_of_hashes(BatchType block) {
        if (block.empty()) return;

        std::unique_lock<std::shared_mutex> lock(mutex_);

        // Backpressure: Block while queued RAM items meet or exceed current target capacity
        can_feed_cv_.wait(lock, [this]() {
            return buffer_.size() < downstream_remaining_capacity_ || stopping_;
        });

        if (stopping_) return;

        // Insert for reverse lookup
        auto local_pi_int = buffer_.size();
        for (const auto& optional_hash : block) {
            if (optional_hash.has_value()) {
                reverse_lookup_map_[optional_hash.value()] = LocalPi(local_pi_int);
            }
            ++local_pi_int;
        }

        // Insert (move) for forward lookup & downstream consumption
        buffer_.insert(
            buffer_.end(),
            std::make_move_iterator(block.begin()),
            std::make_move_iterator(block.end())
        );

        has_items_cv_.notify_one();
    }

    /// Requests an immediate flush of queued hashes (e.g., end-of-stream signal).
    /// BLOCKS until the worker processes the batch and completes physical flush.
    void flush() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (buffer_.empty()) return;

        please_flush_ = true;
        has_items_cv_.notify_one();

        flush_done_cv_.wait(lock, [this]() {
            return !please_flush_ || stopping_;
        });
    }

    /// Wakes all waiting threads for clean pipeline shutdown.
    void stop() {
        {
            std::lock_guard<std::shared_mutex> lock(mutex_);
            stopping_ = true;
            please_flush_ = false;
        }
        can_feed_cv_.notify_all();
        has_items_cv_.notify_all();
        flush_done_cv_.notify_all();
    }

    // =========================================================================
    // PASSIVE OUTPUT INTERFACE (For IngesterTransitionWorker Thread)
    // =========================================================================

    /// Blocks until buffer holds >= downstream_remaining_capacity_ hashes OR flush() is requested.
    /// Executes `func` INSIDE the lock to guarantee atomic backpressure updates.
    ///
    /// @tparam Func Signature: size_t(BatchType& batch, bool is_flushing)
    ///         MUST RETURN the new remaining capacity of the downstream target!
    template<typename Func>
    requires std::invocable<Func, BatchType&, bool> &&
             std::same_as<std::invoke_result_t<Func, BatchType&, bool>, size_t>
    void with_at_least_n_hashes(Func&& func) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        // 1. Wait until buffer reaches current target capacity OR a flush is requested
        has_items_cv_.wait(lock, [this]() {
            return buffer_.size() >= downstream_remaining_capacity_ || please_flush_ || stopping_;
        });

        if (stopping_ && buffer_.empty()) return;   // ToDo: Do we really want to wait for buffer empty when stopping?

        bool is_flush_active = please_flush_;

        // 2. Execute consumer lambda UNDER THE LOCK
        // Passes buffer_ reference directly; func() consumes items from buffer_
        const size_t buffer_size_sent = buffer_.size();
        const size_t new_remaining_capacity = func(buffer_, is_flush_active);
        assert(buffer_.empty()
            && "IngestorBuffer: Lambda should always empty the buffer");
        assert(new_remaining_capacity > 0
            && "IngestorBuffer: We should always find some remaining downstream capacity after lambda");

        // 3. Atomically update target capacity from returned remaining space
        downstream_remaining_capacity_ = new_remaining_capacity;

        // 4. Update our first_global_pi_ which has now changed
        first_global_pi_ = PiOffset(first_global_pi_.to_int() + buffer_size_sent);

        // 5. Empty the reverse lookup map too
        reverse_lookup_map_.clear();

        if (is_flush_active) {
            please_flush_ = false;
            flush_done_cv_.notify_all();
        }

        // 6. Notify any producer threads waiting on backpressure
        can_feed_cv_.notify_all();
    }

    [[nodiscard]] size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return buffer_.size();
    }

    [[nodiscard]] size_t downstream_remaining_capacity() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return downstream_remaining_capacity_;
    }

    std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi, bool& found) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (global_pi.to_int() < first_global_pi_.to_int()) {
            // Need to look further downstream (older hashes)
            found = false;
            return std::nullopt;
        }
        if (global_pi.to_int() >= first_global_pi_.to_int() + buffer_.size()) {
            // Specified global_pi hasn't even arrived yet
            found = false;
            return std::nullopt;
        }
        auto local_pi = global_pi - first_global_pi_;
        found = true;
        return buffer_[local_pi.to_int()];
    }

    GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = reverse_lookup_map_.find(hash);
        if (it != reverse_lookup_map_.end()) {
            return it->second + first_global_pi_;
        }
        return GLOBAL_PI_NO_MATCH;
    }

private:
    size_t downstream_remaining_capacity_;
    BatchType buffer_;
    std::map<HashBytes<HashBytesCount>, LocalPi> reverse_lookup_map_;
    PiOffset first_global_pi_;

    mutable std::shared_mutex mutex_;
    std::condition_variable_any can_feed_cv_;
    std::condition_variable_any has_items_cv_;
    std::condition_variable_any flush_done_cv_;

    bool please_flush_{false};
    bool stopping_{false};
};

static_assert(Lookupable<IngesterBuffer<32>, 32>,
              "IngesterBuffer<32> must fulfill the Lookupable concept!");

} // namespace wedding_cake