cmake_minimum_required(VERSION 3.20)
project(gendb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

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
find_package(benchmark REQUIRED)
find_package(flatbuffers REQUIRED)
find_package(GTest REQUIRED)

# Main library
add_library(gendb_lib
    lib/gendb/bits.h
    lib/gendb/math.h
    lib/gendb/message.h
    lib/gendb/message.cpp
    lib/gendb/checkpoint.h
    lib/gendb/checkpoint.cpp
    lib/gendb/engine.h
    lib/gendb/engine.cpp
    lib/gendb/file_io.h
    lib/gendb/file_io.cpp
    lib/gendb/log_format.h
    lib/gendb/ordered_index.h
    lib/gendb/record_table.h
    lib/gendb/replication.h
    lib/gendb/replication.cpp
    lib/gendb/wal.h
    lib/gendb/wal.cpp
)
target_include_directories(gendb_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(gendb_lib PUBLIC absl::btree absl::crc32c absl::inlined_vector absl::hash absl::span absl::status absl::statusor absl::strings)
find_package(Threads REQUIRED)
target_link_libraries(gendb_lib PUBLIC Threads::Threads)
add_dependencies(gendb_lib gendb_lib_codegen)

# The optional gRPC transport for replication. Built when gRPC is available; the rest of the library
# does not depend on it. See `with_grpc` in conanfile.py.
find_package(gRPC CONFIG QUIET)
find_package(Protobuf CONFIG QUIET)
if(gRPC_FOUND AND Protobuf_FOUND)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
    find_program(PROTOC_EXECUTABLE protoc REQUIRED)
    set(GENDB_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/gendb)
    set(GENDB_PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated_proto)
    set(GENDB_PROTO_SOURCES
        ${GENDB_PROTO_OUT}/replication.pb.cc
        ${GENDB_PROTO_OUT}/replication.pb.h
        ${GENDB_PROTO_OUT}/replication.grpc.pb.cc
        ${GENDB_PROTO_OUT}/replication.grpc.pb.h
    )
    add_custom_command(
        OUTPUT ${GENDB_PROTO_SOURCES}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENDB_PROTO_OUT}
        COMMAND ${PROTOC_EXECUTABLE}
                --proto_path=${GENDB_PROTO_DIR}
                --cpp_out=${GENDB_PROTO_OUT}
                --grpc_out=${GENDB_PROTO_OUT}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                ${GENDB_PROTO_DIR}/replication.proto
        DEPENDS ${GENDB_PROTO_DIR}/replication.proto
        COMMENT "Generating the gRPC replication service"
        VERBATIM
    )
    add_library(gendb_replication_grpc
        lib/gendb/replication_grpc.h
        lib/gendb/replication_grpc.cpp
        ${GENDB_PROTO_SOURCES}
    )
    target_include_directories(gendb_replication_grpc PRIVATE ${GENDB_PROTO_OUT})
    target_link_libraries(gendb_replication_grpc PUBLIC gendb_lib gRPC::grpc++ protobuf::libprotobuf)
endif()

add_executable(gendb_tests
    lib/gendb/bits_test.cpp
    lib/gendb/key_codec_test.cpp
    lib/gendb/message_test.cpp
)
target_include_directories(gendb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/lib)
target_link_libraries(gendb_tests PRIVATE gendb_lib GTest::gtest_main)
add_test(NAME gendb_tests COMMAND gendb_tests)

add_subdirectory(tests)
add_subdirectory(benchmarks)
add_subdirectory(examples)

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
