#pragma once

#include "concepts_wedding_cake.hpp"
//#include "tiers.hpp"

#include <utility>
#include <vector>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include "ingester.hpp"
#include "ingester_tier_folder.hpp"
#include "mishap_assert.hpp"
#include "tier_n_folder.hpp"
#include "tier_n_hydrator.hpp"
#include "tier_n_transition_lookup.hpp"
#include "tier_n_transition_proxy.hpp"
#include "tier_n_transition_worker.hpp"

namespace wedding_cake {

// Named StoreB to distinguish from the legacy class Store
// A store consists of a sequence of one or more tiers, with N=tier_index=0 representing a special ingestion tier.
// A number of C++ objects (listed below) are associated with each tier index.
// Where one of these objects needs to know about another (typically a neighbour), the object has a non-owning pointer.
// => TierNFolder           Manages disk storage for the tier.
//                          Holds the hatchling (input) and buffer room (output).
// => TierNTransitionProxy  Manages thread-safe access to the buffer room in tier N, and the hatchling in tier N+1.
//                          Its (thread) clients are TierNTransitionWorker and TierNTransitionLookup
// => TierNTransitionWorker An endless loop baking from the buffer room in tier N, to the hatchling in tier N+1.
//                          When the hatchling is fully-fledged, moves it to the buffer room in tier N+1.
// => TierNTransitionLookup Forward and reverse hash lookups
//                          Chooses between using (equivalent) data from tier N buffer room and/or tier N+1 hatchling.
//                          Makes this choice based on baking progress by the TierNTransitionWorker.
// TierNFolder's are arranged both horizontally (along the timeline) and vertically (for the different tiers).
// The Ingester Tier (N=0) is considered UPPERMOST, with the largest tier_index being LOWERMOST.
// TierNTransitionProxy, TierNTransitionWorker, and TierNTransitionLookup STRADDLE two adjacent tier slots vertically.
// Therefore, each of these might refer to an UpperBufferRoom and a LowerHatchling. (and sometimes, LowerBufferRoom).
// The references to "Lower" objects MAY BE NULL because at any point in time, there are a finite number of tiers.
template <size_t HashBytesCount>
class StoreB {
public:
    explicit StoreB(std::filesystem::path base_path);

    void feed(uint64_t block_height, const std::vector<std::optional<HashBytes<HashBytesCount>>>& hashes);
    void flush();

    // Satisfy Lookupable concept
    [[nodiscard]] GlobalPi first_offset() const;
    [[nodiscard]] GlobalPi next_offset() const;
    [[nodiscard]] std::optional<HashBytes<HashBytesCount>> forward_lookup(const GlobalPi& global_pi) const;
    [[nodiscard]] GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const;

private:
    std::filesystem::path base_path_;

    // The ingester
    Ingester<HashBytesCount> ingester_;

    // The Tier Folders
    // Use ingester_.tier_folder() as tier 0 (tier_folders[0]==nullptr!)
    std::vector<std::unique_ptr<TierNFolder<HashBytesCount>>> tier_folders_;

    // The Transition Objects (kept same size as tier_folders)
    // Use as tier 0 (transition_XXX[0]==nullptr!)
    TierNTransitionProxy<IngesterTierFolder<HashBytesCount>, HashBytesCount> transition_proxies_0_;
    TierNTransitionWorker<IngesterTierFolder<HashBytesCount>, HashBytesCount> transition_workers_0_;
    TierNTransitionLookup<IngesterTierFolder<HashBytesCount>, HashBytesCount> transition_lookups_0_;
    // these[0] == nullptr so use the above instead of index 0
    std::vector<std::unique_ptr<TierNTransitionProxy<TierNFolder<HashBytesCount>, HashBytesCount>>> transition_proxies_;
    std::vector<std::unique_ptr<TierNTransitionWorker<TierNFolder<HashBytesCount>, HashBytesCount>>> transition_workers_;
    std::vector<std::unique_ptr<TierNTransitionLookup<TierNFolder<HashBytesCount>, HashBytesCount>>> transition_lookups_;

    void create_tier_if_needed(int tier_index);
    void start_workers();
    [[nodiscard]] static std::filesystem::path ensure_base_dir_created(std::filesystem::path base_path);
};

static_assert(Lookupable<StoreB<32>, 32>, "StoreB must implement Lookupable concept");

template<size_t HashBytesCount>
StoreB<HashBytesCount>::StoreB(std::filesystem::path base_path)
    : base_path_(ensure_base_dir_created(std::move(base_path)))
    , ingester_(base_path_)
    , transition_proxies_0_(&(ingester_.tier_folder())) // The ingester always exists, the lower tier is nullptr for now
    , transition_workers_0_(&transition_proxies_0_,
        ms_prefix_bits_for_tier(0), ms_prefix_bits_for_tier(1) - ms_prefix_bits_for_tier(0),
        ingester_donut_capacity)
    , transition_lookups_0_(&transition_proxies_0_)
{
    // these[0] are nullptr to remind you you should be using the _0 objects instead!
    tier_folders_.push_back(nullptr);
    transition_proxies_.push_back(nullptr);
    transition_workers_.push_back(nullptr);
    transition_lookups_.push_back(nullptr);

    // Attach callback for when the ingester buffer room is full
    ingester_.tier_folder().set_on_full_callback([this]() {
        create_tier_if_needed(1);
    });

    for (size_t tier_index = 1; true; ++tier_index) {
        auto tier = hydrate_tier_if_exists<HashBytesCount>(base_path_, tier_index);
        if (tier) {
            // Attach callback for when the tier buffer room is full
            tier->set_on_full_callback([this, tier_index]() {
                create_tier_if_needed(tier_index + 1);
            });
            tier_folders_.push_back(std::move(tier));

            // We can now create and/or wire in (straddling tiers N-1 and N)
            // transition proxy, transition worker, and transition lookup
            if (tier_index==1) {
                // The transition objects are already created, wire in this next tier for the proxy object
                transition_proxies_0_.set_lower_tier(tier_folders_.back().get());
            } else {
                transition_proxies_.push_back(std::make_unique<
                    TierNTransitionProxy<TierNFolder<HashBytesCount>, HashBytesCount>>(
                    tier_folders_[tier_index - 1].get(), tier.get()));
                transition_workers_.push_back(std::make_unique<
                    TierNTransitionWorker<TierNFolder<HashBytesCount>, HashBytesCount>>(
                    transition_proxies_.back().get(),
                    ms_prefix_bits_for_tier(tier_index - 1),
                    ms_prefix_bits_for_tier(tier_index) - ms_prefix_bits_for_tier(tier_index - 1),
                    tier_n_donut_capacity(tier_index - 1)));
                transition_lookups_.push_back(std::make_unique<
                    TierNTransitionLookup<TierNFolder<HashBytesCount>, HashBytesCount>>(
                    transition_proxies_.back().get()));
            }
        } else {
            break;
        }
    }
    start_workers();
}

template<size_t HashBytesCount>
void StoreB<HashBytesCount>::create_tier_if_needed(int tier_index) {
    MISHAP_ASSERT(tier_index != 0, "create_tier_if_needed(): should never need to create ingester tier");
    if (tier_folders_.size() > tier_index && tier_folders_[tier_index] != nullptr) {
        return; // Tier folder already exists
    }
    auto new_offset = first_offset_of_tier(tier_index);
    if (tier_index==1) {
        // Previous tier folder was ingester
        tier_folders_.push_back(std::make_unique<TierNFolder<HashBytesCount>>(
            tier_index, base_path_, new_offset));
        // Wire the existing ingester proxy to the new tier as lower_tier
        transition_proxies_0_.set_lower_tier(tier_folders_.back().get());
    } else {
        // Previous tier folder was not ingester
        tier_folders_.push_back(std::make_unique<TierNFolder<HashBytesCount>>(
            tier_index, base_path_, new_offset));
        // Wire the existing proxy to the new tier as lower_tier
        transition_proxies_.back()->set_lower_tier(tier_folders_.back().get());
    }
    // Create the transition objects to the next tier (for when it finally arrives someday)
    transition_proxies_.push_back(std::make_unique<
        TierNTransitionProxy<TierNFolder<HashBytesCount>, HashBytesCount>>(
        tier_folders_.back().get()));   // Next tier doesn't exist yet
    transition_workers_.push_back(std::make_unique<
        TierNTransitionWorker<TierNFolder<HashBytesCount>, HashBytesCount>>(
        transition_proxies_.back().get(),
        ms_prefix_bits_for_tier(tier_index),
        ms_prefix_bits_for_tier(tier_index + 1) - ms_prefix_bits_for_tier(tier_index),
        tier_n_donut_capacity(tier_index)));
    transition_lookups_.push_back(std::make_unique<
        TierNTransitionLookup<TierNFolder<HashBytesCount>, HashBytesCount>>(
        transition_proxies_.back().get()));
}

template<size_t HashBytesCount>
void StoreB<HashBytesCount>::feed(
    uint64_t block_height, const std::vector<std::optional<HashBytes<HashBytesCount>>>& hashes) {
    ingester_.feed(block_height, hashes);
}

template<size_t HashBytesCount>
void StoreB<HashBytesCount>::flush() {
    ingester_.flush();
}

template<size_t HashBytesCount>
GlobalPi StoreB<HashBytesCount>::first_offset() const {
    // ToDo: Change this to the oldest tier when others implemented
    return ingester_.first_offset();
}

template<size_t HashBytesCount>
GlobalPi StoreB<HashBytesCount>::next_offset() const {
    return ingester_.next_offset();    // The ingestor tier is the youngest
}

template<size_t HashBytesCount>
std::optional<HashBytes<HashBytesCount>> StoreB<HashBytesCount>::forward_lookup(const GlobalPi& global_pi) const {
    MISHAP_ASSERT(false, "Sorry! Not implemented until we have a Foreperson");
    return std::nullopt;
}
template<size_t HashBytesCount>
GlobalPi StoreB<HashBytesCount>::reverse_lookup(const HashBytes<HashBytesCount>& hash) const {
    MISHAP_ASSERT(false, "Sorry! Not implemented until we have a Foreperson");
    return GLOBAL_PI_NO_MATCH;
}

template<size_t HashBytesCount>
std::filesystem::path StoreB<HashBytesCount>::ensure_base_dir_created(std::filesystem::path base_path) {
    std::filesystem::create_directories(base_path);
    return base_path;
}

template<size_t HashBytesCount>
void StoreB<HashBytesCount>::start_workers() {
    for (int i = 0; i < transition_workers_.size(); i++)
        if (i==0) {
            // worker 0 has a bespoke object instead of transition_workers_[0] (which is nullptr)
            transition_workers_0_.start();
        } else {
            transition_workers_[i]->start();
        }
}

}
