import argparse
from pathlib import Path

from jinja2 import Environment, FileSystemLoader
from clang_format import clang_format
import flatc
import json
import naming
import cpp_types

def get_enum_types(schema):
    """
    Returns a dict mapping enum names to their C++ type, namespace, underlying type, and values.
    """
    enums = {}
    for enum in schema.get("enums", []):
        namespace, name = naming.split_namespace_class(enum["name"])
        enums[name] = {
            "namespace": naming.to_cpp_namespace(namespace),
            "underlying_type": cpp_types.cpp_type(enum["underlying_type"]["base_type"]),
            "vals": [(v["name"], v["value"]) for v in enum["values"]]
        }
    return enums

TEMPLATE_PATH = Path(__file__).parent / "templates" / "message_class_template.h.jinja2"


def enum_name(field_name):
    return naming.PascalCase(field_name)

def main():
    parser = argparse.ArgumentParser(description="Generate C++ headers from all FlatBuffer schemas in a directory.")
    parser.add_argument("--fbs_dir", type=Path, required=True, help="Directory containing .fbs schema files")
    parser.add_argument("--output_dir", type=Path, required=True, help="Directory to write generated header files")
    args = parser.parse_args()

    env = Environment(loader=FileSystemLoader(str(TEMPLATE_PATH.parent)), trim_blocks=True, lstrip_blocks=True)
    env.filters['pascalcase'] = naming.PascalCase
    env.filters['snakecase'] = naming.snake_case
    template = env.get_template(TEMPLATE_PATH.name)

    args.output_dir.mkdir(parents=True, exist_ok=True)


    generated_files = []
    for fbs_file in args.fbs_dir.glob("*.fbs"):
        schema = flatc.get_schema(fbs_file)
        enums = get_enum_types(schema)
        tables = []
        for table in schema["objects"]:
            sorted_fields = sorted(table["fields"], key=lambda f: f.get("id", 0))
            fields = []
            fixed_indices = []
            fixed_fields = []
            for idx, f in enumerate(sorted_fields):
                assert idx == f.get("id", 0), f"Field ids must be sequential starting from 0 in {table['name']}"
                base_type = f["type"]["base_type"]
                field_id = f.get("id", 0)
                # Check if field is enum
                enum_type = None
                if cpp_types.is_integral_type(base_type) and f["type"].get("index", -1) >= 0:
                    # Could be enum or table, check if index refers to enum
                    idx_enum = f["type"]["index"]
                    if idx_enum < len(schema.get("enums", [])):
                        _, enum_type = naming.split_namespace_class(schema["enums"][idx_enum]["name"])
                if enum_type:
                    full_cpp_type = f"{enums[enum_type]['namespace']}::{enum_type}" if enums[enum_type]['namespace'] else f"::{enum_type}"
                    default = f"{f.get('default_integer', 0)}"
                    const_ref_type = f"{enum_type}"
                    ref_type = f"{enum_type}&"
                    t = enum_type
                else:
                    full_cpp_type = cpp_types.cpp_type(base_type)
                    default = cpp_types.default_value(base_type)
                    const_ref_type = cpp_types.const_ref_type(base_type)
                    ref_type = cpp_types.ref_type(base_type)
                    t = full_cpp_type
                is_fixed = cpp_types.is_fixed_size(base_type)
                fields.append({
                    "name": f["name"],
                    "cpp_type": t,
                    "const_ref_type": const_ref_type,
                    "ref_type": ref_type,
                    "default": default,
                    "is_fixed_size": is_fixed,
                    "is_enum": bool(enum_type),
                    "enum_type": enum_type,
                    "full_cpp_type": full_cpp_type
                })
                if is_fixed:
                    fixed_indices.append(field_id)
                    fixed_fields.append({"idx": field_id, "name": f["name"]});
            K = (max(fixed_indices) // 32 + 1) if fixed_indices else 1
            namespace, name = naming.split_namespace_class(table["name"])
            tables.append({
                "name": name,
                "namespace": naming.to_cpp_namespace(namespace),
                "fields": fields,
                "fixed_indices": fixed_indices,
                "fixed_fields": fixed_fields,
                "K": K,
            })
        # Output file named after .fbs file
        output_file = args.output_dir / f"{fbs_file.name}.h"
        cpp_code = template.render(tables=tables, enums=enums)
        output_file.write_text(cpp_code)
        generated_files.append(output_file)
        print(f"Generated {output_file}")

    # Run clang-format on generated files
    clang_format(generated_files)

if __name__ == "__main__":
    main()
