#pragma once
#include <evolving_disk_state/tier.hpp>

#include "evolving_type_states.hpp"

namespace wedding_cake {

template <size_t HashBytesCount>
class Tier0: public evolving_disk_state::Tier {
public:
    Tier0(CreateNewOnDisk, const std::filesystem::path& base_path, const size_t sack_size) :
    Tier(base_path, 0)
    , sack_size_(sack_size)

    {
    }

    Tier0(LoadExistingFromDisk, const std::filesystem::path& base_path)
    : Tier(base_path, 0)
    {
        throw std::runtime_error("Tier0::LoadFromDisk not implemented");
    }

    Tier0(const Tier0& other) = delete;

    ~Tier0() override = default;

    std::unique_ptr<evolving_disk_state::Evolver> new_origin_evolver(uint64_t start_offset) override {
        return std::make_unique<origin_evolver_t>(CreateNewOnDisk{}, *this, PiOffset(start_offset), sack_size_);
    }

private:
    size_t sack_size_;
    typedef Sack<HashBytesCount> origin_evolver_t;
};

}