#pragma once

#include <stop_token>
#include <thread>
#include <condition_variable>
#include <mutex>

#include "bake.hpp"
#include "tier_n_folder.hpp"
#include "tier_n_transition_proxy.hpp"

namespace wedding_cake {

/// Thread Client (B): Background worker thread managing Stage 1 & 2 baking
template<size_t HashBytesCount>
class TierNTransitionWorker {
public:
    TierNTransitionWorker(TierNTransitionProxy<HashBytesCount>& proxy,
                          size_t source_prefix_bits_count,
                          size_t delta_prefix_bits_count,
                          size_t source_entries_per_donut);

    ~TierNTransitionWorker();

    TierNTransitionWorker(const TierNTransitionWorker&) = delete;
    TierNTransitionWorker& operator=(const TierNTransitionWorker&) = delete;

    /// Starts the stoppable background loop
    void start();

    /// Signals the background thread to stop cleanly
    void request_stop();

private:
    TierNTransitionProxy<HashBytesCount>& proxy_;

    size_t source_prefix_bits_count_;
    size_t delta_prefix_bits_count_;
    size_t source_entries_per_donut_;

    std::jthread worker_thread_;
    std::mutex cv_mutex_;
    std::condition_variable_any cv_;

    /// Main stoppable baking loop execution
    void worker_loop(std::stop_token stop_token);

    /// Executes Stage 1 & 2 Monolithic Forwarding baking sequence
    bool execute_bake_to_downstream(TierNFolder<HashBytesCount>& downstream_tier,
                                    std::stop_token stop_token);
};

template<size_t HashBytesCount>
TierNTransitionWorker<HashBytesCount>::TierNTransitionWorker(
        TierNTransitionProxy<HashBytesCount>& proxy,
        const size_t source_prefix_bits_count,
        const size_t delta_prefix_bits_count,
        const size_t source_entries_per_donut)
    : proxy_(proxy)
    , source_prefix_bits_count_(source_prefix_bits_count)
    , delta_prefix_bits_count_(delta_prefix_bits_count)
    , source_entries_per_donut_(source_entries_per_donut) {}

template<size_t HashBytesCount>
void TierNTransitionWorker<HashBytesCount>::start() {
    worker_thread_ = std::jthread([this](std::stop_token stop_token) {
        this->worker_loop(std::move(stop_token));
    });
}

template<size_t HashBytesCount>
void TierNTransitionWorker<HashBytesCount>::request_stop() {
    worker_thread_.request_stop();
}

template<size_t HashBytesCount>
TierNTransitionWorker<HashBytesCount>::~TierNTransitionWorker() {
    if (worker_thread_.joinable()) {
        worker_thread_.request_stop(); // Wakes cv_consumer_.wait(..., stop_token, ...) immediately!
        worker_thread_.join();         // Set your debugger breakpoint here
    }
}

template <size_t HashBytesCount>
void TierNTransitionWorker<HashBytesCount>::worker_loop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        bool ready_to_discard = false;

        // 1. Acquire read access to the filled upper-tier buffer room
        const bool upper_room_valid = proxy_.upper_tier_buffer_room_with_contents_when_filled(
            stop_token,
            [&](auto items) -> void {

                // Extract source folders from buffer items
                std::vector<std::filesystem::path> source_folders;
                source_folders.reserve(items.size());
                for (const auto& item : items) {
                    source_folders.push_back(item.prior_st00().st00_path());
                }

                bool evolutionary_destiny_reached = false;

                // 2. Acquire exclusive write access to the lower-tier hatchling
                const bool hatchling_valid = proxy_.with_lower_tier_hatchling_for_write(
                    [&](TierNEvolver<HashBytesCount>& destination) -> void {
                        // Here we are evolving the hatchling.
                        // There are several Typestates along this long process of evolution.
                        // We must examine the typestate (destination) to determine what stages still need to execute.

                        // Is its typestate an st00?
                        auto st00 = std::get_if<Tier_N_ST00_BakingDonut<HashBytesCount>>(&destination);
                        if (st00) {
                            // Execute the bake on disk (returns false if stop_token triggered)
                            // Throughout this long evolution stage, the destination is typestate Tier_N_ST00_BakingDonut
                            bool bake_succeeded = bake_donut<HashBytesCount>(
                                stop_token,
                                source_folders,
                                st00->st00_path(),
                                source_prefix_bits_count_,
                                delta_prefix_bits_count_,
                                st00->first_offset(),
                                source_entries_per_donut_);
                            if (!bake_succeeded) {
                                return; // Interrupted by stop_token
                            }
                            // Upgrade to Tier_N_ST01_BakedDonut, in memory and on disk
                            // 1. Move or extract what ST01 needs before mutating the variant
                            auto st00_obj = std::move(*st00);
                            // 2. Safe in-place variant conversion
                            destination = Tier_N_ST01_BakedDonut<HashBytesCount>(std::move(st00_obj));
                            evolutionary_destiny_reached = true;
                        }
                        // Is it (now, or perhaps originally) an st01?
                        auto st01 = std::get_if<Tier_N_ST01_BakedDonut<HashBytesCount>>(&destination);
                        if (st01) {
                            // We could proceed evolving the ST01 typestate to something more evolved here...
                        }
                    }
                );

                // If hatchling lock acquisition failed or stop requested during bake, stop immediately.
                if (!hatchling_valid || !evolutionary_destiny_reached || stop_token.stop_requested()) {
                    return; // Exits outer lambda WITHOUT discarding buffer room!
                }

                if (hatchling_valid && evolutionary_destiny_reached && !stop_token.stop_requested()) {
                    ready_to_discard = true;
                }
            }
        );

        // If upper buffer room returned false (is discarding or shutdown requested), terminate loop cleanly
        if (!upper_room_valid || stop_token.stop_requested()) {
            break;
        }

        // ReaderScope has now exited (active_readers_ decremented) - safe to Discard!
        if (ready_to_discard) {
            proxy_.upper_tier_buffer_room_discard();
        }
    }
    // Thread terminates cleanly on stop request or buffer discard
}

} // namespace wedding_cake