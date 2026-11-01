//#include "store.hpp"
#include "ingester.hpp"

int main() {
    //wedding_cake::Store<32> store(wedding_cake::CreateNewOnDisk{}, "ExampleBasePath");

    wedding_cake::Ingester<32> ingester("ExampleBasePath");

    for (uint64_t block_height = 0; block_height < 10; ++block_height) {
        constexpr size_t hashes_per_block = 32768;
        // Construct vector of 32,768 default value-initialized (all zeroes) optional hashes
        // Using brace/parenthesis initialization to avoid most vexing parse:
        std::vector<std::optional<wedding_cake::HashBytes<32>>> hash_block(
            hashes_per_block,
            wedding_cake::HashBytes<32>{} // Default zero-initialized hash
        );

        // Feed block of hashes into the live pipeline
        ingester.feed(block_height, std::move(hash_block));
    }

    // Flush remaining buffer state to disk on shutdown
    ingester.flush();
    return 0;
}
