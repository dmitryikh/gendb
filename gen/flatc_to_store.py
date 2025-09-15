
import os
from store import Store
import flatc
from pathlib import Path
import cpp_types
from fb_types import Message, Field, Enum, FieldKind
import naming

def __enum_from_flatc_obj(obj):
    return Enum(
        full_name=obj['name'],
        underlying_type=obj["underlying_type"]["base_type"],
        values={v["name"]: v["value"] for v in obj["values"]}
    )

def __is_enum_type(field, enums: list[Enum]) -> bool:
        base_type = field["type"]["base_type"]
        idx_enum = field["type"].get("index", -1)
        if cpp_types.is_integral_type(base_type) and idx_enum >= 0:
            # Could be enum or table, check if index refers to enum
            if idx_enum < len(enums):
                return True
        return False

def __message_from_flatc_obj(obj, enums: list[Enum], include_prefix=""):
    # obj: dict from flatc schema
    fields = []
    for f in obj["fields"]:
        default = f.get("default_integer", 0)
        if __is_enum_type(f, enums):
            field_kind = FieldKind.ENUM
            type_ = enums[f["type"]["index"]].full_name
        elif cpp_types.is_scalar_type(f["type"]["base_type"]):
            field_kind = FieldKind.SCALAR
            type_ = f["type"]["base_type"]
        else:
            raise ValueError(f"Unknown field type: {f['type']}")

        if field_kind == FieldKind.ENUM:
            cpp_type = naming.to_cpp_namespace(type_)
            const_ref_type = cpp_type
            ref_type = f'{cpp_type}&'
            is_fixed = True
            underlying_type = cpp_types.cpp_type(enums[f["type"]["index"]].underlying_type)
        elif field_kind == FieldKind.SCALAR:
            cpp_type = cpp_types.cpp_type(type_)
            # TODO: support overwritten default values.
            default = cpp_types.default_value(type_)
            const_ref_type = cpp_types.const_ref_type(type_)
            ref_type = cpp_types.ref_type(type_)
            is_fixed = cpp_types.is_fixed_size(type_)
            underlying_type = None
            enum_type = None

        fields.append(Field(
            id = f.get("id", 0),
            name=f["name"],
            type=type_,
            field_kind=field_kind,
            cpp_type=cpp_type,
            underlying_type=underlying_type,
            const_ref_type=const_ref_type,
            ref_type=ref_type,
            is_fixed_size=is_fixed,
            optional=True,  # All fields are optional
            default=default
        ))

    # Sort fields by id
    fields = sorted(fields, key=lambda field: getattr(field, "id", 0))

    cpp_include = os.path.basename(obj["declaration_file"]) + '.h'
    return Message(
        full_name=obj['name'],
        cpp_include=cpp_include,
        fields=fields
    )

def load_fbs_to_store(store: Store, fbs_path: Path):
    schema = flatc.get_schema(fbs_path)
    enums = []
    for enum in schema.get("enums", []):
        e = __enum_from_flatc_obj(enum)
        store.add_enum(e)
        enums.append(e)

    for obj in schema["objects"]:
        store.add_message(__message_from_flatc_obj(obj, enums))
