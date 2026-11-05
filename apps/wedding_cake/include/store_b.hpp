#pragma once

#include "concepts_wedding_cake.hpp"
//#include "tiers.hpp"

#include <utility>
#include <vector>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include "ingester.hpp"

namespace wedding_cake {

// Named StoreB to distinguish from the legacy class Store
template <size_t HashBytesCount>
class StoreB {
public:
    explicit StoreB(std::filesystem::path base_path);

    void feed(uint64_t block_height, const std::vector<std::optional<HashBytes<HashBytesCount>>>& hashes);
    void flush();

    // Satisfy Lookupable concept
    [[nodiscard]] std::optional<HashBytes<HashBytesCount>> forward_lookup(const GlobalPi& global_pi, bool& found) const;
    [[nodiscard]] GlobalPi reverse_lookup(const HashBytes<HashBytesCount>& hash) const;

private:
    std::filesystem::path base_path_;
    Ingester<HashBytesCount> ingester_tier_;

    [[nodiscard]] static std::filesystem::path ensure_base_dir_created(std::filesystem::path base_path);
};

static_assert(Lookupable<StoreB<32>, 32>, "StoreB must implement Lookupable concept");

template<size_t HashBytesCount>
StoreB<HashBytesCount>::StoreB(std::filesystem::path base_path)
    : base_path_(ensure_base_dir_created(std::move(base_path)))
    , ingester_tier_(base_path_)
{
}

template<size_t HashBytesCount>
void StoreB<HashBytesCount>::feed(
    uint64_t block_height, const std::vector<std::optional<HashBytes<HashBytesCount>>>& hashes) {
    ingester_tier_.feed(block_height, hashes);
}

template<size_t HashBytesCount>
void StoreB<HashBytesCount>::flush() {
    ingester_tier_.flush();
}

template<size_t HashBytesCount>
std::optional<HashBytes<HashBytesCount>> StoreB<HashBytesCount>::forward_lookup(const GlobalPi& global_pi, bool& found) const {
    auto result = ingester_tier_.forward_lookup(global_pi, found);
    if (found) {return result;}
    // ToDo: Try subsequent tiers
    return std::nullopt;
}
template<size_t HashBytesCount>
GlobalPi StoreB<HashBytesCount>::reverse_lookup(const HashBytes<HashBytesCount>& hash) const {
    auto result = ingester_tier_.reverse_lookup(hash);
    if (result!= GLOBAL_PI_NO_MATCH) {return result;}
    // ToDo: Try subsequent tiers
    return GLOBAL_PI_NO_MATCH;
}

template<size_t HashBytesCount>
std::filesystem::path StoreB<HashBytesCount>::ensure_base_dir_created(std::filesystem::path base_path) {
    std::filesystem::create_directories(base_path);
    return base_path;
}

}
