cmake_minimum_required(VERSION 3.20)
project(wedding_cake CXX)

# 1. Define the executable target and its source files
add_executable(wedding_cake
        src/main.cpp
        include/evolving_type_states.hpp
        include/tiers.hpp
        include/store.hpp
        include/concepts.hpp
)

# Statically link the C++ standard library so the user doesn't need to install it
if(MINGW)
    target_link_options(wedding_cake PRIVATE
            -mcrtdll=ucrt
            -static
    )
endif()

# 2. Link the static evolving_disk_state library
target_link_libraries(wedding_cake PRIVATE evolving_disk_state)

# 3. Application-specific include directories
target_include_directories(wedding_cake PRIVATE include)

# 4. Enable testing and add test targets
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()