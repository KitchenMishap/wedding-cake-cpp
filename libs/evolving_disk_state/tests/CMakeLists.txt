add_executable(test_evolving_disk_state_dummy test_dummy.cpp
        test_buffer_room.cpp)

target_link_libraries(test_evolving_disk_state_dummy PRIVATE
        evolving_disk_state
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_evolving_disk_state_dummy)