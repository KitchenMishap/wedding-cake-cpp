#pragma once

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <span>

namespace files_manifest {

/// Requires that type T defines a FileHandle type and an InvalidFileHandle constant.
template<typename T>
concept HasFileHandleType = requires {
    typename T::FileHandle;
    { T::InvalidFileHandle } -> std::convertible_to<typename T::FileHandle>;
};

/// Enforces the read-only query interface of a manifest system.
template<typename T>
concept ManifestQueryable = HasFileHandleType<T> && requires(const T const_manifest,
                                                             typename T::FileHandle handle,
                                                             const std::filesystem::path& rel_path) {
    { const_manifest.is_started() } -> std::same_as<bool>;
    { const_manifest.is_finished_sealed() } -> std::same_as<bool>;
};

/// Enforces the full lifecycle and file operation interface of a manifest system.
template<typename T>
concept ManifestLike = ManifestQueryable<T> && requires(T manifest,
                                                        typename T::FileHandle handle,
                                                        const std::filesystem::path& rel_path,
                                                        std::span<const uint8_t> bytes) {
    // File handle management
    { manifest.create_new_file(rel_path) } -> std::same_as<typename T::FileHandle>;
    { manifest.get_file_handle(rel_path) } -> std::same_as<typename T::FileHandle>;
    { manifest.get_file_size(handle) }     -> std::same_as<size_t>;

    // Stream operations
    { manifest.open_file(handle) };
    { manifest.append_to_file(handle, bytes) };
    { manifest.close_file(handle) };

    // Lifecycle & transactional controls
    { manifest.commit() };
    { manifest.flush() };
    { manifest.finish_and_seal() };
};

} // namespace files_manifest