#include <iostream>
#include <random>

#include "../include/wedding_cake/shallow_tree.hpp"
#include "../apps/wedding_cake/include/types.hpp"

// Fixed-seed PRNG for deterministic, reproducible test runs
template <size_t hash_bytes>
class DeterministicHashGenerator {
    std::mt19937_64 rng_{42}; // Fixed seed 42
public:
    std::array<uint8_t, hash_bytes> make_random_hash() {
        std::array<uint8_t, hash_bytes> bytes;
        for (size_t i = 0; i < hash_bytes; ++i) {
            bytes[i] = static_cast<uint8_t>(rng_() & 0xFF);
        }
        return bytes;
    }
};

int main() {
    using namespace wedding_cake;

    constexpr size_t count = 64 * 65535;
    constexpr size_t ls_tail_bits = 8;
    constexpr size_t REASSURANCE_BYTES_COUNT = 2;
    constexpr size_t hash_bytes = 32;

    std::cout << "Generating test data...\n";
    DeterministicHashGenerator<hash_bytes> gen;
    std::vector<HashLocalPresentation<hash_bytes>> presentation_array;
    presentation_array.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        presentation_array.push_back(HashLocalPresentation<32>{
            gen.make_random_hash(),
            LocalPi(i)
        });
    }

    std::cout << "Building tree (Profiling Target)...\n";

    // std::span implicitly constructs zero-cost view from presentation_array!
    ShallowTree<hash_bytes> st(presentation_array, ls_tail_bits, REASSURANCE_BYTES_COUNT);

    std::cout << "Done!\n";
    return 0;
}