cmake_minimum_required(VERSION 3.20)
project(wedding-cake-cpp VERSION 0.1.0 LANGUAGES CXX)

# Fix CMP0135 warning for FetchContent archive timestamps
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure RelWithDebInfo or Debug build types carry frame pointers globally
add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
        $<$<COMPILE_LANGUAGE:CXX>:-mno-omit-leaf-frame-pointer>
)

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

# -----------------------------------------------------------------------------
# 3. Target B: Profiling Target (Standalone main)
# -----------------------------------------------------------------------------
add_executable(profile_shallow_tree
        src/profile_main.cpp
)

target_include_directories(profile_shallow_tree PRIVATE include)

if(NOT MSVC)
    target_compile_options(profile_shallow_tree PRIVATE
            -O2                         # Optimization enabled
            -g3                         # Full macro & line debug info
            -fno-omit-frame-pointer     # Keep frame pointers
            -mno-omit-leaf-frame-pointer
            -fno-optimize-sibling-calls # Prevents tail-call elimination
            -fno-inline                 # Stops inlining of functions explicitly marked inline
            -fno-inline-functions       # Prevents compiler from auto-inlining functions under -O2
            -fno-default-inline         # Prevents inline member functions inside classes from inlining
    )

    target_link_options(profile_shallow_tree PRIVATE
            -rdynamic                   # Export dynamic symbols for perf/DTrace
    )
endif()