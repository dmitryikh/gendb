
from typing import List, Optional, Dict
from fb_types import Message, Collection, Field

class Store:
    """
    Central storage for the DB schema, types, and query info.
    Provides helper methods to access Message/Field/Collection data easily.
    """

    def __init__(self):
        self.messages: Dict[str, Message] = {}
        self.collections: Dict[str, Collection] = {}

    def add_message(self, t: Message):
        if t.name in self.messages:
            raise RuntimeError(f'{t.name} message already exists in store')
        self.messages[t.name] = t

    def get_message(self, type_name: str) -> Optional[Message]:
        return self.messages.get(type_name)

    def get_field(self, message: str, field: str) -> Optional[Field]:
        t = self.get_message(message)
        if not t:
            return None
        for f in t.fields:
            if f.name == field:
                return f
        return None

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

    # Optional: convenience method to list all messages
    def list_messages(self) -> List[str]:
        return list(self.messages.keys())

    def list_collections(self) -> List[str]:
        return list(self.collections.keys())