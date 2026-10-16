#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "manifest_engine.hpp"

namespace fs = std::filesystem;

class SliceContext {
public:
    SliceContext(fs::path state_dir, ManifestEngine& manifest, uint32_t slice_id)
        : state_dir_(std::move(state_dir)), manifest_(manifest), slice_id_(slice_id) {}

    ~SliceContext() {
        std::vector<std::ofstream*> handles;
        for (auto& [fname, stream] : open_files_) {
            stream.flush();
            // Record the actual end position AFTER all worker writes in this slice completed
            uint64_t final_pos = static_cast<uint64_t>(stream.tellp());
            manifest_.record_microstep_declaration(fname, final_pos);
            handles.push_back(&stream);
        }

        manifest_.commit_work_slice(slice_id_, handles);

        for (auto& [_, stream] : open_files_) {
            if (stream.is_open()) {
                stream.close();
            }
        }
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
            active_handles[fname] = &open_files_[fname];
        }

        // Run worker logic first
        action(active_handles);
    }

private:
    fs::path state_dir_;
    ManifestEngine& manifest_;
    uint32_t slice_id_;
    std::unordered_map<std::string, std::ofstream> open_files_;
};