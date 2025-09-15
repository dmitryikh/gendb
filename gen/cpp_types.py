
from typing import Optional


# FlatBuffer base types to C++ types
FLATBUF_TO_CPP = {
    "Bool": "bool",
    "Byte": "int8_t",
    "UByte": "uint8_t",
    "Short": "int16_t",
    "UShort": "uint16_t",
    "Int": "int32_t",
    "UInt": "uint32_t",
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
    "UInt": "uint32_t&",
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
    "UInt": "uint32_t",
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
    "UInt": 4,
    "Long": 8,
    "ULong": 8,
    "Float": 4,
    "Double": 8,
    "String": None,  # Variable size
    # Not supported: UType, Vector, Obj, Union, Array, Vector64
}

CPP_TYPE_SIZE = {
    "bool": 1,
    "uint8_t": 1,
    "int8_t": 1,
    "uint16_t": 2,
    "int16_t": 2,
    "uint32_t": 4,
    "int32_t": 4,
    "uint64_t": 8,
    "int64_t": 8,
    "float": 4,
    "double": 8,
}

# Default values for FlatBuffer types
FLATBUF_TYPE_DEFAULTS = {
    "Bool": "false",
    "Byte": "0",
    "UByte": "0",
    "Short": "0",
    "UShort": "0",
    "Int": "0",
    "UInt": "0",
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
    if flat_type not in FLATBUF_TO_CPP:
        raise KeyError(f"cpp_type: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TO_CPP.keys())}")
    return FLATBUF_TO_CPP[flat_type]



def ref_type(flat_type: str) -> str:
    if flat_type not in FLATBUF_TO_REF:
        raise KeyError(f"ref_type: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TO_REF.keys())}")
    return FLATBUF_TO_REF[flat_type]



def const_ref_type(flat_type: str) -> str:
    if flat_type not in FLATBUF_TO_CONST_REF:
        raise KeyError(f"const_ref_type: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TO_CONST_REF.keys())}")
    return FLATBUF_TO_CONST_REF[flat_type]


def type_size(flat_type: str) -> Optional[int]:
    if flat_type not in FLATBUF_TYPE_SIZE:
        raise KeyError(f"type_size: Unknown FlatBuffer type '{flat_type}'. Supported types: {list(FLATBUF_TYPE_SIZE.keys())}")
    return FLATBUF_TYPE_SIZE[flat_type]


def cpp_type_size(cpp_type: str) -> Optional[int]:
    if cpp_type not in CPP_TYPE_SIZE:
        raise KeyError(f"cpp_type_size: Unknown C++ type '{cpp_type}'. Supported types: {list(CPP_TYPE_SIZE.keys())}")
    return CPP_TYPE_SIZE[cpp_type]


def is_fixed_size(flat_type: str) -> bool:
    sz = type_size(flat_type)
    return sz is not None

def is_integral_type(flat_type: str) -> bool:
    return flat_type in {"Bool", "Byte", "UByte", "Short", "UShort", "Int", "UInt", "Long", "ULong"}

def is_scalar_type(flat_type: str) -> bool:
    if is_integral_type(flat_type):
        return True
    return flat_type in {"Float", "Double", "String"}
