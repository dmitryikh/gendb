from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Union
import naming

class FieldKind(Enum):
    SCALAR = 1
    ENUM = 2
    MESSAGE = 3

# Properties for a field in Flatbuffer message.
@dataclass
class Field:
    id: int
    name: str
    type: str        # e.g., "int", "string", "gendb.tests.ConfigFb"
    field_kind: FieldKind
    cpp_type: str
    underlying_type: str # if FieldKind.ENUM, the c++ underlying type of the enum
    const_ref_type: str # type in c++, which is used to pass the field value as a const reference
    ref_type: str # type in c++, which is used to pass the field value as a reference (for modifications)
    is_fixed_size: bool # if the size of the field is known at compile-time
    optional: bool = True
    default: Optional[Union[int, float, str]] = None

@dataclass
class Enum:
    full_name: str
    underlying_type: str # e.g., "Int", "UInt", etc.
    values: dict[str, int]  # e.g., {"LABEL1": 1, "LABEL2": 2}

    def name(self) -> str:
        return naming.split_namespace_class(self.full_name)[1]

@dataclass
class Index:
    name: str
    collection: str
    field: str

@dataclass
class Message:
    full_name: str                  # e.g., "gendb.main.AccountFb"
    cpp_include: str               # e.g., "tests/generated/AccountFb.h"
    fields: List[Field] = field(default_factory=list)

    def name(self) -> str:
        return naming.split_namespace_class(self.full_name)[1]

@dataclass
class Collection:
    name: str                  # e.g., "accounts"
    type: str                  # link to Message.name
    primary_key: List[str]     # link to Message.fields[i].name(s)

@dataclass
class Database:
    name: str                  # e.g., "main"
    namespace: str             # e.g., "gendb.main"
    collections: List[Collection] = field(default_factory=list)
    indices: List[Index] = field(default_factory=list)