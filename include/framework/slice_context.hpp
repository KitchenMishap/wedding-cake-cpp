// Purpose: Provides nested RAII execution scopes for Work Slices and Microsteps.
// Purpose: Hands opaque, requested file handles to worker lambdas during microsteps.
// Purpose: Automatically records manifest declarations without worker classes knowing transaction mechanics.

#pragma once
#include "ManifestEngine.hpp"
#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>
#include <functional>

class SliceContext {
public:
    SliceContext(fs::path state_dir, ManifestEngine& manifest, uint32_t slice_id)
        : state_dir_(std::move(state_dir)), manifest_(manifest), slice_id_(slice_id) {}

    ~SliceContext() {
        std::vector<std::ofstream*> handles;
        for (auto& [_, stream] : open_files_) {
            handles.push_back(&stream);
        }
        manifest_.commit_work_slice(slice_id_, handles);
    }

    // Microstep execution scope
    void microstep(
        const std::vector<std::string>& filenames,
        const std::function<void(const std::unordered_map<std::string, std::ofstream*>&)>& action
    ) {
        std::unordered_map<std::string, std::ofstream*> active_handles;

        for (const auto& fname : filenames) {
            if (!open_files_.contains(fname)) {
                open_files_.emplace(
                    fname,
                    std::ofstream(state_dir_ / fname, std::ios::binary | std::ios::app)
                );
            }
            auto& stream = open_files_[fname];
            uint64_t current_pos = static_cast<uint64_t>(stream.tellp());
            manifest_.record_microstep_declaration(fname, current_pos);
            active_handles[fname] = &stream;
        }

        // Run worker logic
        action(active_handles);
    }

private:
    fs::path state_dir_;
    ManifestEngine& manifest_;
    uint32_t slice_id_;
    std::unordered_map<std::string, std::ofstream> open_files_;
};