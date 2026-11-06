
from typing import Optional, NamedTuple


class TypeInfo(NamedTuple):
    """Type information for a schema type in C++."""
    cpp_type: str
    ref_type: str
    const_ref_type: str
    size: Optional[int]  # None for variable-size types
    default_value: str


# The schema language's types, as C++.
TYPES = {
    "bool": TypeInfo("bool", "bool&", "bool", 1, "false"),
    "int8": TypeInfo("int8_t", "int8_t&", "int8_t", 1, "0"),
    "uint8": TypeInfo("uint8_t", "uint8_t&", "uint8_t", 1, "0"),
    "int16": TypeInfo("int16_t", "int16_t&", "int16_t", 2, "0"),
    "uint16": TypeInfo("uint16_t", "uint16_t&", "uint16_t", 2, "0"),
    "int32": TypeInfo("int32_t", "int32_t&", "int32_t", 4, "0"),
    "uint32": TypeInfo("uint32_t", "uint32_t&", "uint32_t", 4, "0"),
    "int64": TypeInfo("int64_t", "int64_t&", "int64_t", 8, "0LL"),
    "uint64": TypeInfo("uint64_t", "uint64_t&", "uint64_t", 8, "0ULL"),
    "float": TypeInfo("float", "float&", "float", 4, "0.0f"),
    "double": TypeInfo("double", "double&", "double", 8, "0.0"),
    "string": TypeInfo("std::string", "std::string_view", "std::string_view", None, '""'),
}

# C++ type sizes for direct lookup
CPP_TYPE_SIZE = {
    "bool": 1, "uint8_t": 1, "int8_t": 1, "uint16_t": 2, "int16_t": 2,
    "uint32_t": 4, "int32_t": 4, "uint64_t": 8, "int64_t": 8,
    "float": 4, "double": 8,
}

def default_value(type_name: str) -> str:
    """Get the default value for a schema type."""
    if type_name not in TYPES:
        return "{}"
    return TYPES[type_name].default_value


def cpp_type(type_name: str) -> str:
    """Get the C++ type for a schema type."""
    if type_name not in TYPES:
        raise KeyError(f"cpp_type: Unknown schema type '{type_name}'. Supported types: {list(TYPES.keys())}")
    return TYPES[type_name].cpp_type


def ref_type(type_name: str) -> str:
    """Get the reference type for a schema type."""
    if type_name not in TYPES:
        raise KeyError(f"ref_type: Unknown schema type '{type_name}'. Supported types: {list(TYPES.keys())}")
    return TYPES[type_name].ref_type


def const_ref_type(type_name: str) -> str:
    """Get the const reference type for a schema type."""
    if type_name not in TYPES:
        raise KeyError(f"const_ref_type: Unknown schema type '{type_name}'. Supported types: {list(TYPES.keys())}")
    return TYPES[type_name].const_ref_type


def type_size(type_name: str) -> Optional[int]:
    """Get the size in bytes for a schema type. Returns None for variable-size types."""
    if type_name not in TYPES:
        raise KeyError(f"type_size: Unknown schema type '{type_name}'. Supported types: {list(TYPES.keys())}")
    return TYPES[type_name].size


def cpp_type_size(cpp_type: str) -> Optional[int]:
    """Get the size in bytes for a C++ type."""
    if cpp_type not in CPP_TYPE_SIZE:
        raise KeyError(f"cpp_type_size: Unknown C++ type '{cpp_type}'. Supported types: {list(CPP_TYPE_SIZE.keys())}")
    return CPP_TYPE_SIZE[cpp_type]


def is_fixed_size(type_name: str) -> bool:
    """Check if a schema type has a fixed size."""
    sz = type_size(type_name)
    return sz is not None
