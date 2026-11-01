//#include "store.hpp"
#include "ingester.hpp"

int main() {
    //wedding_cake::Store<32> store(wedding_cake::CreateNewOnDisk{}, "ExampleBasePath");

    wedding_cake::Ingester<32> ingester("ExampleBasePath");
    return 0;
}
