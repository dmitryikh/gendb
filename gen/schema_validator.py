import naming

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
