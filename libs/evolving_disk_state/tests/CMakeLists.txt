add_executable(test_settler_dummy test_dummy.cpp)

target_link_libraries(test_settler_dummy PRIVATE
        evolving_disk_state
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_settler_dummy)