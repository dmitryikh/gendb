import argparse
from pathlib import Path

from jinja2 import Environment, FileSystemLoader
from clang_format import clang_format
import flatc
import json
import naming
import cpp_types

TEMPLATE_PATH = Path(__file__).parent / "templates" / "message_class_template.h.jinja2"


def enum_name(field_name):
    return naming.PascalCase(field_name)

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
                base_type = f["type"]["base_type"]
                t = cpp_types.cpp_type(base_type)
                is_fixed = cpp_types.is_fixed_size(base_type)
                fields.append({
                    "name": f["name"],
                    "cpp_type": t,
                    "const_ref_type": cpp_types.const_ref_type(base_type),
                    "ref_type": cpp_types.ref_type(base_type),
                    "const_ref_type": cpp_types.const_ref_type(base_type),
                    "enum_name": enum_name(f["name"]),
                    "default": cpp_types.default_value(base_type),
                    "is_fixed_size": is_fixed,
                    "size": cpp_types.type_size(base_type)
                })
                if is_fixed:
                    fixed_indices.append(idx)
                    fixed_fields.append({"idx": idx, "enum_name": enum_name(f["name"])});
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
