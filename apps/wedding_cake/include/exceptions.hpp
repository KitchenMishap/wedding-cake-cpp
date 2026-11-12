#pragma once

#include <stdexcept>
#include <string>
#include <filesystem>
#include <iostream>

namespace wedding_cake {

/// Base exception for all wedding_cake storage and format violations
class DatabaseCorruptException : public std::runtime_error {
public:
    explicit DatabaseCorruptException(const std::string& message)
        : std::runtime_error(message) {}

    DatabaseCorruptException(const std::filesystem::path& file_path, const std::string& details)
        : std::runtime_error("Corrupt file [" + file_path.string() + "]: " + details)
        , file_path_(file_path) {}

    [[nodiscard]] const std::filesystem::path& file_path() const noexcept {
        return file_path_;
    }

private:
    std::filesystem::path file_path_;
};

/// Specific exception for header/magic byte or record alignment mismatches
class InvalidFileFormatException : public DatabaseCorruptException {
public:
    using DatabaseCorruptException::DatabaseCorruptException;
};

inline void log_ungraceful_shutdown_recovered(std::filesystem::path file_path, std::string details) {
    std::cerr << "Ungraceful shutdown recovered from file [" << file_path.string() << "]: " << details << std::endl;
}

} // namespace wedding_cake