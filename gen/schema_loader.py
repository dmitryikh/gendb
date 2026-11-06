"""Turns a parsed schema into what the code generators work with."""
from pathlib import Path

from schema_parser import EnumeratorRef, ParsedMessage, ParsedSchema, parse_schema
from schema_types import Enum, Field, FieldKind, Message
from store import Store
import base_types
import naming

SCHEMA_SUFFIX = ".gendb"


def _qualified(namespace: str, name: str) -> str:
    return f"{namespace}.{name}" if namespace else name


def _cpp_string(value: bytes) -> str:
    """An expression for a constant string_view; its size is explicit, as it can hold zero bytes."""
    if not value:
        return ""
    literal = []
    for byte in value:
        if byte in (0x22, 0x5c, 0x3f):  # " \ and ?, as of trigraphs.
            literal.append("\\" + chr(byte))
        elif 0x20 <= byte < 0x7f:
            literal.append(chr(byte))
        else:
            literal.append(f"\\{byte:03o}")
    return f'std::string_view("{"".join(literal)}", {len(value)})'


def _cpp_default(type_name: str, default, enum_cpp_type: str, underlying: str) -> str:
    if default is None:
        if enum_cpp_type:
            return "0"
        return "" if type_name == "string" else base_types.default_value(type_name)
    if enum_cpp_type:
        return f"static_cast<{underlying}>({enum_cpp_type}::{default.name})"
    if type_name == "bool":
        return "true" if default else "false"
    if type_name == "string":
        return _cpp_string(default)
    if type_name == "float":
        return f"{default!r}f"
    if type_name == "double":
        return repr(default)
    # The most negative value of a type cannot be written as a literal of that type.
    if default == -(2**63):
        return "(-9223372036854775807LL - 1)"
    suffix = "ULL" if type_name.startswith("uint") else "LL"
    return f"static_cast<{base_types.cpp_type(type_name)}>({default}{suffix})"


def _to_message(schema: ParsedSchema, message: ParsedMessage, cpp_include: str) -> Message:
    enums = {e.name: e for e in schema.enums}
    fields = []
    for f in message.fields:
        if f.type in enums:
            cpp_type = naming.to_cpp_namespace(_qualified(schema.namespace, f.type))
            underlying = base_types.cpp_type(enums[f.type].underlying)
            fields.append(Field(
                id=f.number,
                name=f.name,
                type=_qualified(schema.namespace, f.type),
                field_kind=FieldKind.ENUM,
                cpp_type=cpp_type,
                underlying_type=underlying,
                const_ref_type=cpp_type,
                ref_type=f"{cpp_type}&",
                is_fixed_size=True,
                default=_cpp_default(f.type, f.default, cpp_type, underlying),
                has_default=f.default is not None,
                deprecated=f.deprecated,
            ))
        else:
            fields.append(Field(
                id=f.number,
                name=f.name,
                type=f.type,
                field_kind=FieldKind.SCALAR,
                cpp_type=base_types.cpp_type(f.type),
                underlying_type=None,
                const_ref_type=base_types.const_ref_type(f.type),
                ref_type=base_types.ref_type(f.type),
                is_fixed_size=base_types.is_fixed_size(f.type),
                default=_cpp_default(f.type, f.default, "", ""),
                has_default=f.default is not None,
                deprecated=f.deprecated,
            ))
    return Message(full_name=_qualified(schema.namespace, message.name), cpp_include=cpp_include,
                   fields=fields)


def load_schema_to_store(store: Store, path: Path):
    """Raises SchemaError if the file is not a valid schema."""
    schema = parse_schema(Path(path).read_bytes(), str(path))
    for enum in schema.enums:
        store.add_enum(Enum(
            full_name=_qualified(schema.namespace, enum.name),
            underlying_type=enum.underlying,
            values={v.name: v.value for v in enum.values},
        ))
    cpp_include = f"{Path(path).name}.h"
    for message in schema.messages:
        store.add_message(_to_message(schema, message, cpp_include))
