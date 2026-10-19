#pragma once
#include <evolving_disk_state/tier.hpp>

#include "concepts.hpp"
#include "evolving_type_states.hpp"

namespace wedding_cake {

template <size_t HashBytesCount>
class Tier0: public evolving_disk_state::Tier {
public:
    Tier0(CreateNewOnDisk, const std::filesystem::path& base_path, const size_t sack_size) :
    Tier(base_path, 0)
    , sack_size_(sack_size)
    , active_evolver_(std::make_unique<origin_evolver_t>(CreateNewOnDisk{}, *this, PiOffset(0), sack_size_))
    {
    }

    Tier0(LoadExistingFromDisk, const std::filesystem::path& base_path, const size_t sack_size)
    : Tier(base_path, 0)
    , sack_size_(sack_size)
    {
        throw std::runtime_error("Tier0::LoadFromDisk not implemented");
    }

    [[nodiscard]] uint64_t Count() const override {return active_evolver_->Count();}
    [[nodiscard]] PiOffset Offset() const {return active_evolver_->Offset();}

    Tier0(const Tier0& other) = delete;

    ~Tier0() override = default;

    std::unique_ptr<evolving_disk_state::Evolver> new_origin_evolver(uint64_t start_offset) override {
        return std::make_unique<origin_evolver_t>(CreateNewOnDisk{}, *this, PiOffset(start_offset), sack_size_);
    }

private:
    size_t sack_size_;
    typedef Sack<HashBytesCount> origin_evolver_t;
    std::unique_ptr<origin_evolver_t> active_evolver_;
};

static_assert(SubStoreConcept<Tier0<32>, 32>, "Tier0 must implement SubStoreConcept");

}