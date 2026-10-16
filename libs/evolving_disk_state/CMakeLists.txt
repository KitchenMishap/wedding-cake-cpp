add_library(evolving_disk_state STATIC
        src/dummy1.cpp
        src/dummy2.cpp
)

# Tell consumers where to find the header files
target_include_directories(evolving_disk_state PUBLIC include)

# Enforce C++20 standard for both MSVC and GCC/Clang
target_compile_features(evolving_disk_state PUBLIC cxx_std_20)

if (MSVC)
    # Enable UTF-8 source/execution character sets on Windows
    target_compile_options(evolving_disk_state PRIVATE /utf-8)
endif()

# Enable testing if this is the main project or if BUILD_TESTING is ON
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
