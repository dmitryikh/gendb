import yaml
from pathlib import Path
from fb_types import Message, Field, Collection, Database, Index, FieldKind
from store import Store
import naming
from clang_format import clang_format
import cpp_types
import flatc_to_store
import schema_validator


def load_yaml_db(yaml_path):
    with open(yaml_path, 'r') as f:
        return yaml.safe_load(f)


def build_store_from_yaml(db_cfg):
    store = Store()
    # Get include prefix
    include_prefix = db_cfg.get("types", {}).get("include_prefix", "")
    # Load all messages from fbs files
    fbs_files = db_cfg.get("types", {}).get("fbs_files", [])
    # Add metadata.fbs as the first file
    all_fbs_files = ["lib/schemas/metadata.fbs"] + [f for f in fbs_files if f != "lib/schemas/metadata.fbs"]
    for fbs_file in all_fbs_files:
        flatc_to_store.load_fbs_to_store(store, Path(fbs_file))

    # Load collections
    # Add metadata collection as the first collection
    metadata_collection = Collection(
        name="metadata",
        type="gendb.MetadataValue",
        primary_key=["type", "id"],
        private=True
    )
    store.add_collection(metadata_collection)
    for col in db_cfg.get("collections", []):
        collection = Collection(
            name=col["name"],
            type=col["type"],
            primary_key=col["primary_key"] if isinstance(col["primary_key"], list) else [col["primary_key"]],
            private=col.get("private", False)
        )
        store.add_collection(collection)
    # Load indices
    for idx in db_cfg.get("indices", []):
        index = Index(
            name=idx["name"],
            collection=idx["collection"],
            field=idx["fields"][0] if isinstance(idx["fields"], list) else idx["fields"]
        )
        store.add_index(index)
    return store


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Load DB schema from YAML and FBS files, and generate C++ database files.")
    parser.add_argument("--yaml", required=True, help="Path to db.yaml config")
    parser.add_argument("--output_dir", required=False, help="Directory to write generated files")
    args = parser.parse_args()
    db_cfg = load_yaml_db(args.yaml)
    store = build_store_from_yaml(db_cfg)
    print(f"Loaded {len(store.messages)} messages and {len(store.collections)} collections.")
    # Optionally, print summary
    for enum_name in store.list_enums():
        enum = store.get_enum(enum_name)
        print(f"Enum: {enum}")
    for msg_name in store.list_messages():
        msg = store.get_message(msg_name)
        print(f"Message: {msg}")
    for col_name in store.list_collections():
        col = store.get_collection(col_name)
        print(f"Collection: {col}")

    errors = schema_validator.validate_names(store)
    if errors:
        print("Schema validation errors found:")
        for error in errors:
            print(f" - {error}")
        raise ValueError("Schema validation failed")

    # --- Database code generation ---
    from jinja2 import Environment, FileSystemLoader
    template_dir = Path(__file__).parent / "templates"
    env = Environment(loader=FileSystemLoader(str(template_dir)), trim_blocks=True, lstrip_blocks=True)
    env.filters['pascalcase'] = naming.PascalCase
    env.filters['snakecase'] = naming.snake_case
    header_template = env.get_template("database_template.h.jinja2")
    cpp_template = env.get_template("database_template.cpp.jinja2")

    # Compose context for templates
    # Use namespace from first message, or fallback
    namespace = db_cfg.get("options", {}).get("cpp_namespace", "")
    generated_source_base_name = db_cfg.get("options", {}).get("generated_source_base_name", "database")

    # Collect includes
    includes = []
    for msg in store.messages.values():
        includes.append(msg.cpp_include)

    includes = sorted(set(includes))

    # Compose collections info
    collections = []
    for col in store.collections.values():
        type = naming.split_namespace_class(col.type)[1]
        pk_fields = []
        pk_fixed_size = 0
        for pk_name in col.primary_key:
            pk_field = store.get_field(col.type, pk_name)
            if pk_field.is_fixed_size and pk_fixed_size >= 0:
                if pk_field.field_kind == FieldKind.SCALAR:
                    pk_fixed_size += cpp_types.type_size(pk_field.type)
                else:
                    pk_fixed_size += cpp_types.cpp_type_size(pk_field.underlying_type)
            elif not pk_field.is_fixed_size:
                pk_fixed_size = -1
            pk_fields.append(pk_field)

        collections.append({
            "name": col.name,
            "type": type,
            "type_snake_case": naming.snake_case(type),
            "enum_name": naming.PascalCase(type) + "CollId",
            "pk_fields": pk_fields,
            "pk_fixed_size": pk_fixed_size, # -1 in case the size is not fixed
            "private": col.private,
        })

    # Compose indices info
    indices = []
    for idx in store.indices.values():
        collection = store.get_collection(idx.collection)
        msg = store.get_message(collection.type)
        key_type = store.get_field(collection.type, idx.field).type
        value_type = store.get_field(collection.type, collection.primary_key[0]).type
        col_type = naming.split_namespace_class(collection.type)[1]
        indices.append({
            "name": idx.name,
            "name_pascal_case": naming.PascalCase(idx.name),
            "collection": idx.collection,
            "type": col_type,
            "type_snake_case": naming.snake_case(col_type),
            "field": idx.field,
            "field_enum": naming.PascalCase(idx.field),
            "key_type": key_type,
            "key_cpp_type": cpp_types.cpp_type(key_type),
            "value_cpp_type": cpp_types.cpp_type(value_type),
            "index_class": f"gendb::Index</*{idx.field}*/ {cpp_types.cpp_type(key_type)}, std::array<uint8_t, sizeof({cpp_types.cpp_type(value_type)})>>",
            "primary_key": collection.primary_key[0],
        })

    template_ctx = {
        "namespace": namespace,
        "includes": includes,
        "collections": collections,
        "indices": indices,
        "generated_source_base_name": generated_source_base_name
    }

    # Output dir
    output_dir = Path(args.output_dir) if args.output_dir else Path("./tests/generated")
    output_dir.mkdir(parents=True, exist_ok=True)
    header_path = output_dir / f"{generated_source_base_name}.h"
    cpp_path = output_dir / f"{generated_source_base_name}.cpp"

    header_code = header_template.render(**template_ctx)
    cpp_code = cpp_template.render(**template_ctx)
    header_path.write_text(header_code)
    cpp_path.write_text(cpp_code)


    # Run clang-format on generated files using project root .clang-format
    clang_format([header_path, cpp_path])

    print(f"Generated {header_path} and {cpp_path}")

if __name__ == "__main__":
    main()
