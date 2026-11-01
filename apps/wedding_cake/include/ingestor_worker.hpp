#pragma once

#include "ingester_buffer.hpp"
#include "ingester_tier_folder.hpp"

#include <variant>
#include <stop_token>
#include <cassert>

namespace wedding_cake {

template<size_t HashBytesCount>
void ingestor_worker(const std::stop_token please_stop,
                     IngesterBuffer<HashBytesCount>& ingester_buffer,
                     IngesterTierFolder<HashBytesCount>& ingester_tier_folder)
{
    // Continuously loop until a stop is requested
    while (!please_stop.stop_requested()) {

        ingester_buffer.with_at_least_n_hashes(
            [&](typename IngesterBuffer<HashBytesCount>::BatchType& ram_buffer, bool is_flushing) -> size_t
            {
                if (ram_buffer.empty()) {
                    // Nothing to consume (e.g. spurious wake or pure flush request)
                    if (is_flushing) {
                        ingester_tier_folder.flush_manifest_to_disk();
                    }
                    auto& active_st00 = ingester_tier_folder.hatchling();
                    return active_st00.remaining_capacity();
                }

                // 1. Ingestion Loop: Continuously fill active Hatchling and spill into new ST00s if needed
                while (!ram_buffer.empty()) {
                    auto& st00 = ingester_tier_folder.hatchling();

                    // 2. Consume hashes from RAM buffer into Hatchling
                    size_t consumed_count = st00.append_hashes(ram_buffer);

                    // 3. Erase consumed items from RAM buffer immediately
                    if (consumed_count > 0) {
                        ram_buffer.erase(ram_buffer.begin(), ram_buffer.begin() + consumed_count);
                    } else {
                        // Prevent infinite loop if append_hashes fails to consume items
                        break;      // ToDo: Needed?
                    }

                    // 4. If Hatchling is full, promote to BufferRoom and spawn fresh Hatchling
                    if (st00.remaining_capacity() == 0) {
                        ingester_tier_folder.promote_hatchling_and_spawn_next();
                    }
                } // Loop repeats if ram_buffer still contains remaining hashes

                // Invariant: ram_buffer is now completely drained!
                assert(ram_buffer.empty() && "ingester_worker(): Expected RAM buffer to be fully drained");

                // 5. Handle explicit end-of-stream / flush requests
                if (is_flushing) {
                    ingester_tier_folder.flush_manifest_to_disk();
                }

                // 6. Return remaining capacity of the active Hatchling back to IngesterBuffer.
                const auto& active_st00 = ingester_tier_folder.hatchling();
                return active_st00.remaining_capacity();
            }
        );
    }
}

} // namespace wedding_cake