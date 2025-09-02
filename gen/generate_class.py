import json
from pathlib import Path
from jinja2 import Environment, FileSystemLoader
import argparse

import flatc

BASE_DIR = Path(__file__).resolve().parent
TEMPLATES_DIR = BASE_DIR / Path("templates")
env = Environment(loader=FileSystemLoader(TEMPLATES_DIR), trim_blocks=True, lstrip_blocks=True)
template = env.get_template("native_class_template.h.jinja2")

TYPE_MAP = {
    "Int": "int",
    "Float": "float",
    "Bool": "bool",
    "String": "std::string"
}

def canonical_name(full_name: str) -> str:
    """
    Simplify a FlatBuffers type name by:
    1. Dropping namespaces (anything before the last dot)
    2. Removing a trailing 'Fb' suffix if present
    """
    if not full_name:
        return full_name
    base = full_name.split(".")[-1]

    if base.endswith("Fb"):
        base = base[:-2]
    return base

def parse_json_schema(schema):
    tables = []
    for table in schema.get("objects", []):
        fields = []
        for field in table["fields"]:
            f_type = TYPE_MAP.get(field["type"]["base_type"])
            fields.append({"name": field["name"], "type": f_type})
        tables.append({"name": canonical_name(table["name"]), "fields": fields})
    return tables

def generate_cpp_class(table_def):
    return template.render(table=table_def)

def main():
    parser = argparse.ArgumentParser(description="Generate C++ native classes from FlatBuffers JSON schema.")
    parser.add_argument("--fbs", type=Path, required=True, help="Path to the FlatBuffers message")
    parser.add_argument("--output-dir", "-o", type=Path, default=Path("output"), help="Directory for generated files")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    schema = flatc.get_schema(args.fbs)
    tables = parse_json_schema(schema)
    for table in tables:
        cpp_code = generate_cpp_class(table)
        output_file = args.output_dir / f"{table['name']}.h"
        with open(output_file, "w") as f:
            f.write(cpp_code)
        print(f"Generated {output_file}")

if __name__ == "__main__":
    main()
