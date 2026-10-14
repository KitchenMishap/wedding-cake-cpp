// Purpose: Core concurrency bridge ("Baker and Reader Hats" model) between pipeline stages.
// Purpose: Provides shared lock access for concurrent readers/bakers during work slices.
// Purpose: Enforces exclusive locks only when "turning out the lights" to wipe the buffer.

#pragma once
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <concepts>
#include <utility>
#include <cstddef>
#include <chrono>

using namespace std::chrono_literals;

template <typename TElement>
class OutputBuffer {
public:
    explicit OutputBuffer(std::size_t capacity) : capacity_(capacity) {
        contents_.reserve(capacity_);
    }

    void push_back_as_provider(TElement item) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cv_provider_.wait(lock, [this] { return contents_.size() < capacity_; });
        contents_.push_back(std::move(item));
        cv_reader_consumer_.notify_all();
    }

    // Modern C++20 constrained auto parameter:
    void with_contents_as_reader(
        std::invocable<const std::vector<TElement>&> auto&& visitor
    ) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        visitor(contents_);
    }

    // Waits up to 10ms for a full batch. Returns true if batch was processed and wiped.
    bool with_full_contents_as_consumer_maybe_wipe(
        std::invocable<const std::vector<TElement>&> auto&& consumer_lambda
    ) {
        bool should_wipe = false;
        {
            std::shared_lock<std::shared_mutex> shared_lock(mutex_);

            // Timed wait so consumer threads can wake up during shutdown/settled states
            bool full = cv_reader_consumer_.wait_for(
                shared_lock,
                10ms,
                [this] { return contents_.size() >= capacity_; }
            );

            if (!full) {
                return false; // Timed out waiting for full capacity
            }

            should_wipe = consumer_lambda(contents_);
        }

        if (should_wipe) {
            std::unique_lock<std::shared_mutex> exclusive_lock(mutex_);
            if (contents_.size() >= capacity_) {
                contents_.clear();
                cv_provider_.notify_all();
                return true;
            }
        }
        return false;
    }

    // Check if buffer has zero items remaining
    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return contents_.empty();
    }

    // Non-blocking drain for workers: processes ONLY full batches
    bool try_process_full_batch(
        std::invocable<const std::vector<TElement>&> auto&& consumer_lambda
    ) {
        bool should_wipe = false;
        {
            std::shared_lock<std::shared_mutex> shared_lock(mutex_);
            if (contents_.size() < capacity_) {
                return false; // Not enough for a full, deterministic work slice
            }
            should_wipe = consumer_lambda(contents_);
        }

        if (should_wipe) {
            std::unique_lock<std::shared_mutex> exclusive_lock(mutex_);
            if (contents_.size() >= capacity_) {
                contents_.clear();
                cv_provider_.notify_all();
            }
        }
        return true;
    }

private:
    const std::size_t capacity_;
    std::vector<TElement> contents_;
    mutable std::shared_mutex mutex_;
    std::condition_variable_any cv_provider_;
    mutable std::condition_variable_any cv_reader_consumer_;
};