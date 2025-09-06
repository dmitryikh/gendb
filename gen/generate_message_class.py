import argparse
from pathlib import Path
from jinja2 import Environment, FileSystemLoader
from clang_format import clang_format
import flatc
import json
import naming

TEMPLATE_PATH = Path(__file__).parent / "templates" / "message_class_template.h.jinja2"

def cpp_type(flat_type):
    # Map FlatBuffer types to C++ types
    mapping = {
        "Uint": "uint32_t",
        "Int": "int32_t",
        "Float": "float",
        "Bool": "bool",
        "String": "std::string_view",
        # Add more mappings as needed
    }
    return mapping[flat_type]

def is_fixed_size(flat_type):
    # Map FlatBuffer types to fixed size status
    mapping = {
        "Uint": True,
        "Int": True,
        "Float": True,
        "Bool": True,
        "String": False,
        # Add more mappings as needed
    }
    return mapping[flat_type]

def enum_name(field_name):
    # Convert field name to PascalCase for enum
    return naming.PascalCase(field_name)

def default_value(flat_type):
    # Provide default values for C++ types
    defaults = {
        "uint32_t": "0",
        "int32_t": "0",
        "float": "0.0f",
        "bool": "false",
        "std::string_view": '""',
    }
    return defaults[flat_type]

def main():
    parser = argparse.ArgumentParser(description="Generate C++ headers from all FlatBuffer schemas in a directory.")
    parser.add_argument("--fbs_dir", type=Path, required=True, help="Directory containing .fbs schema files")
    parser.add_argument("--output_dir", type=Path, required=True, help="Directory to write generated header files")
    args = parser.parse_args()

    env = Environment(loader=FileSystemLoader(str(TEMPLATE_PATH.parent)), trim_blocks=True, lstrip_blocks=True)
    template = env.get_template(TEMPLATE_PATH.name)

    args.output_dir.mkdir(parents=True, exist_ok=True)

    generated_files = []
    for fbs_file in args.fbs_dir.glob("*.fbs"):
        schema = flatc.get_schema(fbs_file)
        for table in schema["objects"]:
            fields = []
            fixed_indices = []
            fixed_fields = []
            for idx, f in enumerate(table["fields"]):
                t = cpp_type(f["type"]["base_type"])
                is_fixed = is_fixed_size(f["type"]["base_type"])
                fields.append({
                    "name": f["name"],
                    "cpp_type": t,
                    "enum_name": enum_name(f["name"]),
                    "default": default_value(t),
                    "is_fixed_size": is_fixed
                })
                if is_fixed:
                    fixed_indices.append(idx)
                    fixed_fields.append({"idx": idx, "enum_name": enum_name(f["name"])})
            # Compute K for bitmask
            K = (max(fixed_indices) // 32 + 1) if fixed_indices else 1
            namespace, class_name = naming.split_namespace_class(table["name"])
            table_data = {
                "name": table["name"],
                "namespace": naming.to_cpp_namespace(namespace),
                "class_name": class_name,
                "fields": fields,
                "fixed_indices": fixed_indices,
                "fixed_fields": fixed_fields,
                "K": K
            }
            output_file = args.output_dir / f"{class_name}.h"
            cpp_code = template.render(table=table_data, fields=fields)
            output_file.write_text(cpp_code)
            generated_files.append(output_file)
            print(f"Generated {output_file}")

    # Run clang-format on generated files
    clang_format(generated_files)

if __name__ == "__main__":
    main()
