cmake_minimum_required(VERSION 3.20)
project(gendb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CodegenTargets.cmake)


add_codegen_target(
    ${CMAKE_SOURCE_DIR}/tests/schemas
    ${CMAKE_BINARY_DIR}/tests/schemas
)

include_directories(
    ${CMAKE_SOURCE_DIR}/lib
)

# Main library
add_library(gendb_lib
    lib/message_format.h
    lib/message_format.cpp
)
target_include_directories(gendb_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# Add your test sources here
add_executable(gendb_tests
    lib/message_format_test.cpp
)
target_link_libraries(gendb_tests PRIVATE gendb_lib GTest::gtest_main)
add_test(NAME gendb_tests COMMAND gendb_tests)

add_subdirectory(tests)


# Example executable (add your .cpp files here)
# add_executable(gendb_example src/main.cpp)
# target_link_libraries(gendb_example PRIVATE gendb_lib)

# GoogleTest setup for unit tests
enable_testing()
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# Add your test sources here
# add_executable(gendb_tests tests/test_message_format.cpp)
# target_link_libraries(gendb_tests PRIVATE gendb_lib gtest_main)
# add_test(NAME gendb_tests COMMAND gendb_tests)

# TODO: Add your test sources

find_program(CLANG_FORMAT_EXE NAMES clang-format)

file(GLOB_RECURSE ALL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/*.h"
    "${CMAKE_SOURCE_DIR}/*.cpp"
)

add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXE}
    -i
    --style=file
    ${ALL_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting all .h and .cpp files with clang-format"
)