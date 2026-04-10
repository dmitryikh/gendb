set(BENCHMARK_FLATBUFFERS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/benchmarks/generated/flatbuffers)
file(GLOB BENCHMARK_FBS_FILES ${CMAKE_SOURCE_DIR}/tests/schemas/*.fbs)

find_program(FLATC_EXECUTABLE NAMES flatc REQUIRED)

add_custom_target(flatbuffers_codegen
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCHMARK_FLATBUFFERS_OUTPUT_DIR}
    COMMAND ${FLATC_EXECUTABLE} --cpp -o ${BENCHMARK_FLATBUFFERS_OUTPUT_DIR} ${BENCHMARK_FBS_FILES}
    DEPENDS ${BENCHMARK_FBS_FILES}
    COMMENT "Generating FlatBuffers C++ headers for benchmarks"
)

add_executable(gendb_message_benchmarks
    message_benchmark_common.h
    gendb_message_benchmark.cpp
)
target_include_directories(gendb_message_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests/generated
)
target_link_libraries(gendb_message_benchmarks PRIVATE
    gendb_lib
    benchmark::benchmark
    benchmark::benchmark_main
)
add_dependencies(gendb_message_benchmarks codegen)

add_executable(flatbuffers_message_benchmarks
    message_benchmark_common.h
    flatbuffers_message_benchmark.cpp
)
target_include_directories(flatbuffers_message_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BENCHMARK_FLATBUFFERS_OUTPUT_DIR}
)
target_link_libraries(flatbuffers_message_benchmarks PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    flatbuffers::flatbuffers
)
add_dependencies(flatbuffers_message_benchmarks flatbuffers_codegen)
