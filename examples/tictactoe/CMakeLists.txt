# An application of its own: it takes GenDB from a Conan package, not from this repository's build.
cmake_minimum_required(VERSION 3.20)
project(tictactoe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gendb 0.1 REQUIRED)

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
gen_db_messages(tictactoe_messages ${CMAKE_CURRENT_SOURCE_DIR}/schemas ${GENERATED})
gen_db_schema(tictactoe_database ${CMAKE_CURRENT_SOURCE_DIR}/schemas/db.yaml ${GENERATED})

set_source_files_properties(${GENERATED}/database.cpp ${GENERATED}/database.h PROPERTIES GENERATED TRUE)
add_executable(tictactoe main.cpp game_store.h ${GENERATED}/database.cpp)
# The parent directory has the raw TCP transport that is shared with replication_demo.
target_include_directories(tictactoe PRIVATE ${GENERATED} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tictactoe PRIVATE gendb::gendb)
add_dependencies(tictactoe tictactoe_messages tictactoe_database)
