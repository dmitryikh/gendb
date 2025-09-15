import argparse
from pathlib import Path

from jinja2 import Environment, FileSystemLoader
from clang_format import clang_format
import flatc
import json
import flatc_to_store
import naming
import cpp_types
from store import Store
from fb_types import FieldKind

TEMPLATE_PATH = Path(__file__).parent / "templates" / "message_class_template.h.jinja2"

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
        store = Store()
        flatc_to_store.load_fbs_to_store(store, fbs_file)
        tables = []
        enums = {}
        for msg_name in store.list_messages():
            msg = store.get_message(msg_name)
            fields = []
            fixed_indices = []
            fixed_fields = []
            for f_name in store.list_fields(msg_name):
                f = store.get_field(msg_name, f_name)
                fields.append(f)

                if f.is_fixed_size:
                    fixed_indices.append(f.id)
                    fixed_fields.append({"idx": f.id, "name": f.name});
            K = (max(fixed_indices) // 32 + 1) if fixed_indices else 1
            namespace, name = naming.split_namespace_class(msg.full_name)
            tables.append({
                "name": name,
                "namespace": naming.to_cpp_namespace(namespace),
                "fields": fields,
                "fixed_indices": fixed_indices,
                "fixed_fields": fixed_fields,
                "K": K,
            })

        for e_name in store.list_enums():
            enum = store.get_enum(e_name)
            namespace, name = naming.split_namespace_class(enum.full_name)
            enums[enum.full_name] = {
                "name": name,
                "namespace": naming.to_cpp_namespace(namespace),
                "underlying_type": cpp_types.cpp_type(enum.underlying_type),
                "vals": [(k, v) for k, v in enum.values.items()]
            }
        # Output file named after .fbs file
        output_file = args.output_dir / f"{fbs_file.name}.h"
        cpp_code = template.render(tables=tables, enums=enums, FieldKind=FieldKind)
        output_file.write_text(cpp_code)
        generated_files.append(output_file)
        print(f"Generated {output_file}")

    # Run clang-format on generated files
    clang_format(generated_files)

if __name__ == "__main__":
    main()
