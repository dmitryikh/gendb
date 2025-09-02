from types import Type, Table
class Store:
    """
    Central storage for the DB schema, types, and query info.
    Provides helper methods to access Type/Field/Table data easily.
    """

    def __init__(self):
        self.fb_tables: Dict[str, FbTable] = {}
        self.collections: Dict[str, Collection] = {}

    def add_fb_table(self, t: FbTable):
        if t.name in self.fb_tables:
            raise RuntimeError(f'{t.name} FB table already exists in store')
        self.types[t.name] = t

    def get_fb_table(self, type_name: str) -> Optional[Type]:
        return self.types.get(type_name)

    def get_field(self, type_name: str, field_name: str) -> Optional[Field]:
        t = self.get_type(type_name)
        if not t:
            return None
        for f in t.fields:
            if f.name == field_name:
                return f
        return None

    # --- Table helpers ---
    def add_table(self, table: Table):
        self.tables[table.name] = table

    def get_table(self, table_name: str) -> Optional[Table]:
        return self.tables.get(table_name)

    # --- Query helpers ---
    def add_query(self, query: Query):
        self.queries.append(query)

    def get_query(self, func_name: str) -> Optional[Query]:
        for q in self.queries:
            if q.func_name == func_name:
                return q
        return None

    # Optional: convenience method to list all types/tables
    def list_types(self) -> List[str]:
        return list(self.types.keys())

    def list_tables(self) -> List[str]:
        return list(self.tables.keys())