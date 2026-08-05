#include <iostream>
#include <random>

#include "../include/wedding_cake/ShallowTree.hpp"
#include "../include/wedding_cake/types.hpp"

// Fixed-seed PRNG for deterministic, reproducible test runs
class DeterministicHashGenerator {
    std::mt19937_64 rng_{42}; // Fixed seed 42
public:
    std::vector<uint8_t> make_random_hash(size_t num_bytes) {
        std::vector<uint8_t> bytes(num_bytes);
        for (size_t i = 0; i < num_bytes; ++i) {
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
    DeterministicHashGenerator gen;
    std::vector<HashPresentation> presentation_array;
    presentation_array.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        presentation_array.push_back({
            gen.make_random_hash(hash_bytes),
            LocalPi(i)
        });
    }

    std::cout << "Building tree (Profiling Target)...\n";

    // std::span implicitly constructs zero-cost view from presentation_array!
    ShallowTree st(presentation_array, ls_tail_bits, hash_bytes, REASSURANCE_BYTES_COUNT);

    std::cout << "Done!\n";
    return 0;
}