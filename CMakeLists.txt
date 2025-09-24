cmake_minimum_required(VERSION 3.20)
project(gendb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CodegenTargets.cmake)


include_directories(
    ${CMAKE_SOURCE_DIR}/lib
)

gen_db_messages(
    gendb_lib_codegen
    ${CMAKE_SOURCE_DIR}/lib/schemas
    ${CMAKE_SOURCE_DIR}/lib/gendb/generated
)

find_package(absl REQUIRED)
find_package(RocksDB REQUIRED)

# Main library
add_library(gendb_lib
    lib/gendb/message_base.h
    lib/gendb/message_base.cpp
    lib/gendb/message_builder.h
    lib/gendb/message_builder.cpp
    lib/gendb/bits.h
    lib/gendb/math.h
    lib/gendb/message_patch.h
    lib/gendb/message_patch.cpp
    lib/gendb/storage.h
    lib/gendb/storage.cpp
    lib/gendb/layered_storage.h
    lib/gendb/layered_storage.cpp
)
target_include_directories(gendb_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(gendb_lib PUBLIC absl::inlined_vector absl::hash absl::span absl::status absl::strings RocksDB::rocksdb)
add_dependencies(gendb_lib gendb_lib_codegen)

# Add your test sources here
add_executable(gendb_tests
    lib/gendb/message_format_test.cpp
    lib/gendb/bits_test.cpp
    lib/gendb/storage_test.cpp
)
target_include_directories(gendb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/lib)
target_link_libraries(gendb_tests PRIVATE gendb_lib GTest::gtest_main)
add_test(NAME gendb_tests COMMAND gendb_tests)

add_subdirectory(tests)

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