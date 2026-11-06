# Each is a libFuzzer binary. The whole build needs -fsanitize=fuzzer-no-link,address, which
# scripts/ci/fuzz.sh sets up.
foreach(target fuzz_message fuzz_recovery fuzz_replication)
    add_executable(${target} ${target}.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE gendb_lib)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endforeach()
