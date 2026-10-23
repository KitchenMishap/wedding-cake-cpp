add_executable(test_wedding_cake_app
        test_create_empty.cpp
        test_hashes_65535.cpp
        hashes_order_file_test.cpp
        ls_suffix_entries_file_test.cpp
        ms_prefix_path_test.cpp
)

target_include_directories(test_wedding_cake_app PRIVATE ../include)

target_link_libraries(test_wedding_cake_app PRIVATE
        evolving_disk_state
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_wedding_cake_app)
