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
template <typename T>
concept SubStore = requires(const T store, GlobalPi gpi, HashView h_view) {
    // Forward lookup takes GlobalPi and returns a optional hash byte-vector
    { store.ForwardLookup(gpi) } -> std::same_as<std::optional<std::vector<uint8_t>>>;

    // Base offset and total count of presentation indices covered
    { store.Offset() } -> std::same_as<PiOffset>;
    { store.Count() }  -> std::convertible_to<uint64_t>;

    // Candidate lookup returns a strong LocalPi candidate or LOCAL_PI_NO_MATCH
    { store.ReverseLookupCandidate(h_view) } -> std::same_as<LocalPi>;
};

// ============================================================================
// 2. DONUT CONCEPT
// Self-contained SubStore backed by on-disk tail-bit & prefix files.
// ============================================================================
template <typename T>
concept DonutConcept = SubStore<T> && requires(T donut, HashView h_view, LocalPi lpi) {
    // Internal candidate verification against tail-bits and prefix files
    { donut.VerifyCandidate(lpi, h_view) } -> std::same_as<bool>;
    
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

template <typename T>
concept CakeConcept = SubStore<T> && requires(T cake, HashView h_view, GlobalPi gpi, BakeCompletionCallback cb) {
    // Direct insertion (Cake manages internal active tier boundaries automatically)
    { cake.InsertHashNoDup(h_view, gpi) } -> std::same_as<bool>;

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
template <typename T>
concept HashStoreConcept = requires(T hs, HashView h_view, GlobalPi gpi) {
    // Insert with duplicate rejection (returns existing primary GlobalPi or newly added gpi)
    { hs.InsertHashDupDetect(h_view, gpi) } -> std::same_as<GlobalPi>;

    // Full forward & reverse lookups
    { hs.ForwardLookup(gpi) } -> std::same_as<std::optional<std::vector<uint8_t>>>;
    { hs.ReverseLookup(h_view) } -> std::same_as<GlobalPi>;
};

} // namespace wedding_cake