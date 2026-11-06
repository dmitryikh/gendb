# Message codegen
gen_db_messages(
    codegen
    ${CMAKE_SOURCE_DIR}/tests/schemas
    ${CMAKE_SOURCE_DIR}/tests/generated
)

# Database codegen
gen_db_schema(
    db_codegen
    ${CMAKE_SOURCE_DIR}/tests/schemas/db.yaml
    ${CMAKE_SOURCE_DIR}/tests/generated
)

gen_db_schema(
    primitive_db_codegen
    ${CMAKE_SOURCE_DIR}/tests/schemas/primitive_db.yaml
    ${CMAKE_SOURCE_DIR}/tests/generated
)

add_executable(message_test
    message_test.cpp
    lib/parse_text.cpp
    lib/parse_text.h
)
target_link_libraries(message_test PRIVATE gendb_lib GTest::gtest_main)
add_dependencies(message_test codegen)
add_test(NAME message_test COMMAND message_test)



add_executable(database_test
    database_test.cpp
    generated/database.h
    generated/database.cpp
)
target_link_libraries(database_test PRIVATE gendb_lib GTest::gtest_main GTest::gmock)
add_dependencies(database_test codegen db_codegen primitive_db_codegen)
add_test(NAME database_test COMMAND database_test)

add_executable(persistence_test
    persistence_test.cpp
    generated/database.h
    generated/database.cpp
)
target_link_libraries(persistence_test PRIVATE gendb_lib GTest::gtest_main GTest::gmock)
add_dependencies(persistence_test codegen db_codegen)
add_test(NAME persistence_test COMMAND persistence_test)

add_executable(subscription_test
    subscription_test.cpp
    generated/database.h
    generated/database.cpp
)
target_link_libraries(subscription_test PRIVATE gendb_lib GTest::gtest_main GTest::gmock)
add_dependencies(subscription_test codegen db_codegen)
add_test(NAME subscription_test COMMAND subscription_test)

# Replication, over an in-process transport and raw TCP, and over gRPC when available.
add_executable(replication_test
    replication_test.cpp
    generated/database.h
    generated/database.cpp
)
target_include_directories(replication_test PRIVATE ${CMAKE_SOURCE_DIR}/examples ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(replication_test PRIVATE gendb_lib GTest::gtest_main GTest::gmock)
add_dependencies(replication_test codegen db_codegen)
add_test(NAME replication_test COMMAND replication_test)

# Crash points and failed operations, on a file system that simulates a disk.
add_executable(fault_injection_test
    fault_injection_test.cpp
    generated/database.h
    generated/database.cpp
)
target_include_directories(fault_injection_test PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(fault_injection_test PRIVATE gendb_lib GTest::gtest_main GTest::gmock)
add_dependencies(fault_injection_test codegen db_codegen)
add_test(NAME fault_injection_test COMMAND fault_injection_test)

if(TARGET gendb_replication_grpc)
    add_executable(replication_grpc_test
        replication_grpc_test.cpp
        generated/database.h
        generated/database.cpp
    )
    target_link_libraries(replication_grpc_test PRIVATE gendb_replication_grpc GTest::gtest_main)
    add_dependencies(replication_grpc_test codegen db_codegen)
    add_test(NAME replication_grpc_test COMMAND replication_grpc_test)
endif()

# Primitive database test
add_executable(primitive_database_test
    primitive_database_test.cpp
    generated/primitive_database.h
    generated/primitive_database.cpp
)
target_link_libraries(primitive_database_test PRIVATE gendb_lib GTest::gtest_main)
add_dependencies(primitive_database_test codegen primitive_db_codegen)
add_test(NAME primitive_database_test COMMAND primitive_database_test)

# Python tests (pytest)
find_program(PYTHON_EXECUTABLE python3)
if(PYTHON_EXECUTABLE)
    add_test(
        NAME python_tests
        COMMAND ${PYTHON_EXECUTABLE} -m pytest ${CMAKE_SOURCE_DIR}/gen
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

