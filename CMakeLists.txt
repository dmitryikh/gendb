cmake_minimum_required(VERSION 3.20)
project(gendb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

option(GENDB_BUILD_TESTS "Build tests, benchmarks and examples" ON)

set(GENDB_GEN_DIR ${CMAKE_SOURCE_DIR}/gen)
set(GENDB_SCHEMA_DIR ${CMAKE_SOURCE_DIR}/lib/schemas)
include(cmake/CodegenTargets.cmake)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

gen_db_messages(
    gendb_lib_codegen
    ${CMAKE_SOURCE_DIR}/lib/schemas
    ${CMAKE_SOURCE_DIR}/lib/gendb/generated
)

find_package(absl REQUIRED)
if(GENDB_BUILD_TESTS)
    find_package(benchmark REQUIRED)
    find_package(GTest REQUIRED)
endif()

# Main library
add_library(gendb_lib
    lib/gendb/bits.h
    lib/gendb/math.h
    lib/gendb/list.h
    lib/gendb/message.h
    lib/gendb/message.cpp
    lib/gendb/checkpoint.h
    lib/gendb/checkpoint.cpp
    lib/gendb/commit_ring.h
    lib/gendb/cursors.cpp
    lib/gendb/engine.h
    lib/gendb/engine.cpp
    lib/gendb/engine_compaction.cpp
    lib/gendb/engine_internal.h
    lib/gendb/engine_recovery.cpp
    lib/gendb/engine_replication.cpp
    lib/gendb/file_io.h
    lib/gendb/file_io.cpp
    lib/gendb/file_system.h
    lib/gendb/file_system.cpp
    lib/gendb/log_format.h
    lib/gendb/observer.h
    lib/gendb/ordered_index.h
    lib/gendb/record_table.h
    lib/gendb/replication.h
    lib/gendb/replication.cpp
    lib/gendb/subscription.cpp
    lib/gendb/transaction.cpp
    lib/gendb/wal.h
    lib/gendb/wal.cpp
)
target_include_directories(gendb_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/gendb/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/gendb/generated>
)
set_target_properties(gendb_lib PROPERTIES EXPORT_NAME gendb)
add_library(gendb::gendb ALIAS gendb_lib)
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
    set_target_properties(gendb_replication_grpc PROPERTIES EXPORT_NAME replication_grpc)
    add_library(gendb::replication_grpc ALIAS gendb_replication_grpc)
endif()

if(GENDB_BUILD_TESTS)
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
endif()

option(GENDB_BUILD_FUZZERS "Build the libFuzzer targets; needs clang" OFF)
if(GENDB_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

# The package: find_package(gendb) gives gendb::gendb, gendb::replication_grpc if built, and the
# generators.
set(GENDB_INSTALL_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/gendb)
set(GENDB_INSTALL_GEN_DIR ${CMAKE_INSTALL_DATADIR}/gendb/gen)
set(GENDB_INSTALL_SCHEMA_DIR ${CMAKE_INSTALL_DATADIR}/gendb/schemas)

install(TARGETS gendb_lib EXPORT gendbTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY lib/gendb DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*_test.h" EXCLUDE
)
install(DIRECTORY gen/ DESTINATION ${GENDB_INSTALL_GEN_DIR}
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*_test.py" EXCLUDE
    PATTERN "fuzz_*" EXCLUDE
)
install(DIRECTORY lib/schemas/ DESTINATION ${GENDB_INSTALL_SCHEMA_DIR})
install(EXPORT gendbTargets NAMESPACE gendb:: DESTINATION ${GENDB_INSTALL_CMAKE_DIR})

set(GENDB_INSTALL_GRPC 0)
if(TARGET gendb_replication_grpc)
    set(GENDB_INSTALL_GRPC 1)
    install(TARGETS gendb_replication_grpc EXPORT gendbGrpcTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(EXPORT gendbGrpcTargets NAMESPACE gendb:: DESTINATION ${GENDB_INSTALL_CMAKE_DIR})
endif()

configure_package_config_file(cmake/gendbConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/package/gendbConfig.cmake
    INSTALL_DESTINATION ${GENDB_INSTALL_CMAKE_DIR}
    PATH_VARS GENDB_INSTALL_GEN_DIR GENDB_INSTALL_SCHEMA_DIR
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/package/gendbConfigVersion.cmake
    COMPATIBILITY SameMinorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/package/gendbConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/package/gendbConfigVersion.cmake
    cmake/CodegenTargets.cmake
    DESTINATION ${GENDB_INSTALL_CMAKE_DIR}
)

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
