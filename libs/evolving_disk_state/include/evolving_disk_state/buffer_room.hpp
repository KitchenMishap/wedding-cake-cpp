#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <span>
#include <type_traits>

#include "concepts.hpp"

namespace evolving_disk_state {

template <typename T, std::size_t C>
class BufferRoom {
public:
    using value_type = T;
    static constexpr std::size_t capacity = C;

    BufferRoom() = default;
    ~BufferRoom() = default; // Preserves disk folders on app exit!

    // Non-copyable, non-movable
    BufferRoom(const BufferRoom&) = delete;
    BufferRoom& operator=(const BufferRoom&) = delete;

    // -------------------------------------------------------------------------
    // Upstream Producer: Feed
    // -------------------------------------------------------------------------
    void Feed(T item) {
        std::unique_lock<std::mutex> state_lock(state_mutex_);

        // Block if full or if a discard operation is purging the buffer
        cv_producer_.wait(state_lock, [this]() {
            return items_.size() < C && !is_discarding_;
        });

        // Acquire EXCLUSIVE lock on data array before push_back
        {
            std::unique_lock<std::shared_mutex> data_lock(data_mutex_);
            items_.push_back(std::move(item));
        }

        // Notify waiting tier workers if capacity C is reached
        if (items_.size() == C) {
            cv_consumer_.notify_all();
        }
    }

    // -------------------------------------------------------------------------
    // Interface 1: Tier Ingestion Worker (Blocking Batch Rendezvous)
    // -------------------------------------------------------------------------
    template <typename Func>
    bool with_contents_when_filled(Func&& worker_lambda) {
        std::unique_lock<std::mutex> state_lock(state_mutex_);

        // BLOCK until buffer holds C items OR a discard is explicitly initiated
        cv_consumer_.wait(state_lock, [this]() {
            return items_.size() == C || is_discarding_;
        });

        if (is_discarding_) {
            return false; // Discard/shutdown requested!
        }

        // RAII Reader Registration (increments active_readers_ under state_lock)
        ReaderScope reader(*this);
        state_lock.unlock(); // Drop state lock during user lambda execution!

        // Acquire SHARED lock on data array for duration of worker_lambda
        {
            std::shared_lock<std::shared_mutex> data_lock(data_mutex_);
            std::span<const T, C> fixed_view(items_.data(), C);
            worker_lambda(fixed_view);
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Interface 2: Hat Reader / Search Thread (Non-Blocking Point Lookup)
    // -------------------------------------------------------------------------
    template <typename Func>
    bool with_current_contents(Func&& reader_lambda) {
        std::unique_lock<std::mutex> state_lock(state_mutex_);

        if (is_discarding_) {
            return false;
        }

        // RAII Reader Registration
        ReaderScope reader(*this);
        state_lock.unlock(); // Drop state lock during user lambda execution!

        {
            std::shared_lock<std::shared_mutex> data_lock(data_mutex_);
            std::span<const T> current_view(items_.data(), items_.size());
            reader_lambda(current_view);
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Lifecycle Transition: Discard
    // -------------------------------------------------------------------------
    void Discard() {
        std::unique_lock<std::mutex> state_lock(state_mutex_);

        is_discarding_ = true;

        // Wake up any waiting tier workers so they can unblock cleanly
        cv_consumer_.notify_all();

        // Wait for all active readers (both workers and hat readers) to exit
        cv_discard_.wait(state_lock, [this]() {
            return active_readers_ == 0;
        });

        // Release state_lock BEFORE acquiring exclusive data_lock to prevent mutex ordering inversions!
        state_lock.unlock();

        // Acquire EXCLUSIVE lock on data before clearing vector
        {
            std::unique_lock<std::shared_mutex> data_lock(data_mutex_);

            for (auto& item : items_) {
                if constexpr (requires(const T& obj) { obj.purge_from_disk(); }) {
                    item.purge_from_disk();
                }
            }
            items_.clear();
        }

        // Re-acquire state_lock to reset is_discarding_ and notify producers
        state_lock.lock();
        is_discarding_ = false;

        // Wake up producers waiting in Feed()
        cv_producer_.notify_all();
    }

private:
    // RAII Guard to ensure active_readers_ count is decremented thread-safely
    struct ReaderScope {
        BufferRoom& room;

        explicit ReaderScope(BufferRoom& r) : room(r) {
            // Must be called while holding room.state_mutex_!
            room.active_readers_++;
        }

        ~ReaderScope() {
            std::unique_lock<std::mutex> lock(room.state_mutex_);
            room.active_readers_--;
            if (room.active_readers_ == 0 && room.is_discarding_) {
                room.cv_discard_.notify_all();
            }
        }
    };

    std::mutex state_mutex_;             // Protects counters, cv predicates, and discard flags
    std::shared_mutex data_mutex_;       // Protects items_ vector layout during Feed/Read
    std::condition_variable cv_producer_;
    std::condition_variable cv_consumer_;
    std::condition_variable cv_discard_;

    std::vector<T> items_;
    std::size_t active_readers_{0};
    bool is_discarding_{false};
};

// 1. Declare a mock or actual move-only item type used in your tier
struct MockSealedItem {
    MockSealedItem(MockSealedItem&&) noexcept = default;
    MockSealedItem& operator=(MockSealedItem&&) noexcept = default;

    MockSealedItem(const MockSealedItem&) = delete;
    MockSealedItem& operator=(const MockSealedItem&) = delete;
};

// 2. Define the concrete Buffer type
using TestBuffer = BufferRoom<MockSealedItem, 4>;

// 3. Static Assert
static_assert(concepts::BufferInput<TestBuffer, MockSealedItem>,
    "BufferRoom must satisfy the BufferInput concept");
static_assert(concepts::BufferOutput<TestBuffer, MockSealedItem>,
    "BufferRoom must satisfy BufferOutput concept");
static_assert(concepts::BufferReadable<TestBuffer, MockSealedItem>,
    "BufferRoom must satisfy BufferReadable concept");

} // namespace evolving_disk_state