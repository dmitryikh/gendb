import re

def split_namespace_class(full_name):
    parts = full_name.split('.')
    if len(parts) > 1:
        namespace = '.'.join(parts[:-1])
        class_name = parts[-1]
    else:
        namespace = ''
        class_name = full_name
    return namespace, class_name

def to_cpp_namespace(namespace):
    return namespace.replace('.', '::')

def PascalCase(name):
    # If already PascalCase, return as is
    if re.fullmatch(r'[A-Z][a-zA-Z0-9]*', name):
        return name
    return ''.join(word.capitalize() for word in name.split('_'))

def snake_case(s):
    # If already snake_case, return as is
    if re.fullmatch(r'[a-z][a-z0-9_]*', s):
        return s
    # Handle camelCase, PascalCase, and ALLCAPS
    if '_' in s:
        # If already contains underscores, split and process each part
        parts = s.split('_')
        words = []
        for part in parts:
            if part:
                words += re.findall(r'[A-Z]+(?=[A-Z][a-z0-9]|[0-9]|$)|[A-Z][a-z0-9]*|[a-z]+|[0-9]+', part)
        return '_'.join(word.lower() for word in words)
    else:
        # Split at transitions from lower to upper or upper to lower/number
        words = re.findall(r'[A-Z]+(?=[A-Z][a-z0-9]|[0-9]|$)|[A-Z][a-z0-9]*|[a-z]+|[0-9]+', s)
        return '_'.join(word.lower() for word in words)