#pragma once

#include <concepts>
#include <span>
#include <type_traits>
#include <utility>

namespace evolving_disk_state::concepts {

    // -----------------------------------------------------------------------------
    // 1. BufferInput Concept (Upstream Tier Interface)
    // -----------------------------------------------------------------------------
    // The upstream producer feeds move-only items into the buffer room.
    template <typename Buffer, typename T>
    concept BufferInput = requires(Buffer& buffer, T&& item) {
        // Feed must accept an rvalue move parameter and block if full.
        { buffer.Feed(std::move(item)) } -> std::same_as<void>;
    };

    // -----------------------------------------------------------------------------
    // 2. BufferOutput Concept (Downstream Tier Interface)
    // -----------------------------------------------------------------------------
    // The downstream consumer inspects items via a scoped span lambda and discards them.
    template <typename Buffer, typename T>
    concept BufferOutput = requires(Buffer& buffer) {
        // 1. Access via with_contents lambda passing std::span<const T>
        buffer.with_contents_when_filled([](std::span<const T> items) {
        });

        // 2. Discard blocks until all active readers exit, then purges buffer & disk
        { buffer.Discard() } -> std::same_as<void>;
    };

    // -----------------------------------------------------------------------------
    // 2. BufferReadable Concept (Asynchronous Read Interface)
    // -----------------------------------------------------------------------------
    // The buffer can be read whilst BufferInput and BufferOutput activities are ongoing
    template <typename Buffer, typename T>
    concept BufferReadable = requires(Buffer& buffer) {
        // 1. Access via with_current_contents lambda passing std::span<const T>
        buffer.with_current_contents([](std::span<const T> items) {
        });
    };

} // namespace evolving_disk_state::concepts