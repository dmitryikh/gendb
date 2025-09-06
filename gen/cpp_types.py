
from typing import Optional


# FlatBuffer base types to C++ types
FLATBUF_TO_CPP = {
    "Bool": "bool",
    "Byte": "int8_t",
    "UByte": "uint8_t",
    "Short": "int16_t",
    "UShort": "uint16_t",
    "Int": "int32_t",
    "Uint": "uint32_t",
    "Long": "int64_t",
    "ULong": "uint64_t",
    "Float": "float",
    "Double": "double",
    "String": "std::string",
    # Not supported: UType, Vector, Obj, Union, Array, Vector64
}


# Reference types
FLATBUF_TO_REF = {
    "Bool": "bool&",
    "Byte": "int8_t&",
    "UByte": "uint8_t&",
    "Short": "int16_t&",
    "UShort": "uint16_t&",
    "Int": "int32_t&",
    "Uint": "uint32_t&",
    "Long": "int64_t&",
    "ULong": "uint64_t&",
    "Float": "float&",
    "Double": "double&",
    "String": "std::string_view",
    # Not supported: UType, Vector, Obj, Union, Array, Vector64
}


# Const reference types
FLATBUF_TO_CONST_REF = {
    "Bool": "bool",
    "Byte": "int8_t",
    "UByte": "uint8_t",
    "Short": "int16_t",
    "UShort": "uint16_t",
    "Int": "int32_t",
    "Uint": "uint32_t",
    "Long": "int64_t",
    "ULong": "uint64_t",
    "Float": "float",
    "Double": "double",
    "String": "std::string_view",
    # Not supported: UType, Vector, Obj, Union, Array, Vector64
}


# Sizes (in bytes)
FLATBUF_TYPE_SIZE = {
    "Bool": 1,
    "Byte": 1,
    "UByte": 1,
    "Short": 2,
    "UShort": 2,
    "Int": 4,
    "Uint": 4,
    "Long": 8,
    "ULong": 8,
    "Float": 4,
    "Double": 8,
    "String": None,  # Variable size
    # Not supported: UType, Vector, Obj, Union, Array, Vector64
}

# Default values for FlatBuffer types
FLATBUF_TYPE_DEFAULTS = {
    "Bool": "false",
    "Byte": "0",
    "UByte": "0",
    "Short": "0",
    "UShort": "0",
    "Int": "0",
    "Uint": "0",
    "Long": "0LL",
    "ULong": "0ULL",
    "Float": "0.0f",
    "Double": "0.0",
    "String": '""',
    # Not supported: UType, Vector, Obj, Union, Array, Vector64
}

def default_value(flat_type: str) -> str:
    return FLATBUF_TYPE_DEFAULTS.get(flat_type, "{}")
# cpp_types.py
# Utilities for mapping FlatBuffer types to C++ types, reference types, const reference types, and sizes.


def cpp_type(flat_type: str) -> str:
    return FLATBUF_TO_CPP.get(flat_type, flat_type)


def ref_type(flat_type: str) -> str:
    return FLATBUF_TO_REF.get(flat_type, flat_type)


def const_ref_type(flat_type: str) -> str:
    return FLATBUF_TO_CONST_REF.get(flat_type, f"const {cpp_type(flat_type)}&")


def type_size(flat_type: str) -> Optional[int]:
    return FLATBUF_TYPE_SIZE.get(flat_type)


def is_fixed_size(flat_type: str) -> bool:
    sz = type_size(flat_type)
    return sz is not None
