include(CTest)

find_package(GTest REQUIRED)


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
    )
endif()

