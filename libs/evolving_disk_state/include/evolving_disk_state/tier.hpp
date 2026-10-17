#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace evolving_disk_state {

class Evolver;

class Tier {
public:
    virtual ~Tier() = default;

    // The tier determines the type ('species') of the origin evolver
    // (Tier0 often uses a different evolutionary path)
    virtual std::unique_ptr<Evolver> new_origin_evolver(uint64_t start_offset) = 0;

    static std::filesystem::path evolver_folder_name(uint64_t start_offset) {
        return std::string("Offset_") + std::to_string(start_offset);
    }

    void create_untyped_evolver_folder(uint64_t start_offset) const {
        const auto path = this->path() / evolver_folder_name(start_offset);
        std::filesystem::create_directory(path);
    }

    [[nodiscard]] std::filesystem::path path() const {
        return base_path_ / (std::string("Tier") + std::to_string(tier_index_));
    }

protected:
    Tier(std::filesystem::path base_path, const size_t tier_index) :
        base_path_(std::move(base_path))
        , tier_index_(tier_index)
    {
        std::filesystem::create_directory(path());
    }

private:
    std::filesystem::path base_path_;
    size_t tier_index_;
};

}
