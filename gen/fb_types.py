from dataclasses import dataclass, field
from typing import List, Optional, Union

# Properties for a field in Flatbuffer message.
@dataclass
class Field:
    name: str
    type: str        # e.g., "int", "string", "ConfigFb"
    is_array: bool
    optional: bool = False
    default: Optional[Union[int, float, str]] = None

@dataclass
class Message:
    name: str                  # e.g., "AccountFb"
    namespace: str             # e.g., "gendb.main"
    cpp_include: str               # e.g., "tests/generated/AccountFb.h"
    fields: List[Field] = field(default_factory=list)

@dataclass
class Collection:
    name: str                  # e.g., "accounts"
    type: str                  # link to Type.name
    primary_key: List[str]     # link to Type.fields[i].name(s)

@dataclass
class Database:
    name: str                  # e.g., "main"
    namespace: str          # e.g., "gendb.main"
    collections: List[Collection] = field(default_factory=list)