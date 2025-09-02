from dataclasses import dataclass, field
from typing import List, Optional, Union

# Properties for a field in Flatbuffer message.
@dataclass
class FbField:
    name: str
    type: str        # e.g., "int", "string", "ConfigFb"
    is_array: bool
    optional: bool = False
    default: Optional[Union[int, float, str]] = None

@dataclass
class FbTable:
    name: str                  # e.g., "AccountFb"
    fields: List[Field] = field(default_factory=list)

@dataclass
class Collection:
    name: str                  # e.g., "accounts"
    type: str             # link to Type.name
    primary_key: str  # link to Type.fields[i].name