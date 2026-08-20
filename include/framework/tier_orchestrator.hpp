// Purpose: Inspects the physical directory structure on boot to instantiate the correct worker class.
// Purpose: Handles state transitions by locking finished subfolders read-only and removing manifest files.
// Purpose: Drives execution slices until reaching the terminal Product state in total settled determinism.

#pragma once
#include "TypeStates.hpp"
#include "ManifestEngine.hpp"
#include "SliceContext.hpp"
#include <memory>

class TierOrchestrator {
public:
    explicit TierOrchestrator(fs::path sack_dir) : sack_dir_(std::move(sack_dir)) {}

    void boot_and_recover() {
        // 1. Inspect physical folders to detect active state
        if (fs::exists(sack_dir_ / ProductState::folder_name)) {
            current_state_ = StateKind::Product;
            return;
        }
        if (fs::exists(sack_dir_ / SprinklingSackWorker::folder_name)) {
            current_state_ = StateKind::Sprinkling;
            active_dir_ = sack_dir_ / SprinklingSackWorker::folder_name;
        } else if (fs::exists(sack_dir_ / FillingSackWorker::folder_name)) {
            current_state_ = StateKind::Filling;
            active_dir_ = sack_dir_ / FillingSackWorker::folder_name;
        } else {
            current_state_ = StateKind::Origin;
            active_dir_ = sack_dir_;
        }

        // 2. Perform generic manifest recovery on active workspace
        if (current_state_ != StateKind::Origin && current_state_ != StateKind::Product) {
            ManifestEngine::recover_state_directory(active_dir_);
            manifest_engine_ = std::make_unique<ManifestEngine>(active_dir_);
        }
    }

    void transition_to_next_state() {
        if (current_state_ == StateKind::Filling) {
            finalize_current_state(sack_dir_ / SprinklingSackWorker::folder_name);
            current_state_ = StateKind::Sprinkling;
        } else if (current_state_ == StateKind::Sprinkling) {
            finalize_current_state(sack_dir_ / ProductState::folder_name);
            current_state_ = StateKind::Product;
        }
    }

private:
    enum class StateKind { Origin, Filling, Sprinkling, Product };

    void finalize_current_state(const fs::path& next_dir) {
        // 1. Delete manifest file to achieve bit-for-bit settled determinism
        fs::remove(active_dir_ / "manifest.tx");

        // 2. Lock current active folder to Read-Only
        if (active_dir_ != sack_dir_) {
            fs::permissions(
                active_dir_,
                fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
                fs::perm_options::replace
            );
        }

        // 3. Create next atomic state folder
        fs::create_directory(next_dir);
        active_dir_ = next_dir;

        if (next_dir.filename() != ProductState::folder_name) {
            manifest_engine_ = std::make_unique<ManifestEngine>(active_dir_);
        } else {
            manifest_engine_.reset();
        }
    }

    fs::path sack_dir_;
    fs::path active_dir_;
    StateKind current_state_ = StateKind::Origin;
    std::unique_ptr<ManifestEngine> manifest_engine_;
};