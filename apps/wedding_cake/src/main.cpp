#include "store.hpp"

int main() {
    wedding_cake::Store<32> store(wedding_cake::CreateNewOnDisk{}, "ExampleBasePath");
}
