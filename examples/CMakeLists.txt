add_executable(replication_demo
    replication_demo.cpp
    ${CMAKE_SOURCE_DIR}/tests/generated/database.cpp
)
target_include_directories(replication_demo PRIVATE ${CMAKE_SOURCE_DIR}/tests/generated)
target_link_libraries(replication_demo PRIVATE gendb_lib)
if(TARGET gendb_replication_grpc)
    target_link_libraries(replication_demo PRIVATE gendb_replication_grpc)
    target_compile_definitions(replication_demo PRIVATE GENDB_DEMO_WITH_GRPC)
endif()
add_dependencies(replication_demo codegen db_codegen)
