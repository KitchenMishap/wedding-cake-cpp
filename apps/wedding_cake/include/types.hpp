#pragma once

#include <vector>
#include <cstdint>
#include <span>
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace wedding_cake {

constexpr uint64_t PI_NO_MATCH_VAL = std::numeric_limits<uint64_t>::max();

// -----------------------------------------------------------------------------
// Tag-based Strong Type Base
// -----------------------------------------------------------------------------
template <typename Tag, typename T = uint64_t>
struct StrongIndex {
    T value{PI_NO_MATCH_VAL};

    constexpr StrongIndex() noexcept = default;
    explicit constexpr StrongIndex(T v) noexcept : value(v) {}

    [[nodiscard]] constexpr bool is_no_match() const noexcept {
        return value == PI_NO_MATCH_VAL;
    }

    [[nodiscard]] constexpr T to_int() const noexcept { return value; }

    constexpr auto operator<=>(const StrongIndex&) const noexcept = default;
};

// -----------------------------------------------------------------------------
// Distinct Strong Types
// -----------------------------------------------------------------------------
struct LocalPiTag {};
struct GlobalPiTag {};
struct PiOffsetTag {};

using LocalPi  = StrongIndex<LocalPiTag>;
using GlobalPi = StrongIndex<GlobalPiTag>;

struct PiOffset : StrongIndex<PiOffsetTag> {
    using StrongIndex::StrongIndex;
    constexpr PiOffset() noexcept : StrongIndex(0) {} // Default offset is 0
};

// Standard Sentinels
constexpr LocalPi  LOCAL_PI_NO_MATCH{PI_NO_MATCH_VAL};
constexpr GlobalPi GLOBAL_PI_NO_MATCH{PI_NO_MATCH_VAL};

// -----------------------------------------------------------------------------
// Strongly-Typed Translation Operators
// -----------------------------------------------------------------------------

// LocalPi + PiOffset -> GlobalPi
[[nodiscard]] constexpr GlobalPi operator+(LocalPi local, PiOffset offset) noexcept {
    if (local.is_no_match()) return GLOBAL_PI_NO_MATCH;
    return GlobalPi(local.value + offset.value);
}

// GlobalPi - PiOffset -> LocalPi
[[nodiscard]] constexpr LocalPi operator-(GlobalPi global, PiOffset offset) noexcept {
    if (global.is_no_match() || global.value < offset.value) return LOCAL_PI_NO_MATCH;
    return LocalPi(global.value - offset.value);
}

// GlobalPi - GlobalPi -> PiOffset
[[nodiscard]] constexpr PiOffset operator-(GlobalPi a, GlobalPi b) noexcept {
    if (a.is_no_match() || b.is_no_match() || a.value < b.value) return PiOffset(0);
    return PiOffset(a.value - b.value);
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

struct HashLocalPresentation {
    std::vector<uint8_t> hash_bytes;
    LocalPi presentation_index;

    [[nodiscard]] HashView view() const noexcept {
        return HashView(hash_bytes);
    }
};

struct HashGlobalPresentation {
    std::vector<uint8_t> hash_bytes;
    GlobalPi presentation_index;

    [[nodiscard]] HashView view() const noexcept {
        return HashView(hash_bytes);
    }
};

} // namespace wedding_cake