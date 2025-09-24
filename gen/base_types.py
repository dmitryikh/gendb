
from typing import Optional, NamedTuple


class TypeInfo(NamedTuple):
    """Type information for FlatBuffer to C++ mapping."""
    cpp_type: str
    ref_type: str
    const_ref_type: str
    size: Optional[int]  # None for variable-size types
    default_value: str


# Consolidated FlatBuffer type mappings
# Note: UType, Vector, Obj, Union, Array, Vector64 are not supported
FLATBUF_TYPES = {
    "Bool": TypeInfo("bool", "bool&", "bool", 1, "false"),
    "Byte": TypeInfo("int8_t", "int8_t&", "int8_t", 1, "0"),
    "UByte": TypeInfo("uint8_t", "uint8_t&", "uint8_t", 1, "0"),
    "Short": TypeInfo("int16_t", "int16_t&", "int16_t", 2, "0"),
    "UShort": TypeInfo("uint16_t", "uint16_t&", "uint16_t", 2, "0"),
    "Int": TypeInfo("int32_t", "int32_t&", "int32_t", 4, "0"),
    "UInt": TypeInfo("uint32_t", "uint32_t&", "uint32_t", 4, "0"),
    "Long": TypeInfo("int64_t", "int64_t&", "int64_t", 8, "0LL"),
    "ULong": TypeInfo("uint64_t", "uint64_t&", "uint64_t", 8, "0ULL"),
    "Float": TypeInfo("float", "float&", "float", 4, "0.0f"),
    "Double": TypeInfo("double", "double&", "double", 8, "0.0"),
    "String": TypeInfo("std::string", "std::string_view", "std::string_view", None, '""'),
}

# C++ type sizes for direct lookup
CPP_TYPE_SIZE = {
    "bool": 1, "uint8_t": 1, "int8_t": 1, "uint16_t": 2, "int16_t": 2,
    "uint32_t": 4, "int32_t": 4, "uint64_t": 8, "int64_t": 8,
    "float": 4, "double": 8,
}

def default_value(flat_type: str) -> str:
    """Get the default value for a FlatBuffer type."""
    if flat_type not in FLATBUF_TYPES:
        return "{}"
    return FLATBUF_TYPES[flat_type].default_value


def cpp_type(flat_type: str) -> str:
    """Get the C++ type for a FlatBuffer type."""
    if flat_type not in FLATBUF_TYPES:
        raise KeyError(f"cpp_type: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TYPES.keys())}")
    return FLATBUF_TYPES[flat_type].cpp_type


def ref_type(flat_type: str) -> str:
    """Get the reference type for a FlatBuffer type."""
    if flat_type not in FLATBUF_TYPES:
        raise KeyError(f"ref_type: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TYPES.keys())}")
    return FLATBUF_TYPES[flat_type].ref_type


def const_ref_type(flat_type: str) -> str:
    """Get the const reference type for a FlatBuffer type."""
    if flat_type not in FLATBUF_TYPES:
        raise KeyError(f"const_ref_type: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TYPES.keys())}")
    return FLATBUF_TYPES[flat_type].const_ref_type


def type_size(flat_type: str) -> Optional[int]:
    """Get the size in bytes for a FlatBuffer type. Returns None for variable-size types."""
    if flat_type not in FLATBUF_TYPES:
        raise KeyError(f"type_size: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TYPES.keys())}")
    return FLATBUF_TYPES[flat_type].size


def cpp_type_size(cpp_type: str) -> Optional[int]:
    """Get the size in bytes for a C++ type."""
    if cpp_type not in CPP_TYPE_SIZE:
        raise KeyError(f"cpp_type_size: Unknown C++ type '{cpp_type}'. Supported types: {list(CPP_TYPE_SIZE.keys())}")
    return CPP_TYPE_SIZE[cpp_type]


def is_fixed_size(flat_type: str) -> bool:
    """Check if a FlatBuffer type has a fixed size."""
    sz = type_size(flat_type)
    return sz is not None


def is_integral_type(flat_type: str) -> bool:
    """Check if a FlatBuffer type is an integral type."""
    return flat_type in {"Bool", "Byte", "UByte", "Short", "UShort", "Int", "UInt", "Long", "ULong"}


def is_scalar_type(flat_type: str) -> bool:
    """Check if a FlatBuffer type is a scalar type (integral, float, or string)."""
    if is_integral_type(flat_type):
        return True
    return flat_type in {"Float", "Double", "String"}
