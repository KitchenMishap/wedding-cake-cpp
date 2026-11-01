#pragma once

#include "tier.hpp"

#include <concepts>
#include <filesystem>
#include <utility>

namespace evolving_disk_state {

// Move-Only Concept
// Objects derived from Evolver are TypeStates (each possible state is represented by a separate type)
// As such, TypeStates (and hence derived classes of Evolver) must be move-only.
template <typename T>
concept MoveOnly = !std::is_abstract_v<T>
                && std::is_move_constructible_v<T>
                && !std::is_copy_constructible_v<T>
                && !std::is_copy_assignable_v<T>;

}