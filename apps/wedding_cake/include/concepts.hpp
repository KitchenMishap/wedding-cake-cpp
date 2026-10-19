#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <vector>

#include "types.hpp"

namespace wedding_cake {

// ============================================================================
// 1. SUBSTORE CONCEPT
// Read-only baseline query interface implemented by Tiers, Donuts, and Cakes.
// ============================================================================
template <typename T, size_t HashBytesCount>
concept SubStoreConcept = requires(const T store, GlobalPi gpi, HashBytes<HashBytesCount> hash) {
    // Forward lookup takes GlobalPi and returns a optional hash byte-vector
    { store.ForwardLookup(gpi) } -> std::same_as<std::optional<HashBytes<HashBytesCount>>>;

    // Base offset and total count of presentation indices covered
    { store.Offset() } -> std::same_as<PiOffset>;
    { store.Count() }  -> std::convertible_to<uint64_t>;

    // Candidate lookup returns a strong LocalPi candidate or LOCAL_PI_NO_MATCH
    { store.ReverseLookupCandidate(hash) } -> std::same_as<GlobalPi>;
};

// ============================================================================
// 2. DONUT CONCEPT
// Self-contained SubStore backed by on-disk tail-bit & prefix files.
// ============================================================================
template <typename T, size_t HashByteCount>
concept DonutConcept = SubStoreConcept<T, HashByteCount> && requires(T donut, HashBytes<HashByteCount> hash, LocalPi lpi) {
    // Internal candidate verification against tail-bits and prefix files
    { donut.VerifyCandidate(lpi, hash) } -> std::same_as<bool>;
    
    // Disk state check
    { donut.IsIced() } -> std::same_as<bool>;
};

// ============================================================================
// 3. DUP STORE CONCEPT
// Tracks primary first-seen GlobalPi vs duplicate twin-sister GlobalPis.
// ============================================================================
template <typename T>
concept DupStoreConcept = requires(T dup_store, GlobalPi primary, GlobalPi duplicate) {
    { dup_store.Append(primary, duplicate) } -> std::same_as<void>;
    { dup_store.PrimaryFor(duplicate) }      -> std::same_as<GlobalPi>;
    { dup_store.Others(primary) }            -> std::same_as<std::vector<GlobalPi>>;
};

// ============================================================================
// CAKE CONCEPT
// Manages non-duplicate writes, background compaction, and deterministic state.
// ============================================================================
// Signature for Baker completion notifications:
// Params: (completed_tier_level, newly_created_donut_path)
using BakeCompletionCallback = std::function<void(uint32_t, const char*)>;

template <typename T, size_t HashBytesCount>
concept CakeConcept = SubStoreConcept<T, HashBytesCount> && requires(T cake, HashBytes<HashBytesCount> hash, GlobalPi gpi, BakeCompletionCallback cb) {
    // Direct insertion (Cake manages internal active tier boundaries automatically)
    { cake.InsertHashNoDup(hash, gpi) } -> std::same_as<bool>;

    // ------------------------------------------------------------------------
    // Baker Process Lifecycle & Control
    // ------------------------------------------------------------------------

    // Resumes interrupted or pending bake jobs from disk progress files
    { cake.ResumeBaking() } -> std::same_as<void>;

    // Immediately interrupts/kills active baker processes
    { cake.BrutalStopBaking() } -> std::same_as<void>;

    // Registers a callback to be notified when a background bake job completes
    { cake.OnBakeCompleted(cb) } -> std::same_as<void>;

    // ------------------------------------------------------------------------
    // Determinism & Synchronization
    // ------------------------------------------------------------------------

    // Blocks until all pending tiers are baked and iced, guaranteeing deterministic state
    { cake.AwaitBakingCompletion() } -> std::same_as<void>;

    // Checks if the database is currently in a fully deterministic on-disk state
    { cake.IsFullyBaked() } -> std::same_as<bool>;
};

// ============================================================================
// 5. HASHSTORE CONCEPT
// Top-level user interface coordinating Main Cake, Duplicates Cake, & DupStore.
// ============================================================================
template <typename T, size_t HashBytesCount>
concept HashStoreConcept = requires(T hs, HashBytes<HashBytesCount> hash, GlobalPi gpi) {
    // Insert with duplicate rejection (returns existing primary GlobalPi or newly added gpi)
    { hs.InsertHashDupDetect(hash, gpi) } -> std::same_as<GlobalPi>;

    // Full forward & reverse lookups
    { hs.ForwardLookup(gpi) } -> std::same_as<std::optional<std::vector<uint8_t>>>;
    { hs.ReverseLookup(hash) } -> std::same_as<GlobalPi>;
};

} // namespace wedding_cake