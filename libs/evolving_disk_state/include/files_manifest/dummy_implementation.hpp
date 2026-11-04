#pragma once

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

#include "concepts_manifest.hpp"

// files_manifest is a system for creating and writing append-only files to a disk folder
// (and subfolders) in a stateful recoverable commit / flush / recover manner.

// One of the ideas is that progress can be inferred from file lengths

// dummy_implementation.hpp is a dummy implementation of files_manifest which does not support
// recovery from ungraceful shutdowns. It's main purpose is for drafting the interface before
// we implement the final files_manifest. Comments in functions explain what the "real" function will do someday.

// "Reserved files" format in the base_folder:
// A folder containing the empty flag file "manifest_sealed.bin" represents a "finished" state in which writes
//      are no longer permitted.
// A folder without a "manifest_sealed.bin" file but with a "manifest.bin" file represents an
//      "in progress" state, with the "manifest.bin" file describing the committed filenames and lengths.
// A folder NOT containing "manifest_sealed.bin" and not containing a "manifest.bin" file (for example,
//      an empty folder) corresponds to a "not started / no progress committed" state.

// Remember that globally we are aiming for a deterministic "settled" state, a folder and file state that is
// always the same for a given sequence of input data. We therefore cannot leave "Reserved files" lying around
// (unless they are deterministic) without tidying them (deleting them) at some point. This tidying must be "built in".
// "manifest_sealed.bin" is considered to be deterministic and so can stay.

// This dummy implementation, which does not support ungraceful shutdowns, neither writes nor reads
//      the "manifest_sealed.bin" nor "manifest.bin" files, as it does not need them.

namespace files_manifest {

class FilesManifestDummy {
public:
    typedef int FileHandle;
    constexpr static FileHandle InvalidFileHandle = -1;

    // Create a FilesManifestDummy object to start or continue a manifest session for an existing folder on disk.
    explicit FilesManifestDummy(std::filesystem::path base_path);
    ~FilesManifestDummy();

    // Enable move semantics and prevent accidental copying of stream handles
    FilesManifestDummy(FilesManifestDummy&&) noexcept = default;
    FilesManifestDummy& operator=(FilesManifestDummy&&) noexcept = default;
    FilesManifestDummy(const FilesManifestDummy&) = delete;
    FilesManifestDummy& operator=(const FilesManifestDummy&) = delete;

    [[nodiscard]] bool is_started() const;  // I think this is always true for a constructed FilesManifestDummy?
    [[nodiscard]] bool is_finished_sealed() const;

    FileHandle create_new_file(const std::filesystem::path& relative_path);
    FileHandle get_file_handle(const std::filesystem::path& relative_path);
    size_t get_file_size(FileHandle handle);
    void open_file(FileHandle handle);
    void append_to_file(FileHandle handle, std::span<const uint8_t> bytes);
    void close_file(FileHandle handle);
    void commit();  // Does not necessarily flush to hardware
    void flush();   // Flushes to hardware
    void finish_and_seal(); // Includes relevant flushing

private:
    std::filesystem::path base_path_;
    FileHandle next_file_handle_;
    bool finished_sealed_;
    std::map<std::filesystem::path, FileHandle> file_handles_by_rel_path_;
    std::map<FileHandle, std::filesystem::path> rel_paths_by_handle_;
    std::map<FileHandle, size_t> file_sizes_by_handle_;
    std::map<FileHandle, std::ofstream> open_files_by_handle_;
};

static_assert(ManifestLike<FilesManifestDummy>);

inline FilesManifestDummy::FilesManifestDummy(std::filesystem::path base_path)
: base_path_(base_path)
, next_file_handle_(0)
, finished_sealed_(false)
{
    if (!std::filesystem::exists(base_path_)) {
        throw std::runtime_error("FilesManifestDummy ctor: base_path does not exist");
    }
    if (!std::filesystem::is_directory(base_path_)) {
        throw std::runtime_error("FilesManifestDummy ctor: base_path is not a directory");
    }
    // A "real" implementation would first check for a "manifest_sealed.bin" file in the base_path_ directory.
    // If it exists, it sets a "finished_sealed_" flag in memory and DELETES any existing "manifest.bin" file.
    // Otherwise, it checks for a "manifest.bin" file in the base_path_ directory.
    // If it exists, it does the following "potential recovery" task:
    //      It reads the latest valid state recorded in "manifest.bin" into memory.
    //      This valid state specifies a list of files (possibly in subfolders) and their lengths.
    //      For (recursively) every file IN THE FOLDER is checked against its states:
    //          A file not present in the valid state is deleted. (Ungraceful shutdown presumed)
    //          A file longer than its length in the valid state is truncated. (Ungraceful shutdown presumed)
    //          A file shorter than its length in the valid state THROWS AN EXCEPTION.
    //      Any valid state listing for which the file was not found yet THROWS AN EXCEPTION.
    //      The files in the folder now correspond to the last committed state as described in the "mainifest.bin" file.
    // If neither "manifest.bin" nor "manifest_sealed.bin" exist:
    //      All files in the folder are deleted. (Ungraceful shutdown presumed)
    //      Create a new empty manifest.bin file.

    std::vector<std::filesystem::path> relative_paths;
    // Recursively iterate through the directory structure
    for (const auto& entry : std::filesystem::recursive_directory_iterator(base_path)) {
        // Skip directories, symlinks to directories, etc.
        if (entry.is_regular_file()) {
            // Compute path relative to baseDir
            std::filesystem::path rel_path = std::filesystem::relative(entry.path(), base_path);
            relative_paths.push_back(rel_path);
        }
    }
    for (const auto& rel_path : relative_paths) {
        auto handle = next_file_handle_;
        next_file_handle_++;
        file_handles_by_rel_path_[rel_path] = handle;
        file_sizes_by_handle_[handle] = std::filesystem::file_size(base_path / rel_path);
        rel_paths_by_handle_[handle] = rel_path;
        // Does not open file
    }
}

inline bool FilesManifestDummy::is_started() const {
    return true;
    // A "real" implementation would check a flag in memory
    // Though I THINK that after the ctor has completed, it would always be true?
}

inline bool FilesManifestDummy::is_finished_sealed() const {
    return finished_sealed_;
}

inline FilesManifestDummy::FileHandle FilesManifestDummy::create_new_file(const std::filesystem::path& relative_path) {
    assert((!finished_sealed_) && "create_new_file(): Cannot create new file after finish_and_seal()");
    if (std::filesystem::exists(base_path_ / relative_path)) {
        throw std::runtime_error("create_new_file(): File already exists");
    }

    const auto handle = next_file_handle_;
    next_file_handle_++;

    file_handles_by_rel_path_[relative_path] = handle;
    rel_paths_by_handle_[handle] = relative_path;
    file_sizes_by_handle_[handle] = 0;
    auto path = base_path_ / relative_path;
    auto parent_path = path.parent_path();
    std::filesystem::create_directories(parent_path);
    open_files_by_handle_[handle] = std::ofstream{base_path_ / relative_path,
        std::ios::out | std::ios::app | std::ios::binary};
    return handle;
}

inline FilesManifestDummy::~FilesManifestDummy() {
    try {
        flush();
        open_files_by_handle_.clear();  // ofstream dtors close files
    } catch (...) {
        // Suppress exceptions in destructor to prevent std::terminate()
    }
}

inline FilesManifestDummy::FileHandle FilesManifestDummy::get_file_handle(const std::filesystem::path& relative_path) {
    if (!file_handles_by_rel_path_.contains(relative_path)) {
        return InvalidFileHandle;
    }
    return file_handles_by_rel_path_[relative_path];
}

inline size_t FilesManifestDummy::get_file_size(FileHandle handle) {
    if (!file_sizes_by_handle_.contains(handle)) {
        throw std::runtime_error("get_file_size(): Unknown handle");
    }
    return file_sizes_by_handle_[handle];
}

inline void FilesManifestDummy::open_file(FileHandle handle) {
    if (open_files_by_handle_.contains(handle)) {
        throw std::runtime_error("open_file(): File is already open");
    }
    if (!file_sizes_by_handle_.contains(handle)) {
        throw std::runtime_error("open_file(): Unknown handle");
    }
    open_files_by_handle_[handle] = std::ofstream{base_path_ / rel_paths_by_handle_[handle],
        std::ios::out | std::ios::app | std::ios::binary};
}

inline void FilesManifestDummy::append_to_file(const FileHandle handle, std::span<const uint8_t> bytes) {
    assert((!finished_sealed_) && "append_to_file(): Cannot append to file after finish_and_seal()");
    if (!open_files_by_handle_.contains(handle)) {
        throw std::runtime_error("append_to_file(): File is not open");
    }

    open_files_by_handle_[handle].write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file_sizes_by_handle_[handle] += bytes.size();
}

inline void FilesManifestDummy::close_file(const FileHandle handle) {
    // ofstream destructor will close the file
    open_files_by_handle_.erase(handle);
}

inline void FilesManifestDummy::commit() {
    // A "real" implementation would write the latest info to manifest.bin and
    // CONSIDER whether to flush() based on time since last flush
}
inline void FilesManifestDummy::flush() {
    for (auto &file: open_files_by_handle_ | std::views::values) {
        file.flush();
    }
    // A "real" implementation would also flush manifest.bin
}
inline void FilesManifestDummy::finish_and_seal() {
    flush();
    open_files_by_handle_.clear(); // Close all active stream handles
    finished_sealed_ = true;
    // A "real" implementation would create and close an empty "manifest_sealed.bin" file
    // A "real" implementation would THEN delete "manifest.bin"
}

}