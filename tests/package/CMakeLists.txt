# Uses an installed gendb the way an application would. scripts/ci/run.sh package builds and runs it.
cmake_minimum_required(VERSION 3.20)
project(gendb_package_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gendb REQUIRED)

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
gen_db_messages(messages ${CMAKE_CURRENT_SOURCE_DIR}/schemas ${GENERATED})
gen_db_schema(database ${CMAKE_CURRENT_SOURCE_DIR}/schemas/db.yaml ${GENERATED})

set_source_files_properties(${GENERATED}/database.cpp ${GENERATED}/database.h PROPERTIES GENERATED TRUE)
add_executable(package_test main.cpp ${GENERATED}/database.cpp)
target_include_directories(package_test PRIVATE ${GENERATED})
target_link_libraries(package_test PRIVATE gendb::gendb)
add_dependencies(package_test messages database)
