#pragma once

#include "evolver_type_state.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace evolving_disk_state {

// TierFolder represents a Tier Folder on disk, such as Ingestor/ or Tier0/.
// A Tier Folder on disk contains at least one Object Folder named Offset_<NNN>/.
// A TierFolder object consists of exactly one "Hatchling" (aka "Remainder" for Ingestor/ tier),
// and a BufferRoom.
// The Object Folder with with the highest NNN is implicitly the Hatchling.
// All other Object Folders are implicitly in the BufferRoom.
// When a particular TierFolder is created for the first time, it will have a
// single empty Object Folder named Offset_0/ (the Hatchling, and the BufferRoom will be empty).

// Supply a TierHydrator that can hydrate Evolvers for your particular TierFolder.
class TierHydrator {
public:
    virtual ~TierHydrator() = default;
    virtual std::unique_ptr<Evolver>HydrateEvolverFromDisk(const std::filesystem::path& offset_dir) = 0;
};

template <typename TOrigin, typename TDestiny>
class TierFolder {
    // Hydrates the Hatchling and BufferRoom if object folders exist
    // Otherwise, creates an empty Hatchling with Offset_0/ and an empty BufferRoom.
    TierFolder(const std::filesystem::path& path, int fanout_bit_count, TierHydrator* hydrator_for_tier);

private:
    std::filesystem::path path_;
    int fanout_bit_count_;
    int fanout_;
    std::unique_ptr<Evolver> hatchling_;
    std::vector<std::unique_ptr<TDestiny>> buffer_room_;
};

constexpr std::string object_folder_prefix() {
    return "Offset_";
}

inline std::filesystem::path object_folder_name(const uint64_t offset) {
    return object_folder_prefix() + std::to_string(offset);
}

inline uint64_t object_folder_single_offset(const std::filesystem::path& object_folder) {
    std::string dirName = object_folder.filename().string();    // ToDo: filename()? Are you sure
    // Check if folder name starts with "Offset_"
    if (dirName.rfind(object_folder_prefix(), 0) == 0) {
        const std::string numStr = dirName.substr(object_folder_prefix().length());
        try {
            size_t pos = 0;
            int n = std::stoi(numStr, &pos);
            // Ensure the entire suffix was a valid integer
            if (pos == numStr.length()) {
                return n;
            }
            throw std::runtime_error("object_folder_single_offset(): Invalid folder name format");
        } catch (const std::exception&) {
            throw std::runtime_error("object_folder_single_offset(): Invalid folder name format");
        }
    }
    throw std::runtime_error("object_folder_single_offset(): Invalid folder name format");
}

// Find sorted "nnn"'s where nnn are found in folder names starting with "Offset_"
inline std::vector<uint64_t> object_folder_offsets(const std::filesystem::path& basePath) {
    std::vector<uint64_t> offsetValues;
    const std::string prefix = object_folder_prefix();

    if (!std::filesystem::exists(basePath) || !std::filesystem::is_directory(basePath)) {
        return offsetValues;
    }

    for (const auto& entry : std::filesystem::directory_iterator(basePath)) {
        if (entry.is_directory()) {
            std::string dirName = entry.path().filename().string();     // ToDo: filename()? Are you sure

            // Check if folder name starts with "Offset_"
            if (dirName.rfind(prefix, 0) == 0) {
                std::string numStr = dirName.substr(prefix.length());
                try {
                    size_t pos = 0;
                    int n = std::stoi(numStr, &pos);
                    // Ensure the entire suffix was a valid integer
                    if (pos == numStr.length()) {
                        offsetValues.push_back(n);
                    }
                } catch (const std::exception&) {
                    // Ignore folders that start with "Offset_" but do not end with a valid integer
                }
            }
        }
    }

    std::ranges::sort(offsetValues);
    return offsetValues;
}

template<typename TOrigin, typename TDestiny>
TierFolder<TOrigin, TDestiny>::TierFolder(
    const std::filesystem::path& path, const int fanout_bit_count, TierHydrator* hydrator_for_tier)
    : fanout_bit_count_(fanout_bit_count)
    , fanout_(1 << fanout_bit_count)
    , hatchling_(nullptr)
{
    buffer_room_.reserve(fanout_);
    std::vector<uint64_t> offsets;
    if (std::filesystem::exists(path)) {
        if (std::filesystem::is_directory(path)) {
            offsets = object_folder_offsets(path);
            if (offsets.empty()) {
                std::filesystem::create_directories(path / object_folder_name(0));
                offsets.push_back(0);
            } else if (offsets.size() > fanout_ + 1) {
                throw std::runtime_error("TierFolder path contains too many object folders");
            }
        } else {
            throw std::runtime_error("TierFolder path is not a directory");
        }
    } else {
        std::filesystem::create_directories(path / object_folder_name(0));
        offsets.push_back(0);
    }
    // offsets now holds the sorted offsets of object folders, and there are between 1 and fanout_ + 1 of them

    // The highest offset is implicitly the hatchling. Hydrate it from disk.
    auto hatchling_offset = offsets.back();
    hatchling_ = hydrator_for_tier->HydrateEvolverFromDisk(object_folder_name(hatchling_offset));

    // The remaining offsets are implicitly in the buffer room. Hydrate them from disk.
    for (int buffer_room_i = 0; buffer_room_i < offsets.size() - 1; ++buffer_room_i) {
        buffer_room_.push_back(hydrator_for_tier->HydrateEvolverFromDisk(
            object_folder_name(offsets[buffer_room_i])));
    }
}


}