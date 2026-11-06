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

using LocalPi  = StrongIndex<LocalPiTag>;
using GlobalPi = StrongIndex<GlobalPiTag>;

// Standard Sentinels
constexpr LocalPi  LOCAL_PI_NO_MATCH{PI_NO_MATCH_VAL};
constexpr GlobalPi GLOBAL_PI_NO_MATCH{PI_NO_MATCH_VAL};

// -----------------------------------------------------------------------------
// Strongly-Typed Translation Operators (LocalPi <-> GlobalPi)
// -----------------------------------------------------------------------------

// LocalPi + GlobalPi (base offset) -> GlobalPi
[[nodiscard]] constexpr GlobalPi operator+(LocalPi local, GlobalPi base_offset) noexcept {
    if (local.is_no_match() || base_offset.is_no_match()) return GLOBAL_PI_NO_MATCH;
    return GlobalPi(local.to_int() + base_offset.to_int());
}

[[nodiscard]] constexpr GlobalPi operator+(GlobalPi base_offset, LocalPi local) noexcept {
    return local + base_offset;
}

// GlobalPi - GlobalPi (base offset) -> LocalPi
[[nodiscard]] constexpr LocalPi operator-(GlobalPi target, GlobalPi base_offset) noexcept {
    if (target.is_no_match() || base_offset.is_no_match() || target.to_int() < base_offset.to_int()) {
        return LOCAL_PI_NO_MATCH;
    }
    return LocalPi(target.to_int() - base_offset.to_int());
}

// GlobalPi - LocalPi -> GlobalPi
[[nodiscard]] constexpr GlobalPi operator-(GlobalPi target, LocalPi local) noexcept {
    if (target.is_no_match() || local.is_no_match() || target.to_int() < local.to_int()) {
        return GLOBAL_PI_NO_MATCH;
    }
    return GlobalPi(target.to_int() - local.to_int());
}

// GlobalPi - GlobalPi -> uint64_t (Distance / Delta count)
[[nodiscard]] constexpr uint64_t distance(GlobalPi high, GlobalPi low) noexcept {
    if (high.is_no_match() || low.is_no_match() || high.to_int() < low.to_int()) return 0;
    return high.to_int() - low.to_int();
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

template<size_t HashBytesCount>
struct HashBytes {
    std::array<uint8_t, HashBytesCount> bytes;
    HashBytes() = default;
    HashBytes(std::array<uint8_t, HashBytesCount> bytes) : bytes(bytes) {}
    auto operator<=>(const HashBytes&) const = default;
};

template<size_t HashBytesCount>
bool operator<(const HashBytes<HashBytesCount>& lhs, const HashBytes<HashBytesCount>& rhs) {
    return std::lexicographical_compare(lhs.bytes.begin(), lhs.bytes.end(), rhs.bytes.begin(), rhs.bytes.end());
}

template<size_t HashBytesCount>
struct HashLocalPresentation {
    HashBytes<HashBytesCount> hash;
    LocalPi presentation_index;

    [[nodiscard]] HashView view() const noexcept {
        return HashView(hash.bytes);
    }
};

template<size_t HashBytesCount>
struct HashGlobalPresentation {
    HashBytes<HashBytesCount> hash;
    GlobalPi presentation_index;

    [[nodiscard]] HashView view() const noexcept {
        return HashView(hash.bytes);
    }
};

} // namespace wedding_cake