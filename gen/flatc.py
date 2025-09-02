import tempfile
import subprocess
import json
from typing import Union
import argparse
from pathlib import Path

# Content of https://github.com/google/flatbuffers/blob/master/reflection/reflection.fbs
REFLECTION_FBS = r"""
// This schema defines objects that represent a parsed schema, like
// the binary version of a .fbs file.
// This could be used to operate on unknown FlatBuffers at runtime.
// It can even ... represent itself (!)

namespace reflection;

// These must correspond to the enum in idl.h.
enum BaseType : byte {
    None,
    UType,
    Bool,
    Byte,
    UByte,
    Short,
    UShort,
    Int,
    UInt,
    Long,
    ULong,
    Float,
    Double,
    String,
    Vector,
    Obj,     // Used for tables & structs.
    Union,
    Array,
    Vector64,

    // Add any new type above this value.
    MaxBaseType
}

table Type {
    base_type:BaseType;
    element:BaseType = None;  // Only if base_type == Vector
                              // or base_type == Array.
    index:int = -1;  // If base_type == Object, index into "objects" below.
                     // If base_type == Union, UnionType, or integral derived
                     // from an enum, index into "enums" below.
                     // If base_type == Vector && element == Union or UnionType.
    fixed_length:uint16 = 0;  // Only if base_type == Array.
    /// The size (octets) of the `base_type` field.
    base_size:uint = 4; // 4 Is a common size due to offsets being that size.
    /// The size (octets) of the `element` field, if present.
    element_size:uint = 0;
}

table KeyValue {
    key:string (required, key);
    value:string;
}

table EnumVal {
    name:string (required);
    value:long (key);
    object:Object (deprecated);
    union_type:Type;
    documentation:[string];
    attributes:[KeyValue];
}

table Enum {
    name:string (required, key);
    values:[EnumVal] (required);  // In order of their values.
    is_union:bool = false;
    underlying_type:Type (required);
    attributes:[KeyValue];
    documentation:[string];
    /// File that this Enum is declared in.
    declaration_file: string;
}

table Field {
    name:string (required, key);
    type:Type (required);
    id:ushort;
    offset:ushort;  // Offset into the vtable for tables, or into the struct.
    default_integer:long = 0;
    default_real:double = 0.0;
    deprecated:bool = false;
    required:bool = false;
    key:bool = false;
    attributes:[KeyValue];
    documentation:[string];
    optional:bool = false;
    /// Number of padding octets to always add after this field. Structs only.
    padding:uint16 = 0; 
    /// If the field uses 64-bit offsets.
    offset64:bool = false;
}

table Object {  // Used for both tables and structs.
    name:string (required, key);
    fields:[Field] (required);  // Sorted.
    is_struct:bool = false;
    minalign:int;
    bytesize:int;  // For structs.
    attributes:[KeyValue];
    documentation:[string];
    /// File that this Object is declared in.
    declaration_file: string;
}

table RPCCall {
    name:string (required, key);
    request:Object (required);      // must be a table (not a struct)
    response:Object (required);     // must be a table (not a struct)
    attributes:[KeyValue];
    documentation:[string];
}

table Service {
    name:string (required, key);
    calls:[RPCCall];
    attributes:[KeyValue];
    documentation:[string];
    /// File that this Service is declared in.
    declaration_file: string;
}

/// New schema language features that are not supported by old code generators.
enum AdvancedFeatures : ulong (bit_flags) {
    AdvancedArrayFeatures,
    AdvancedUnionFeatures,
    OptionalScalars,
    DefaultVectorsAndStrings,
}

/// File specific information.
/// Symbols declared within a file may be recovered by iterating over all
/// symbols and examining the `declaration_file` field.
table SchemaFile {
  /// Filename, relative to project root.
  filename:string (required, key);
  /// Names of included files, relative to project root.
  included_filenames:[string];
}

table Schema {
    objects:[Object] (required);    // Sorted.
    enums:[Enum] (required);        // Sorted.
    file_ident:string;
    file_ext:string;
    root_table:Object;
    services:[Service];             // Sorted.
    advanced_features:AdvancedFeatures;
    /// All the files used in this compilation. Files are relative to where
    /// flatc was invoked.
    fbs_files:[SchemaFile];         // Sorted.
}

root_type Schema;

file_identifier "BFBS";
file_extension "bfbs";
"""

def get_schema(fbs_file: Union[str, Path]) -> dict:
    """
    Obtains schema for the .fbs file by using flatc calls.
    Essentially, it does the calls similar to:
        flatc --schema --binary -o tests/schemas/ tests/schemas/account.fbs
        flatc --json --strict-json -o tests/schemas third_party/reflection.fbs  -- tests/schemas/account.bfbs
    
    :param fbs_file: path to .fbs file
    :return: dict with json schema
    """
    fbs_file = Path(fbs_file).resolve()

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)

        # Save reflection.fbs to a temp file.
        reflection_fbs_path = tmpdir / "reflection.fbs"
        reflection_fbs_path.write_text(REFLECTION_FBS, encoding="utf-8")

        # Get the schema in binary format (.bfbs).
        subprocess.run(
            ["flatc", "--schema", "--binary", "-o", str(tmpdir), str(fbs_file)],
            check=True
        )

        bfbs_file = tmpdir / (fbs_file.stem + ".bfbs")

        # Convert to JSON.
        subprocess.run(
            ["flatc", "--json", "--strict-json", "-o", str(tmpdir), str(reflection_fbs_path), "--", str(bfbs_file)],
            check=True
        )

        json_file = tmpdir / (fbs_file.stem + ".json")

        # Read and return.
        with open(json_file, "r", encoding="utf-8") as f:
            return json.load(f)

def main():
    parser = argparse.ArgumentParser(
        description="Performs actions on fbs. Wrapper for flatc for simplicity."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Example operation: dump-json
    dump_json_parser = subparsers.add_parser(
        "dump-schema", help="Generate JSON schema from an .fbs file"
    )
    dump_json_parser.add_argument("fbs_file", type=Path, help="Path to the .fbs file")

    args = parser.parse_args()

    if args.command == "dump-schema":
        schema = get_schema(args.fbs_file)
        print(json.dumps(schema, indent=2))


if __name__ == "__main__":
    main()
