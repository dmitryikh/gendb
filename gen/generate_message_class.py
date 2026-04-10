import argparse
from pathlib import Path

from jinja2 import Environment, FileSystemLoader
from clang_format import clang_format
import flatc
import json
import flatc_to_store
import naming
import base_types
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
            storage_class_counts = {1: 0, 4: 0, 8: 0}
            for f_name in store.list_fields(msg_name):
                f = store.get_field(msg_name, f_name)
                f.fixed_descriptor_index = None
                fields.append(f)

                if f.is_fixed_size:
                    fixed_indices.append(f.id)
                    logical_size = base_types.cpp_type_size(f.underlying_type) if f.field_kind == FieldKind.ENUM else base_types.type_size(f.type)
                    storage_width = 4 if logical_size == 2 else logical_size
                    class_ordinal = storage_class_counts[storage_width]
                    word_index = class_ordinal // 32
                    bit_index = class_ordinal
                    mask_before = (1 << (class_ordinal % 32)) - 1
                    f.fixed_descriptor_index = len(fixed_fields)
                    fixed_fields.append({
                        "idx": f.id,
                        "name": f.name,
                        "fixed_descriptor_index": f.fixed_descriptor_index,
                        "logical_size": logical_size,
                        "storage_width": storage_width,
                        "bit_index_within_class": bit_index,
                        "ordinal_within_class": class_ordinal,
                        "mask_before_within_word": mask_before,
                        "word_index_within_class": word_index,
                    })
                    storage_class_counts[storage_width] += 1
            K = (max(fixed_indices) // 32 + 1) if fixed_indices else 1
            namespace, name = naming.split_namespace_class(msg.full_name)
            tables.append({
                "name": name,
                "namespace": naming.to_cpp_namespace(namespace),
                "fields": fields,
                "fixed_indices": fixed_indices,
                "fixed_fields": fixed_fields,
                "fixed_word_counts": {
                    1: (storage_class_counts[1] + 31) // 32,
                    4: (storage_class_counts[4] + 31) // 32,
                    8: (storage_class_counts[8] + 31) // 32,
                },
                "K": K,
            })

        for e_name in store.list_enums():
            enum = store.get_enum(e_name)
            namespace, name = naming.split_namespace_class(enum.full_name)
            enums[enum.full_name] = {
                "name": name,
                "namespace": naming.to_cpp_namespace(namespace),
                "underlying_type": base_types.cpp_type(enum.underlying_type),
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
