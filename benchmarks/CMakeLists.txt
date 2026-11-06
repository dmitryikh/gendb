add_executable(message_benchmarks
    message_benchmark.cpp
    message_benchmark_common.h
)
target_include_directories(message_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests/generated
)
target_link_libraries(message_benchmarks PRIVATE
    gendb_lib
    benchmark::benchmark
    benchmark::benchmark_main
)
add_dependencies(message_benchmarks codegen)

add_executable(db_benchmarks
    db_benchmark.cpp
    ${CMAKE_SOURCE_DIR}/tests/generated/database.cpp
)
target_include_directories(db_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/tests/generated ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(db_benchmarks PRIVATE
    gendb_lib
    benchmark::benchmark
    benchmark::benchmark_main
)
add_dependencies(db_benchmarks codegen db_codegen)
