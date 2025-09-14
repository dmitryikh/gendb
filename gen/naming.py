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
    return '_'.join(word.lower() for word in re.findall(r'[A-Z][a-z]*', s))