import argparse
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from clang_format import clang_format
import base_types
import schema_loader
import schema_parser
from schema_types import Field, FieldKind
import naming
from store import Store

TEMPLATE_PATH = Path(__file__).parent / "templates" / "message_class_template.h.jinja2"

# gendb::FieldInfo::ScalarType enumerator for each C++ scalar type.
SCALAR_TYPE_NAMES = {
    "bool": "BOOL",
    "int8_t": "INT8",
    "uint8_t": "UINT8",
    "int16_t": "INT16",
    "uint16_t": "UINT16",
    "int32_t": "INT32",
    "uint32_t": "UINT32",
    "int64_t": "INT64",
    "uint64_t": "UINT64",
    "float": "FLOAT",
    "double": "DOUBLE",
}


def scalar_type_name(field: Field) -> str:
    cpp_type = field.underlying_type if field.field_kind == FieldKind.ENUM else field.cpp_type
    return SCALAR_TYPE_NAMES.get(cpp_type, "NOT_SCALAR")


def render_schema(template, schema_file: Path) -> str:
    store = Store()
    schema_loader.load_schema_to_store(store, schema_file)

    tables = []
    for msg_name in store.list_messages():
        msg = store.get_message(msg_name)
        namespace, name = naming.split_namespace_class(msg.full_name)
        tables.append({
            "name": name,
            "namespace": naming.to_cpp_namespace(namespace),
            "fields": [f for f in msg.fields if not f.deprecated],
            "deprecated_fields": [f for f in msg.fields if f.deprecated],
        })

    enums = {}
    for e_name in store.list_enums():
        enum = store.get_enum(e_name)
        namespace, name = naming.split_namespace_class(enum.full_name)
        enums[enum.full_name] = {
            "name": name,
            "namespace": naming.to_cpp_namespace(namespace),
            "underlying_type": base_types.cpp_type(enum.underlying_type),
            "vals": list(enum.values.items()),
        }
    return template.render(tables=tables, enums=enums, FieldKind=FieldKind)


def make_template():
    env = Environment(loader=FileSystemLoader(str(TEMPLATE_PATH.parent)), trim_blocks=True, lstrip_blocks=True)
    env.filters['pascalcase'] = naming.PascalCase
    env.filters['snakecase'] = naming.snake_case
    env.filters['scalar_type_name'] = scalar_type_name
    return env.get_template(TEMPLATE_PATH.name)


def main():
    parser = argparse.ArgumentParser(description="Generate C++ headers from all schema files in a directory.")
    parser.add_argument("--schema_dir", type=Path, required=True, help="Directory containing .gendb schema files")
    parser.add_argument("--output_dir", type=Path, required=True, help="Directory to write generated header files")
    args = parser.parse_args()

    template = make_template()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    generated_files = []
    for schema_file in sorted(args.schema_dir.glob(f"*{schema_loader.SCHEMA_SUFFIX}")):
        output_file = args.output_dir / f"{schema_file.name}.h"
        try:
            output_file.write_text(render_schema(template, schema_file))
        except schema_parser.SchemaError as error:
            raise SystemExit(f"error: {error}")
        generated_files.append(output_file)
        print(f"Generated {output_file}")

    # Run clang-format on generated files
    clang_format(generated_files)


if __name__ == "__main__":
    main()
