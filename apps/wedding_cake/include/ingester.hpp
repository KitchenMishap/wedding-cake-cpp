#pragma once

#include <evolving_disk_state/buffer_room.hpp>

#include <filesystem>
#include <thread>
#include <vector>

#include "ingester_buffer.hpp"
#include "ingester_tier_folder.hpp"
#include "ingestor_worker.hpp"

namespace wedding_cake {

template<size_t HashBytesCount>
class Ingester {
public:
    /// Constructs the Ingester tier and kicks off the asynchronous background worker thread.
    explicit Ingester(const std::filesystem::path& base_path)
        : ingester_tier_folder_(base_path)
        , ingester_buffer_(get_downstream_remaining_capacity(ingester_tier_folder_),
                            get_downstream_next_offset(ingester_tier_folder_))
        // std::jthread automatically creates a stop token and passes it as parameter 1 to ingestor_worker!
        , worker_thread_(&ingestor_worker<HashBytesCount>,
                         std::ref(ingester_buffer_),
                         std::ref(ingester_tier_folder_))
    {
    }

    /// Destructor cleanly halts the pipeline and waits for the worker thread to exit.
    ~Ingester() {
        // 1. Signal stop request to jthread's stop token
        worker_thread_.request_stop();

        // 2. Unblock any cv.wait() inside ingester_buffer_ so worker loop exits immediately
        ingester_buffer_.stop();

        // 3. jthread destructor automatically joins here
    }

    // Non-copyable, non-movable
    Ingester(const Ingester&) = delete;
    Ingester& operator=(const Ingester&) = delete;
    Ingester(Ingester&&) = delete;
    Ingester& operator=(Ingester&&) = delete;

    // Client interface
    // This can block (which is fine)
    void feed(uint64_t block_height, std::vector<std::optional<HashBytes<HashBytesCount>>> hashes) {
        ingester_buffer_.feed_block_of_hashes(hashes);  // ToDo this will need the block_height someday...
    }
    void flush() {
        ingester_buffer_.flush();
    }

    // Forward lookup requests to buffer
    std::optional<HashBytes<HashBytesCount>> forward_lookup(GlobalPi global_pi, bool& found) const {
        auto result = ingester_buffer_.forward_lookup(global_pi, found);
        if (found) {
            return result;
        }
        result = ingester_tier_folder_.forward_lookup(global_pi, found);
        return result;
    }
    [[nodiscard]] GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const {
        auto result = ingester_buffer_.reverse_lookup(hash);
        if (result != GLOBAL_PI_NO_MATCH)
            return result;
        result = ingester_tier_folder_.reverse_lookup(hash);
        return result;
    }

private:
    // Member declaration order matters!
    // ingester_tier_folder_ and ingester_buffer_ MUST be declared BEFORE worker_thread_
    // so they are fully initialized BEFORE the thread starts running, and destroyed AFTER the thread joins.
    IngesterTierFolder<HashBytesCount> ingester_tier_folder_;
    IngesterBuffer<HashBytesCount> ingester_buffer_;

    // std::jthread automatically manages stop token lifetime and joins on destruction
    std::jthread worker_thread_;

    static uint64_t get_downstream_remaining_capacity(const IngesterTierFolder<HashBytesCount>& ingester_tier_folder) {
        return ingester_tier_folder.hatchling().remaining_capacity();
    }
    static PiOffset get_downstream_next_offset(const IngesterTierFolder<HashBytesCount>& ingester_tier_folder) {
        return ingester_tier_folder.hatchling().next_offset();
    }
};

static_assert(Lookupable<Ingester<32>, 32>,
          "Ingester<32> must fulfill the Lookupable concept!");

} // namespace wedding_cake