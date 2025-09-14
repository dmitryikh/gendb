# Function to add codegen target for database (db.yaml)
function(gen_db_schema TARGET_NAME YAML_PATH OUTPUT_DIR)
    add_custom_target(${TARGET_NAME}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/gen/gen_database.py
                --yaml ${YAML_PATH} --output_dir ${OUTPUT_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${YAML_PATH} ${CMAKE_SOURCE_DIR}/gen/gen_database.py
        COMMENT "Generating C++ database files from ${YAML_PATH}"
    )
    include_directories(${OUTPUT_DIR})
endfunction()
# CodegenTargets.cmake
# Function to add codegen targets for FlatBuffer schemas using Python generator

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(gen_db_messages TARGET_NAME FBS_DIR OUTPUT_DIR)
    file(GLOB FBS_FILES ${FBS_DIR}/*.fbs)
    add_custom_target(${TARGET_NAME}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/gen/generate_message_class.py
                --fbs_dir ${FBS_DIR} --output_dir ${OUTPUT_DIR}
        DEPENDS ${FBS_FILES} ${CMAKE_SOURCE_DIR}/gen/generate_message_class.py
        COMMENT "Generating C++ headers from schemas in ${FBS_DIR}"
    )
    # Add OUTPUT_DIR to the global include directories
    # TODO: include the directory  only if the target is added to library
    include_directories(${OUTPUT_DIR})
endfunction()
