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

namespace wedding_cake {

template<size_t HashBytesCount>
class IngesterBuffer {
public:
    using HashItem = std::optional<HashBytes<HashBytesCount>>;
    using BatchType = std::vector<HashItem>;

    /// @param initial_downstream_remaining_capacity Query from downstream Remainder at startup (e.g., remainder.remaining_capacity())
    explicit IngesterBuffer(size_t initial_downstream_remaining_capacity)
        : downstream_remaining_capacity_(initial_downstream_remaining_capacity) {
        assert(downstream_remaining_capacity_ > 0
            && "IngestorBuffer: We should always find some downstream capacity at startup");
    }

    IngesterBuffer(const IngesterBuffer&) = delete;
    IngesterBuffer& operator=(const IngesterBuffer&) = delete;

    // =========================================================================
    // PASSIVE INPUT INTERFACE (Client Feed Thread)
    // =========================================================================

    /// Feeds a block of hashes into the RAM buffer.
    /// BLOCKS when buffer_.size() >= downstream_remaining_capacity_ (Backpressure).
    void feed_block_of_hashes(BatchType block) {
        if (block.empty()) return;

        std::unique_lock<std::mutex> lock(mutex_);

        // Backpressure: Block while queued RAM items meet or exceed current target capacity
        can_feed_cv_.wait(lock, [this]() {
            return buffer_.size() < downstream_remaining_capacity_ || stopping_;
        });

        if (stopping_) return;

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
        std::unique_lock<std::mutex> lock(mutex_);
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
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            please_flush_ = false;
        }
        can_feed_cv_.notify_all();
        has_items_cv_.notify_all();
        flush_done_cv_.notify_all();
    }

    // =========================================================================
    // PASSIVE OUTPUT INTERFACE (IngesterTransitionWorker Thread)
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
        std::unique_lock<std::mutex> lock(mutex_);

        // 1. Wait until buffer reaches current target capacity OR a flush is requested
        has_items_cv_.wait(lock, [this]() {
            return buffer_.size() >= downstream_remaining_capacity_ || please_flush_ || stopping_;
        });

        if (stopping_ && buffer_.empty()) return;

        bool is_flush_active = please_flush_;

        // 2. Execute consumer lambda UNDER THE LOCK
        // Passes buffer_ reference directly; func() consumes items from buffer_
        const size_t new_remaining_capacity = func(buffer_, is_flush_active);
        assert(new_remaining_capacity > 0
            && "IngestorBuffer: We should always find some remaining downstream capacity after lambda");

        // 3. Atomically update target capacity from returned remaining space
        downstream_remaining_capacity_ = new_remaining_capacity;

        if (is_flush_active) {
            please_flush_ = false;
            flush_done_cv_.notify_all();
        }

        // 4. Notify any producer threads waiting on backpressure
        can_feed_cv_.notify_all();
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    [[nodiscard]] size_t downstream_remaining_capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return downstream_remaining_capacity_;
    }

private:
    size_t downstream_remaining_capacity_;
    BatchType buffer_;

    mutable std::mutex mutex_;
    std::condition_variable can_feed_cv_;
    std::condition_variable has_items_cv_;
    std::condition_variable flush_done_cv_;

    bool please_flush_{false};
    bool stopping_{false};
};

} // namespace wedding_cake