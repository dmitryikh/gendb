# Targets that generate C++ from schemas. Both the source tree and an installed package include this,
# after setting GENDB_GEN_DIR and GENDB_SCHEMA_DIR.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Generates a message class header for each .fbs file in FBS_DIR.
function(gen_db_messages TARGET_NAME FBS_DIR OUTPUT_DIR)
    file(GLOB FBS_FILES ${FBS_DIR}/*.fbs)
    add_custom_target(${TARGET_NAME}
        COMMAND ${Python3_EXECUTABLE} ${GENDB_GEN_DIR}/generate_message_class.py
                --fbs_dir ${FBS_DIR} --output_dir ${OUTPUT_DIR}
        DEPENDS ${FBS_FILES}
        COMMENT "Generating C++ headers from schemas in ${FBS_DIR}"
        VERBATIM
    )
endfunction()

# Generates the database API described by YAML_PATH. Paths in that file are relative to the top of
# the source tree.
function(gen_db_schema TARGET_NAME YAML_PATH OUTPUT_DIR)
    add_custom_target(${TARGET_NAME}
        COMMAND ${Python3_EXECUTABLE} ${GENDB_GEN_DIR}/gen_database.py
                --yaml ${YAML_PATH} --output_dir ${OUTPUT_DIR}
                --metadata_fbs ${GENDB_SCHEMA_DIR}/metadata.fbs
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${YAML_PATH}
        COMMENT "Generating C++ database files from ${YAML_PATH}"
        VERBATIM
    )
endfunction()
