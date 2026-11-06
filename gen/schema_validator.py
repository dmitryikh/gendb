import naming

# Must match gendb::kMaxFieldId in lib/gendb/message.h: 255 bitmap bytes per storage class.
MAX_FIELD_ID = 255 * 8


def validate_field_ids(store):
	errors = []
	for msg_name, msg in store.messages.items():
		for field in msg.fields:
			if not 1 <= field.id <= MAX_FIELD_ID:
				errors.append(f"Field '{field.name}' in message '{msg_name}' has ID {field.id}, outside 1..{MAX_FIELD_ID}")
	return errors


def validate_collections(store):
	errors = []
	for col_name, col in store.collections.items():
		for pk in col.primary_key:
			field = store.try_get_field(col.type, pk)
			if field is not None and field.deprecated:
				errors.append(f"Primary key '{pk}' of collection '{col_name}' is deprecated")
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
	return errors


def validate_names(store):
	errors = []
	# Validate Message names
	for msg_name, msg in store.messages.items():
		expected = naming.PascalCase(naming.split_namespace_class(msg_name)[1])
		if msg.name() != expected:
			errors.append(f"Message name '{msg.name()}' should be PascalCase ('{expected}')")
		# Validate fields
		for field in msg.fields:
			expected_field = naming.snake_case(field.name)
			if field.name != expected_field:
				errors.append(f"Field name '{field.name}' in message '{msg.name()}' should be snake_case ('{expected_field}')")
	# Validate Enum names
	for enum_name, enum in store.enums.items():
		expected_enum = naming.PascalCase(naming.split_namespace_class(enum_name)[1])
		if enum.name() != expected_enum:
			errors.append(f"Enum name '{enum.name()}' should be PascalCase ('{expected_enum}')")
	return errors
