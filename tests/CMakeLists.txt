include(CTest)

find_package(GTest REQUIRED)

add_codegen_target(
    ${CMAKE_SOURCE_DIR}/tests/schemas
    ${CMAKE_SOURCE_DIR}/tests/generated
)

add_executable(message_test message_test.cpp)

target_link_libraries(message_test PRIVATE gendb_lib GTest::gtest_main)

add_dependencies(message_test codegen)

add_test(NAME message_test COMMAND message_test)
