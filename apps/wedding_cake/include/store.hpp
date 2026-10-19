#pragma once

#include "concepts.hpp"
#include "tiers.hpp"

#include <utility>
#include <vector>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace wedding_cake {

constexpr size_t SackSize = 65535;

template <size_t HashBytesCount>
class Store {
public:
    Store(CreateNewOnDisk, std::filesystem::path base_path)
        : base_path_(std::move(base_path))
    {
        if( std::filesystem::exists(base_path) ) {
            throw std::runtime_error("Store already exists. Manually delete the folder and try again");
        }
        std::filesystem::create_directory(base_path_);
        tiers_.emplace_back(std::make_unique<Tier0<HashBytesCount>>(CreateNewOnDisk{}, base_path_, SackSize));
    }

    // Requirements of SubStoreConcept
    [[nodiscard]] static PiOffset Offset() {return PiOffset{0};}  // A store always represents offset 0
    [[nodiscard]] uint64_t Count() const {return tiers_[0]->Count() + other_tiers_counts_total;}
    [[nodiscard]] std::optional<HashBytes<HashBytesCount>>ForwardLookup(const GlobalPi gpi) const {
        return tiers_[0]->ForwardLookup(gpi);
    }

private:
    std::filesystem::path base_path_;
    std::vector<std::unique_ptr<TierBase<HashBytesCount>>> tiers_;
    uint64_t other_tiers_counts_total{0};    // Total of counts of tiers other than tier[0]
};

static_assert(SubStoreConcept<Store<32>, 32>, "Store must implement SubStoreConcept");

}