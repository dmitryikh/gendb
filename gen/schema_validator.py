from schema_types import FieldKind

# Generated in the same namespace as a database's message types.
RESERVED_COLLECTION_TYPES = {"Db", "DbOptions", "Snapshot", "ScopedWrite", "ChangeHandlers", "CollectionId", "IndexId",
                             "SequenceMetadataId"}


def _not_a_key(field):
	return field.repeated or field.field_kind == FieldKind.MESSAGE


def validate_collections(store):
	errors = []
	for col_name, col in store.collections.items():
		if col.type.split(".")[-1] in RESERVED_COLLECTION_TYPES:
			errors.append(f"Message '{col.type}' of collection '{col_name}' has a name the generated code needs")
		for pk in col.primary_key:
			field = store.try_get_field(col.type, pk)
			if field is not None and field.deprecated:
				errors.append(f"Primary key '{pk}' of collection '{col_name}' is deprecated")
			if field is not None and _not_a_key(field):
				errors.append(f"Primary key '{pk}' of collection '{col_name}' is a message or repeated field")
	for idx_name, idx in store.indices.items():
		col = store.get_collection(idx.collection)
		if not idx.fields:
			errors.append(f"Index '{idx_name}' has no fields")
		if len(set(idx.fields)) != len(idx.fields):
			errors.append(f"Index '{idx_name}' lists a field twice")
		for name in idx.fields:
			field = store.try_get_field(col.type, name)
			if field is None:
				errors.append(f"Index '{idx_name}' has unknown field '{name}'")
			elif field.deprecated:
				errors.append(f"Field '{name}' of index '{idx_name}' is deprecated")
			elif _not_a_key(field):
				errors.append(f"Field '{name}' of index '{idx_name}' is a message or repeated field")
	return errors
