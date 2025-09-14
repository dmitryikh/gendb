---
applyTo: "**"
---
# DB Code Generation Instructions

## General Flow
1. The database schema is defined in yaml files. Example of such file is `tests/schemas/db.yaml`. This file includes references to FlatBuffers schema files (with `.fbs` extension) that define the structure of the data.

2. The CMakeLists.txt file contains a custom CMake function `gen_db_schema` that is responsible for generating the database code based on the provided schema files. Example usage of this function can be found in `tests/CMakeLists.txt`. `gen_db_messages` is a similar function that generates only message classes.

3. The code generation is performed by a python script `gen/generate_database.py`. The script uses jinja2 templates for generating the C++ code. The database templates are located at `gen/templates/database_template.h.jinja2` and `gen/templates/database_class_template.cpp.jinja2`.

`gen/generate_message_class.py` is a similar script that generates only message classes. Message template is located at `gen/templates/message_class_template.h.jinja2`.

## Naming Conventions
- Use PascalCase for Class/Function/Enum names.
- Use snake_case for variable and function argument names.
