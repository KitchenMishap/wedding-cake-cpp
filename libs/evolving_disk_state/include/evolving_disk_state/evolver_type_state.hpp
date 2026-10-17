#pragma once

#include "tier.hpp"

#include <filesystem>

namespace evolving_disk_state {

// Base class for all Evolver objects
// An Evolver is an object somewhere on a path of evolution
class Evolver {
public:
    virtual ~Evolver() = default;

    [[nodiscard]] uint64_t start_offset() const noexcept { return start_offset_; }

    [[nodiscard]] std::filesystem::path object_path() const noexcept {
        return parent_tier_.path() / parent_tier_.evolver_folder_name(start_offset_);
    }

protected:
    Evolver(const Tier& parent_tier, const uint64_t start_offset)
        : parent_tier_(parent_tier), start_offset_(start_offset) {}

    // A particular Evolver type has a tag (sometimes used to generate a subfolder)
    [[nodiscard]] virtual std::filesystem::path tag() const noexcept = 0;

    // An Evolver object is always contained within a Tier and does not move between tiers
    const evolving_disk_state::Tier& parent_tier_;
    uint64_t start_offset_{0};
};

// A path of evolution starts with an origin
class EvolverOrigin : public Evolver {
public:
    class CreateOnDisk {};  // Tag to pass to ctor to instruct "create on disk"
    class LoadFromDisk {};  // Tag to pass to ctor to instruct "load from disk"

    // Because a path of evolution starts with an EvolverOrigin, we have a public ctor that creates a new one on disk
    EvolverOrigin(CreateOnDisk, const Tier& parent_tier, const uint64_t start_offset, const std::filesystem::path tag)
        : Evolver(parent_tier, start_offset)
    {
        auto object_subfolder = object_path();
        if( std::filesystem::exists(object_subfolder) ) {
            throw std::runtime_error("Evolver object with this offset already exists");
        }
        std::filesystem::create_directory(object_subfolder);
        // Creation of the tag folder is the atomic operation that indicates the existence of this object
        // Consequently an empty tag folder must be treated as representing a valid object!
        std::filesystem::create_directory(object_subfolder / tag);
    }
};

// A path of evolution ends with a destiny
class EvolverDestiny : public Evolver {
    // Because an EvolverDestiny can only arise by evolution, only its derived class can call the ctor
protected:
    EvolverDestiny(const Tier& parent_tier, const uint64_t start_offset)
        : Evolver(parent_tier, start_offset) {}
};

// There is currently no concept of an Evolver that is both an origin and a destiny

}