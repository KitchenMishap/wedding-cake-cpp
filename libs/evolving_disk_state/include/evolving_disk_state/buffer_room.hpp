#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <span>
#include <stdexcept>
#include <stop_token>

#include "concepts.hpp"

namespace evolving_disk_state {

template <typename T, std::size_t C>
class BufferRoom {
public:
    using value_type = T;
    static constexpr std::size_t capacity = C;
    using OnFullCallback = std::function<void()>;

    BufferRoom() = default;
    ~BufferRoom() = default;

    explicit BufferRoom(OnFullCallback on_full_cb)
        : on_full_cb_(std::move(on_full_cb)) {}

    void set_on_full_callback(OnFullCallback on_full_cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        on_full_cb_ = std::move(on_full_cb);
    }

    BufferRoom(const BufferRoom&) = delete;
    BufferRoom& operator=(const BufferRoom&) = delete;

    // -------------------------------------------------------------------------
    // Initialization (Single-threaded startup)
    // -------------------------------------------------------------------------
    void InitializeAdd(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.size() >= capacity) {
            throw std::invalid_argument("BufferRoom::InitializeAdd(): buffer room already at capacity");
        }
        items_.push_back(std::move(item));
    }

    // -------------------------------------------------------------------------
    // Upstream Producer: Feed
    // -------------------------------------------------------------------------
    void Feed(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait until there is space AND no readers are inspecting the buffer
        cv_producer_.wait(lock, [this]() {
            return items_.size() < C && active_readers_ == 0;
        });

        items_.push_back(std::move(item));

        if (items_.size() == C) {
            if (on_full_cb_) {
                on_full_cb_();
            }
            cv_consumer_.notify_all();
        }
    }

    // -------------------------------------------------------------------------
    // Interface 1: Tier Ingestion Worker (Blocking Batch Rendezvous)
    // -------------------------------------------------------------------------
    template <typename Func>
    bool with_contents_when_filled(std::stop_token stop_token, Func&& worker_lambda) {
        std::unique_lock<std::mutex> lock(mutex_);
        const uint64_t entry_epoch = discard_epoch_;

        auto is_ready = [this, entry_epoch]() {
            return items_.size() == C || discard_epoch_ != entry_epoch;
        };

        // Fast-path if stop_token isn't associated with a stop_source (e.g. unit tests)
        if (stop_token.stop_possible()) {
            cv_consumer_.wait(lock, stop_token, is_ready);
        } else {
            cv_consumer_.wait(lock, is_ready);
        }

        // Exit cleanly if stopped OR if a Discard() happened while we were waiting
        if (stop_token.stop_requested() || discard_epoch_ != entry_epoch || items_.size() < C) {
            return false;
        }

        // Register active reader (prevents Feed/Discard from mutating items_ while unlocked)
        ReaderScope reader(*this);
        std::span<const T, C> fixed_view(items_.data(), C);

        lock.unlock();
        worker_lambda(fixed_view);
        return true;
    }

    // -------------------------------------------------------------------------
    // Interface 2: Hat Reader / Search Thread (Non-Blocking Point Lookup)
    // -------------------------------------------------------------------------
    template <typename Func>
    bool with_current_contents(Func&& reader_lambda) const {
        std::unique_lock<std::mutex> lock(mutex_);

        // Register active reader so multiple readers run concurrently without holding mutex_
        ReaderScope reader(*this);
        std::span<const T> current_view(items_.data(), items_.size());

        lock.unlock();
        reader_lambda(current_view);
        return true;
    }

    // -------------------------------------------------------------------------
    // Lifecycle Transition: Discard
    // -------------------------------------------------------------------------
    void Discard() {
        std::unique_lock<std::mutex> lock(mutex_);

        // Advance epoch so any sleeping consumers immediately know a discard occurred
        ++discard_epoch_;
        cv_consumer_.notify_all();

        // Wait until all active readers (workers & hat readers) finish reading
        cv_discard_.wait(lock, [this]() {
            return active_readers_ == 0;
        });

        // Move items out locally so disk I/O (purge_from_disk) runs outside the mutex
        std::vector<T> to_purge = std::move(items_);
        items_.clear();

        // Wake up producers blocked in Feed()
        cv_producer_.notify_all();

        lock.unlock();
        for (const auto& item : to_purge) {
            item.purge_from_disk();
        }
    }

private:
    struct ReaderScope {
        const BufferRoom& room;

        explicit ReaderScope(const BufferRoom& r) : room(r) {
            ++room.active_readers_;
        }

        ~ReaderScope() {
            std::lock_guard<std::mutex> lock(room.mutex_);
            if (--room.active_readers_ == 0) {
                room.cv_discard_.notify_all();
                room.cv_producer_.notify_all();
            }
        }
    };

    OnFullCallback on_full_cb_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_producer_;
    std::condition_variable_any cv_consumer_;
    mutable std::condition_variable cv_discard_;

    std::vector<T> items_;
    mutable std::size_t active_readers_{0};
    uint64_t discard_epoch_{0};
};

}