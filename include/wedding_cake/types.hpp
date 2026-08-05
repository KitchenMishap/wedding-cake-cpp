#pragma once

#include <vector>
#include <cstdint>
#include <span>
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace wedding_cake {

// Forward declarations
struct PiOffset;
struct GlobalPi;

// Sentinel values
constexpr uint64_t PI_NO_MATCH_VAL = std::numeric_limits<uint64_t>::max();

// -----------------------------------------------------------------------------
// Strong Type Wrappers for Presentation Indices
// -----------------------------------------------------------------------------

// Local Presentation Index (local to a ShallowTree / leaf group)
struct LocalPi {
    uint64_t value{PI_NO_MATCH_VAL};

    constexpr LocalPi() noexcept = default;
    explicit constexpr LocalPi(uint64_t v) noexcept : value(v) {}

    [[nodiscard]] constexpr bool is_no_match() const noexcept {
        return value == PI_NO_MATCH_VAL;
    }

    constexpr bool operator==(const LocalPi& other) const noexcept = default;
    constexpr bool operator!=(const LocalPi& other) const noexcept = default;

    // Translation helper
    [[nodiscard]] GlobalPi to_global(PiOffset offset) const noexcept;
    [[nodiscard]] uint64_t to_int() const noexcept;
};

// Global Presentation Index (global position across the entire dataset)
struct GlobalPi {
    uint64_t value{PI_NO_MATCH_VAL};

    constexpr GlobalPi() noexcept = default;
    explicit constexpr GlobalPi(uint64_t v) noexcept : value(v) {}

    [[nodiscard]] constexpr bool is_no_match() const noexcept {
        return value == PI_NO_MATCH_VAL;
    }

    constexpr bool operator==(const GlobalPi& other) const noexcept = default;
    constexpr bool operator!=(const GlobalPi& other) const noexcept = default;

    // Translation helper
    [[nodiscard]] LocalPi to_local(PiOffset offset) const noexcept;
    [[nodiscard]] uint64_t to_int() const noexcept;
};

// PiOffset (Base offset used to translate Local <-> Global)
struct PiOffset {
    uint64_t value{0};

    constexpr PiOffset() noexcept = default;
    explicit constexpr PiOffset(uint64_t v) noexcept : value(v) {}

    constexpr bool operator==(const PiOffset& other) const noexcept = default;
    [[nodiscard]] uint64_t to_int() const noexcept;
};

// Standard Sentinels
constexpr LocalPi LOCAL_PI_NO_MATCH{PI_NO_MATCH_VAL};
constexpr GlobalPi GLOBAL_PI_NO_MATCH{PI_NO_MATCH_VAL};

// Inline translation implementations
inline GlobalPi LocalPi::to_global(PiOffset offset) const noexcept {
    if (is_no_match()) {
        return GLOBAL_PI_NO_MATCH;
    }
    return GlobalPi(value + offset.value);
}
inline uint64_t LocalPi::to_int() const noexcept {
    return value;
}

inline LocalPi GlobalPi::to_local(PiOffset offset) const noexcept {
    if (is_no_match() || value < offset.value) {
        return LOCAL_PI_NO_MATCH;
    }
    return LocalPi(value - offset.value);
}
inline uint64_t GlobalPi::to_int() const noexcept {
    return value;
}

// -----------------------------------------------------------------------------
// Hash Structures
// -----------------------------------------------------------------------------

class HashView {
public:
    constexpr HashView() noexcept = default;

    template <typename Container>
    requires requires(const Container& c) { std::span<const uint8_t>(c); }
    constexpr HashView(const Container& container) : bytes_(container) {}

    constexpr HashView(const uint8_t* data, size_t size) : bytes_(data, size) {}

    [[nodiscard]] constexpr std::span<const uint8_t> bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return bytes_.empty(); }

    [[nodiscard]] constexpr uint8_t operator[](size_t index) const {
        return bytes_[index];
    }

    constexpr bool operator==(const HashView& other) const noexcept {
        if (bytes_.size() != other.bytes_.size()) return false;
        return std::equal(bytes_.begin(), bytes_.end(), other.bytes_.begin());
    }

private:
    std::span<const uint8_t> bytes_;
};

struct HashPresentation {
    std::vector<uint8_t> hash_bytes;
    LocalPi presentation_index;

    [[nodiscard]] HashView view() const noexcept {
        return HashView(hash_bytes);
    }
};

} // namespace wedding_cake