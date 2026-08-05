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

#include "types.hpp"

namespace wedding_cake {

// Forward declarations
struct Node;

// -----------------------------------------------------------------------------
// LeafNode & SlotsNode Definition
// -----------------------------------------------------------------------------
struct LeafNode {
    std::vector<uint8_t> reassurance_hash_bytes;
    LocalPi presentation_index{LOCAL_PI_NO_MATCH};
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
// Partition Helper Functions
// -----------------------------------------------------------------------------

// Strategy A: Standard Sort O(N log N)
std::array<size_t, 256> sort_partition_by_byte(
    std::span<HashPresentation> input,
    size_t byte_index)
{
    std::ranges::sort(input, {}, [byte_index](const HashPresentation& hp) {
        return hp.hash_bytes[byte_index];
    });

    std::array<size_t, 256> counts{};
    for (const auto& item : input) {
        counts[item.hash_bytes[byte_index]]++;
    }
    return counts;
}

// Strategy B: Linear Bucket Partition O(N)
std::array<size_t, 256> bucket_partition_by_byte(
    std::span<HashPresentation> input,
    size_t byte_index,
    std::vector<HashPresentation>& scratch_buffer)
{
    std::array<size_t, 256> counts{};
    for (const auto& item : input) {
        counts[item.hash_bytes[byte_index]]++;
    }

    std::array<size_t, 256> offsets{};
    offsets[0] = 0;
    for (size_t i = 1; i < 256; ++i) {
        offsets[i] = offsets[i - 1] + counts[i - 1];
    }

    std::array<size_t, 256> running_offsets = offsets;
    for (const auto& item : input) {
        uint8_t byte_val = item.hash_bytes[byte_index];
        scratch_buffer[running_offsets[byte_val]++] = item;
    }

    std::copy_n(scratch_buffer.begin(), input.size(), input.begin());

    return counts;
}

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

        // Bitmask tracking available bytes (bit b = 1 means unused)
        // Up to 64 bytes fit naturally in uint64_t
        uint64_t unused_mask = (total_hash_bytes_ == 64) ? ~0ULL : ((1ULL << total_hash_bytes_) - 1);

        // Clear bits for handled LSB tail bytes
        for (size_t b = 0; b < handled_bytes; ++b) {
            unused_mask &= ~(1ULL << b);
        }

        // Single hash edge-case
        if (input.size() == 1) {
            root_slot_.next_node = make_leaf_node(input[0], unused_mask, handled_bytes);
            return;
        }

        // Single buffer allocation reused throughout recursive steps
        std::vector<HashPresentation> scratch_buffer(input.size());

        root_slot_.next_node = recurse_generate_node(input, unused_mask, handled_bytes, scratch_buffer);
    }

    // Fast O(1) Shallow-Trie Lookup
    // Result is only a candidate as not all bytes of hash are examined. False positives are possible.
    // The result is however unique within the tree.
    // The result is "reassured" by a few spare bytes of checking.
    [[nodiscard]] LocalPi lookup_hash_unique_reassured_candidate(HashView target_hash) const {
        if (target_hash.size() != total_hash_bytes_ || root_slot_.is_empty()) {
            return LOCAL_PI_NO_MATCH;
        }

        // Calculate whole bytes touched/handled by the LS tail bits
        size_t handled_bytes = (ls_tail_bits_count_ + 7) / 8;

        // Bitmask tracking available bytes (bit b = 1 means unused)
        // Up to 64 bytes fit naturally in uint64_t
        uint64_t unused_mask = (total_hash_bytes_ == 64) ? ~0ULL : ((1ULL << total_hash_bytes_) - 1);

        // Clear bits for handled LSB tail bytes
        for (size_t b = 0; b < handled_bytes; ++b) {
            unused_mask &= ~(1ULL << b);
        }

        const Node* current = root_slot_.next_node.get();

        while (current != nullptr) {
            if (current->is_leaf()) {
                const auto& leaf = *current->leaf;

                // We must check the reassurance bytes. These are (in order) the first few bytes not yet examined
                size_t index = 0;
                for (size_t byteIndex = 0; byteIndex < total_hash_bytes_; byteIndex++) {
                    if ((unused_mask & (1ULL << byteIndex)) == 0) {
                        continue; // Skip used byte
                    }
                    if ( target_hash[byteIndex] != leaf.reassurance_hash_bytes[index] ) {
                        return LOCAL_PI_NO_MATCH;   // Reassurance byte mismatch
                    }
                    index++;
                    if ( index >= reassurance_bytes_count_ ) {
                        return leaf.presentation_index;     // All reassurance bytes matched
                    }
                }
                // Examined all bytes of hash!
                return leaf.presentation_index;
            }

            // Internal SlotsNode: examine target hash at hash_byte_index
            size_t byte_idx = current->slots->hash_byte_index;
            uint8_t byte_val = target_hash[byte_idx];
            // Clear bit for consumed byte index
            unused_mask &= ~(1ULL << byte_idx);

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
    std::unique_ptr<Node> make_leaf_node(const HashPresentation& hp, uint64_t unused_mask, size_t level_depth) {
        auto leaf = std::make_unique<LeafNode>();
        leaf->presentation_index = hp.presentation_index;

        // Reassurance bytes are, in order, the bytes that have not yet been examined
        leaf->reassurance_hash_bytes.resize(reassurance_bytes_count_);
        size_t index = 0;
        for (size_t b = 0; b < total_hash_bytes_; ++b) {
            if ( (unused_mask & (1ULL << b)) == 0 ) {
                continue;   // Byte b has already been used, can't be used for reassurance
            }
            leaf->reassurance_hash_bytes[index] = hp.hash_bytes[b];
            index++;
            if ( index >= reassurance_bytes_count_ ) {
                break;
            }
        }
        // There may be space for more. Zero them out to give deterministic data
        for ( ;index < reassurance_bytes_count_; ++index) {
            leaf->reassurance_hash_bytes[index] = 0;
        }

        auto node = std::make_unique<Node>();
        node->level_byte_depth = level_depth;
        node->leaf = std::move(leaf);
        return node;
    }

    // Recursive entropy-based partitioning
    std::unique_ptr<Node> recurse_generate_node(
          std::span<HashPresentation> input,
          uint64_t unused_mask,
          size_t level_depth,
          std::vector<HashPresentation>& scratch_buffer)
    {
        if (input.size() < 2) {
            throw std::logic_error("recurse_generate_node called with fewer than 2 elements");
        }

        // Safety check: if mask is 0, all bytes are used and we still have >= 2 identical items
        if (unused_mask == 0) {
            throw std::runtime_error("Duplicate hashes detected during tree generation!");
        }

        // 1. Find the byte index with maximum Shannon Entropy
        double max_entropy = std::numeric_limits<double>::lowest(); // Safe for positive or negative metrics
        size_t best_byte_index = 0;

        for (size_t b = 0; b < total_hash_bytes_; ++b) {
            if ((unused_mask & (1ULL << b)) != 0) {
                double entropy = calculate_byte_entropy(input, b);
                if (entropy > max_entropy) {
                    max_entropy = entropy;
                    best_byte_index = b;
                }
            }
        }

        // Mark chosen byte as consumed
        unused_mask &= ~(1ULL << best_byte_index);

        // 2. In-place sort / partition
        std::array<size_t, 256> counts{};

        // Previous slower way
        //counts = sort_partition_by_byte(input, best_byte_index);

        // Newer faster way (twice as fast it seems)
        counts = bucket_partition_by_byte(input, best_byte_index, scratch_buffer);

        auto node = std::make_unique<Node>();
        node->level_byte_depth = level_depth;
        node->slots = std::make_unique<SlotsNode>();
        node->slots->hash_byte_index = best_byte_index;

        // 3. Partition across 256 sub-slots using bucket count boundaries
        size_t start_index = 0;
        for (uint16_t byte_val = 0; byte_val < 256; ++byte_val) {
            size_t count = counts[byte_val];

            if (count == 1) {
                // Leaf Node
                node->slots->slots[byte_val].next_node =
                    make_leaf_node(input[start_index], unused_mask, level_depth + 1);
            } else if (count > 1) {
                // Subtree Node
                auto sub_span = input.subspan(start_index, count);
                node->slots->slots[byte_val].next_node =
                    recurse_generate_node(sub_span, unused_mask, level_depth + 1, scratch_buffer);
            }

            start_index += count;
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

//      Old slow way (actual entropy)
//      for (size_t count : counts) {
//          if (count > 0) {
//              double prob = static_cast<double>(count) / total;
//              entropy -= prob * std::log2(prob);
//          }
//      }
//      return entropy;

        // New fast way avoiding logs
        // Maximizing Shannon Entropy -sum(p log p) is equivalent to
        // minimizing collision probability sum(count^2)
        uint64_t sum_sq = 0;
        for (size_t count : counts) {
            sum_sq += (count) * count;
        }

        // Lower sum of squares = higher entropy
        return -static_cast<double>(sum_sq);

    }
};

} // namespace wedding_cake