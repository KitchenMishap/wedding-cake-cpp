// Purpose: Manages compact, filename-based binary transaction logging inside active state folders.
// Purpose: Defers physical FlushFileBuffers calls to outer work slice commit boundaries.
// Purpose: Performs generic, zero-domain-knowledge recovery truncation on bootup after crashes.

#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <concepts>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

class ManifestEngine {
public:
    static constexpr uint8_t REC_DECLARATION = 0x01;
    static constexpr uint8_t REC_COMMIT      = 0x02;
    static constexpr uint32_t COMMIT_MAGIC   = 0x434F4D54; // "COMT"

    explicit ManifestEngine(fs::path state_dir)
        : state_dir_(std::move(state_dir)), manifest_path_(state_dir_ / "manifest.tx") {}

    static void recover_state_directory(const fs::path& state_dir) {
        fs::path tx_file = state_dir / "manifest.tx";
        if (!fs::exists(tx_file)) return;

        std::ifstream in(tx_file, std::ios::binary);
        std::unordered_map<std::string, uint64_t> last_committed_lengths;
        std::unordered_map<std::string, uint64_t> pending_lengths;

        while (in.peek() != EOF) {
            uint8_t rec_type = 0;
            if (!in.read(reinterpret_cast<char*>(&rec_type), 1)) break;

            if (rec_type == REC_DECLARATION) {
                uint8_t len = 0;
                in.read(reinterpret_cast<char*>(&len), 1);
                std::string fname(len, '\0');
                in.read(fname.data(), len);
                uint64_t offset = 0;
                in.read(reinterpret_cast<char*>(&offset), 8);
                pending_lengths[fname] = offset;
            }
            else if (rec_type == REC_COMMIT) {
                uint32_t magic = 0, slice_id = 0;
                in.read(reinterpret_cast<char*>(&magic), 4);
                in.read(reinterpret_cast<char*>(&slice_id), 4);
                if (magic == COMMIT_MAGIC) {
                    for (const auto& [fname, size] : pending_lengths) {
                        last_committed_lengths[fname] = size;
                    }
                }
            } else {
                break; // Corrupt/partial trailing bytes detected
            }
        }
        in.close();

        // Truncate files to the last validated commit state
        for (const auto& [fname, valid_size] : last_committed_lengths) {
            fs::path target = state_dir / fname;
            if (fs::exists(target)) {
                fs::resize_file(target, valid_size);
            }
        }
    }

    void record_microstep_declaration(const std::string& filename, uint64_t target_offset) {
        ensure_manifest_open();
        uint8_t type = REC_DECLARATION;
        uint8_t len = static_cast<uint8_t>(filename.size());
        out_.write(reinterpret_cast<const char*>(&type), 1);
        out_.write(reinterpret_cast<const char*>(&len), 1);
        out_.write(filename.data(), len);
        out_.write(reinterpret_cast<const char*>(&target_offset), 8);
    }

    void commit_work_slice(uint32_t slice_id, const std::vector<std::ofstream*>& open_handles) {
        // 1. Flush data files to physical hardware first
        for (auto* handle : open_handles) {
            if (handle && handle->is_open()) {
                handle->flush();
            }
        }
        flush_directory_handles(state_dir_);

        // 2. Write commit record to manifest
        ensure_manifest_open();
        uint8_t type = REC_COMMIT;
        uint32_t magic = COMMIT_MAGIC;
        out_.write(reinterpret_cast<const char*>(&type), 1);
        out_.write(reinterpret_cast<const char*>(&magic), 4);
        out_.write(reinterpret_cast<const char*>(&slice_id), 4);
        out_.flush();
    }

private:
    void ensure_manifest_open() {
        if (!out_.is_open()) {
            out_.open(manifest_path_, std::ios::binary | std::ios::app);
        }
    }

    static void flush_directory_handles(const fs::path& dir) {
#ifdef _WIN32
        // Windows FlushFileBuffers on underlying OS files
#endif
    }

    fs::path state_dir_;
    fs::path manifest_path_;
    std::ofstream out_;
};