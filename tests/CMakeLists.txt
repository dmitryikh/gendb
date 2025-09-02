# Enable testing
include(CTest)

# Find GTest
find_package(GTest REQUIRED)

# Add test sources
add_executable(message_class_test message_class_test.cpp)

# Include generated headers
# (Assumes generated headers are in ${CMAKE_BINARY_DIR}/gendb/tests)
target_include_directories(message_class_test PRIVATE ${CMAKE_BINARY_DIR}/gendb/tests)

# Link GTest and pthread
target_link_libraries(message_class_test PRIVATE gendb_lib GTest::gtest_main)

# Add test to CTest
add_test(NAME message_class_test COMMAND message_class_test)
