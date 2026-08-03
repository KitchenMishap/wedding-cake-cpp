#pragma once

#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

#include "HashTypes.hpp"

namespace wedding_cake {

// Forward declarations
struct Node;

// -----------------------------------------------------------------------------
// LeafNode & SlotsNode Definition
// -----------------------------------------------------------------------------
struct LeafNode {
    std::vector<uint8_t> reassurance_hash_bytes;
    LocalPi presentation_index{LOCAL_PI_NO_MATCH};
    std::vector<uint8_t> full_hash;
};

struct Slot {
    std::unique_ptr<Node> next_node;

    [[nodiscard]] bool is_empty() const noexcept {
        return next_node == nullptr;
    }
};

struct SlotsNode {
    size_t hash_byte_index{0};          // Byte index chosen for maximum entropy split
    std::array<Slot, 256> slots{};      // 256-way byte branch
};

// -----------------------------------------------------------------------------
// Node Container (Leaf OR Slots)
// -----------------------------------------------------------------------------
struct Node {
    size_t level_byte_depth{0};         // Measured in whole bytes
    std::unique_ptr<LeafNode> leaf;
    std::unique_ptr<SlotsNode> slots;

    [[nodiscard]] bool is_leaf() const noexcept {
        return leaf != nullptr;
    }
};

// -----------------------------------------------------------------------------
// ShallowTree Engine Class
// -----------------------------------------------------------------------------
class ShallowTree {
public:
    ShallowTree() = default;

    // Constructs the tree directly from a mutable span of HashPresentation.
    // Standard C++ idiom: Mutates the input span in-place during recursive partitioning.
    ShallowTree(std::span<HashPresentation> input,
                size_t ls_tail_bits_count,
                size_t total_hash_bytes,
                size_t reassurance_bytes_count)
        : total_hash_bytes_(total_hash_bytes),
          ls_tail_bits_count_(ls_tail_bits_count),
          reassurance_bytes_count_(reassurance_bytes_count),
          hash_count_(input.size())
    {
        // Calculate whole bytes touched/handled by the LS tail bits
        size_t handled_bytes = (ls_tail_bits_count + 7) / 8;

        if (total_hash_bytes_ < 1 || total_hash_bytes_ > 64) {
            throw std::invalid_argument("Supported hash size is 1 to 64 bytes (up to 512-bit)");
        }
        if (handled_bytes > total_hash_bytes_) {
            throw std::invalid_argument("ls_tail_bits covers more bytes than the total hash size");
        }

        if (input.empty()) {
            return; // Empty tree: root_slot_ remains empty
        }

        // Single hash edge-case
        if (input.size() == 1) {
            root_slot_.next_node = make_leaf_node(input[0], handled_bytes);
            return;
        }

        // Bitset/Vector tracking which byte indices are available for partitioning
        std::vector<bool> unused_bytes(total_hash_bytes_, true);
        // Exclude the LSB tail bytes already handled by lower radix levels
        for (size_t b = 0; b < handled_bytes; ++b) {
            unused_bytes[b] = false;
        }

        root_slot_.next_node = recurse_generate_node(input, unused_bytes, handled_bytes);
    }

    // Fast O(1) Shallow-Trie Lookup
    [[nodiscard]] LocalPi lookup_hash(HashView target_hash) const {
        if (target_hash.size() != total_hash_bytes_ || root_slot_.is_empty()) {
            return LOCAL_PI_NO_MATCH;
        }

        const Node* current = root_slot_.next_node.get();

        while (current != nullptr) {
            if (current->is_leaf()) {
                const auto& leaf = *current->leaf;
                if (target_hash.bytes().size() == leaf.full_hash.size() &&
                    std::equal(leaf.full_hash.begin(), leaf.full_hash.end(), target_hash.bytes().begin())) {
                    return leaf.presentation_index;
                }
                return LOCAL_PI_NO_MATCH;
            }

            // Internal SlotsNode: examine target hash at hash_byte_index
            size_t byte_idx = current->slots->hash_byte_index;
            uint8_t byte_val = target_hash[byte_idx];

            const Slot& slot = current->slots->slots[byte_val];
            if (slot.is_empty()) {
                return LOCAL_PI_NO_MATCH;
            }

            current = slot.next_node.get();
        }

        return LOCAL_PI_NO_MATCH;
    }

    // Tree Visitors
    template <typename VisitorFunc>
    void visit_all_nodes(VisitorFunc&& visitor) const {
        if (root_slot_.is_empty()) return;

        std::vector<const Node*> stack;
        stack.push_back(root_slot_.next_node.get());

        while (!stack.empty()) {
            const Node* curr = stack.back();
            stack.pop_back();

            visitor(curr);

            if (!curr->is_leaf() && curr->slots) {
                for (size_t i = 0; i < 256; ++i) {
                    if (!curr->slots->slots[i].is_empty()) {
                        stack.push_back(curr->slots->slots[i].next_node.get());
                    }
                }
            }
        }
    }

    [[nodiscard]] size_t count_nodes() const {
        size_t count = 0;
        visit_all_nodes([&count](const Node*) { ++count; });
        return count;
    }

    [[nodiscard]] size_t hash_count() const noexcept { return hash_count_; }

private:
    size_t total_hash_bytes_{0};
    size_t ls_tail_bits_count_{0};
    size_t reassurance_bytes_count_{0};
    size_t hash_count_{0};
    Slot root_slot_;

    // Creates a single leaf node
    std::unique_ptr<Node> make_leaf_node(const HashPresentation& hp, size_t level_depth) {
        auto leaf = std::make_unique<LeafNode>();
        leaf->presentation_index = hp.presentation_index;
        leaf->full_hash = hp.hash_bytes;

        // Reassurance bytes sample remaining bytes
        size_t bytes_to_copy = std::min(reassurance_bytes_count_, hp.hash_bytes.size());
        leaf->reassurance_hash_bytes.assign(hp.hash_bytes.begin(), hp.hash_bytes.begin() + bytes_to_copy);

        auto node = std::make_unique<Node>();
        node->level_byte_depth = level_depth;
        node->leaf = std::move(leaf);
        return node;
    }

    // Recursive entropy-based partitioning
    std::unique_ptr<Node> recurse_generate_node(std::span<HashPresentation> input,
                                                std::vector<bool> unused_bytes,
                                                size_t level_depth)
    {
        if (input.size() < 2) {
            throw std::logic_error("recurse_generate_node called with fewer than 2 elements");
        }

        // 1. Find the byte index with maximum Shannon Entropy
        double max_entropy = -1.0;
        size_t best_byte_index = 0;

        for (size_t b = 0; b < total_hash_bytes_; ++b) {
            if (unused_bytes[b]) {
                double entropy = calculate_byte_entropy(input, b);
                if (entropy > max_entropy) {
                    max_entropy = entropy;
                    best_byte_index = b;
                }
            }
        }

        if (max_entropy <= 0.0) {
            throw std::runtime_error("Duplicate hashes detected during tree generation!");
        }

        // Mark chosen byte as consumed
        unused_bytes[best_byte_index] = false;

        // 2. In-place sort input span by the chosen byte value
        std::stable_sort(input.begin(), input.end(), [best_byte_index](const HashPresentation& a, const HashPresentation& b) {
            return a.hash_bytes[best_byte_index] < b.hash_bytes[best_byte_index];
        });

        auto node = std::make_unique<Node>();
        node->level_byte_depth = level_depth;
        node->slots = std::make_unique<SlotsNode>();
        node->slots->hash_byte_index = best_byte_index;

        // 3. Partition across 256 sub-slots
        size_t index = 0;
        for (uint16_t byte_val = 0; byte_val < 256; ++byte_val) {
            size_t start_index = index;

            while (index < input.size() && input[index].hash_bytes[best_byte_index] == byte_val) {
                index++;
            }

            size_t count = index - start_index;

            if (count == 1) {
                // Leaf Node
                node->slots->slots[byte_val].next_node = make_leaf_node(input[start_index], level_depth + 1);
            } else if (count > 1) {
                // Subtree Node
                auto sub_span = input.subspan(start_index, count);
                node->slots->slots[byte_val].next_node = recurse_generate_node(sub_span, unused_bytes, level_depth + 1);
            }
        }

        return node;
    }

    // Shannon Entropy Calculation for a single byte index across the input span
    static double calculate_byte_entropy(std::span<const HashPresentation> input, size_t byte_index) {
        std::array<size_t, 256> counts{};
        for (const auto& item : input) {
            counts[item.hash_bytes[byte_index]]++;
        }

        double total = static_cast<double>(input.size());
        double entropy = 0.0;

        for (size_t count : counts) {
            if (count > 0) {
                double prob = static_cast<double>(count) / total;
                entropy -= prob * std::log2(prob);
            }
        }

        return entropy;
    }
};

} // namespace wedding_cake