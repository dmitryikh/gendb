
from typing import List, Optional, Dict
from fb_types import Message, Collection, Field, Index, Enum

class Store:
    """
    Central storage for the DB schema, types, and query info.
    Provides helper methods to access Message/Field/Collection/Enum data easily.
    """

    def __init__(self):
        self.messages: Dict[str, Message] = {}
        self.collections: Dict[str, Collection] = {}
        self.indices: Dict[str, Index] = {}
        self.enums: Dict[str, Enum] = {}

    def add_message(self, t: Message):
        if t.full_name in self.messages:
            raise RuntimeError(f'{t.full_name} message already exists in store')
        self.messages[t.full_name] = t

    # --- Enum helpers ---
    def add_enum(self, enum: Enum):
        if enum.full_name in self.enums:
            raise RuntimeError(f"{enum.full_name} enum already exists in store")
        self.enums[enum.full_name] = enum

    def get_enum(self, enum_name: str) -> Optional[Enum]:
        return self.enums.get(enum_name)

    def list_enums(self) -> List[str]:
        return list(self.enums.keys())

    def get_message(self, full_name: str) -> Optional[Message]:
        if full_name not in self.messages:
            raise RuntimeError(f"{full_name} message not found in store")
        return self.messages.get(full_name)

    def try_get_message(self, full_name: str) -> Optional[Message]:
        return self.messages.get(full_name)

    def get_field(self, message: str, field: str) -> Optional[Field]:
        t = self.get_message(message)
        if not t:
            return None
        for f in t.fields:
            if f.name == field:
                return f
        return None

    def list_fields(self, message: str) -> List[str]:
        t = self.get_message(message)
        if not t:
            return []
        return [f.name for f in t.fields]

    # --- Collection helpers ---
    def add_collection(self, collection: Collection):
        self.collections[collection.name] = collection

    def get_collection(self, collection_name: str) -> Optional[Collection]:
        return self.collections.get(collection_name)

    def get_collection_primary_keys(self, collection_name: str) -> Optional[List[str]]:
        collection = self.get_collection(collection_name)
        if collection:
            return collection.primary_key
        return None

    # --- Index helpers ---
    def add_index(self, index: Index):
        self.indices[index.name] = index

    def get_index(self, index_name: str) -> Optional[Index]:
        return self.indices.get(index_name)

    def list_indices(self) -> List[str]:
        return list(self.indices.keys())

    # Optional: convenience method to list all messages
    def list_messages(self) -> List[str]:
        return list(self.messages.keys())

    def list_collections(self) -> List[str]:
        return list(self.collections.keys())