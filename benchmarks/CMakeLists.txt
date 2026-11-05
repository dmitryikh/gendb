set(BENCHMARK_GENERATED_DIR ${CMAKE_BINARY_DIR}/benchmarks/generated)
set(BENCHMARK_FBS ${CMAKE_CURRENT_SOURCE_DIR}/message_benchmark.fbs)
set(BENCHMARK_FLATBUFFERS_HEADER ${BENCHMARK_GENERATED_DIR}/message_benchmark_generated.h)

find_program(FLATC_EXECUTABLE NAMES flatc REQUIRED)

# Standard FlatBuffers tables for the baseline: these are not GenDB messages.
add_custom_command(
    OUTPUT ${BENCHMARK_FLATBUFFERS_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCHMARK_GENERATED_DIR}
    COMMAND ${FLATC_EXECUTABLE} --cpp --gen-mutable --gen-object-api
            -o ${BENCHMARK_GENERATED_DIR} ${BENCHMARK_FBS}
    DEPENDS ${BENCHMARK_FBS}
    COMMENT "Generating FlatBuffers C++ headers for benchmarks"
    VERBATIM
)

add_executable(message_benchmarks
    message_benchmark.cpp
    message_benchmark_common.h
    ${BENCHMARK_FLATBUFFERS_HEADER}
)
target_include_directories(message_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BENCHMARK_GENERATED_DIR}
    ${CMAKE_SOURCE_DIR}/tests/generated
)
target_link_libraries(message_benchmarks PRIVATE
    gendb_lib
    benchmark::benchmark
    benchmark::benchmark_main
    flatbuffers::flatbuffers
)
add_dependencies(message_benchmarks codegen)
