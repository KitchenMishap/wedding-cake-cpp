#pragma once
#include <evolving_disk_state/tier.hpp>

#include "evolving_type_states.hpp"

class Tier0: public evolving_disk_state::Tier {
public:
    class CreateNewOnDisk{};    // Tag type to pass to constructor to indicate "create new on disk"
    class LoadFromDisk{};       // Tag type to pass to constructor to indicate "load from disk"

    Tier0(CreateNewOnDisk, const std::filesystem::path& base_path)
    : Tier(base_path, 0)
    {
    }

    Tier0(LoadFromDisk, const std::filesystem::path& base_path)
    : Tier(base_path, 0)
    {
        throw std::runtime_error("Tier0::LoadFromDisk not implemented");
    }

    Tier0(const Tier0& other) = delete;

    ~Tier0() override = default;

    std::unique_ptr<evolving_disk_state::Evolver> new_origin_evolver(uint64_t start_offset) override {
        return std::make_unique<origin_evolver_t>(origin_evolver_t::CreateNewOnDisk(), *this, start_offset);
    }

private:
    typedef Sack origin_evolver_t;
};
