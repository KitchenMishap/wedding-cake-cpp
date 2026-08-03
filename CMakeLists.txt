cmake_minimum_required(VERSION 3.20)
project(wedding-cake-cpp VERSION 0.1.0 LANGUAGES CXX)

# Fix CMP0135 warning for FetchContent archive timestamps
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

include_directories(include)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# Prevent GoogleTest from installing itself or enabling its own internal tests/Python dependency
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

# Test Executable Target
add_executable(unit_tests
    tests/shallow_tree_test.cpp
        include/HashTypes.hpp
        include/ShallowTree.hpp
)
target_link_libraries(unit_tests PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)