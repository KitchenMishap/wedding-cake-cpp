#include <evolving_disk_state/manifest_engine.hpp>
#include "tiers.hpp"
#include <evolving_type_states.hpp>

#include "store.hpp"

int main() {
    evolving_disk_state::Print123();

    Store store;
    store.CreateEmpty("ExampleBasePath");
}
