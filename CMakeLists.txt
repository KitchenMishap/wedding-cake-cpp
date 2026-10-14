cmake_minimum_required(VERSION 3.20)
project(wedding-cake-cpp VERSION 0.1.0 LANGUAGES CXX)

# Fix CMP0135 warning for FetchContent archive timestamps
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure RelWithDebInfo or Debug build types carry frame pointers globally for flame graphs
add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
        $<$<COMPILE_LANGUAGE:CXX>:-mno-omit-leaf-frame-pointer>
)

include_directories(include)

# -----------------------------------------------------------------------------
# 1. Fetch GoogleTest Dependency
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# 2. Target A: Unit Tests Target (GoogleTest Runner)
# -----------------------------------------------------------------------------
add_executable(unit_tests
        tests/shallow_tree_test.cpp
        tests/hashes_order_file_test.cpp
        tests/prefix_entries_file_test.cpp
        tests/prefix_entries_path_test.cpp
        tests/pipeline_test.cpp
        include/wedding_cake/types.hpp
        include/wedding_cake/shallow_tree.hpp
        # Evolving Disk State Subsystem Headers
        include/evolving_disk_state/external_feeder.hpp
        include/evolving_disk_state/manifest_engine.hpp
        include/evolving_disk_state/pipeline_buffer.hpp
        include/evolving_disk_state/slice_context.hpp
        include/evolving_disk_state/tier_orchestrator.hpp
        include/evolving_disk_state/type_states.hpp
)

target_include_directories(unit_tests PRIVATE include)
target_link_libraries(unit_tests PRIVATE gtest_main)

# Register GoogleTest target explicitly for CTest & CLion Test Runner
include(GoogleTest)
gtest_discover_tests(unit_tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES
        LABELS "GoogleTest"
)

# -----------------------------------------------------------------------------
# 3. Target B: Profiling Target (Flame Graphs & Perf Analysis)
# -----------------------------------------------------------------------------
add_executable(profile_shallow_tree
        src/profile_main.cpp
        include/wedding_cake/concepts.hpp
        include/wedding_cake/hashes_order_file.hpp
        include/wedding_cake/prefix_entries_file.hpp
        include/wedding_cake/reader_writer_factory.hpp
        include/wedding_cake/prefix_entries_path.hpp
)

target_include_directories(profile_shallow_tree PRIVATE include)

if(NOT MSVC)
    target_compile_options(profile_shallow_tree PRIVATE
            -O2                         # Optimization enabled
            -g3                         # Full macro & line debug info
            -fno-omit-frame-pointer     # Keep frame pointers for perf / eBPF flame graphs
            -mno-omit-leaf-frame-pointer
            -fno-optimize-sibling-calls # Prevents tail-call elimination
            -fno-inline                 # Stops inlining of functions explicitly marked inline
            -fno-inline-functions       # Prevents compiler from auto-inlining functions under -O2
            -fno-default-inline         # Prevents inline member functions inside classes from inlining
    )
endif()

    # -rdynamic is Linux/POSIX specific for symbol exporting in profilers
if(UNIX AND NOT APPLE)
    target_link_options(profile_shallow_tree PRIVATE
            -rdynamic
    )
endif()