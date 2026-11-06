"""Parses the schema language, in which a mistake against the evolution rules is a syntax error.

    namespace app;

    enum Direction : uint32 {
      kUnknown = 0;
      kBuy = 1;
    }

    message Position {
      1 position_id: int32;
      2 instrument: string = "EURUSD";
      3 direction: Direction = kBuy;
      4 old_volume: int32 deprecated;
    }

A field states its number, and numbers go up by one from 1 in the order written. So the only place
for a new field is the end, a field cannot be removed without leaving a gap (it is marked `deprecated`
instead), and fields cannot be reordered. Enumerators follow the same rule from 0.

What syntax cannot see is what a file used to say, such as a field whose type changed:
schema_evolution.py compares versions.

A default is what a getter returns for a field that is not set. It is not stored, and it does not make
the field set: has_x() stays false.
"""
import math
import re
from dataclasses import dataclass, field as dataclass_field
from typing import List, Optional, Union

# Must match gendb::kMaxFieldId in lib/gendb/message.h: 255 bitmap bytes per storage class.
MAX_FIELD_ID = 255 * 8

INT_RANGES = {
    "int8": (-(2**7), 2**7 - 1),
    "uint8": (0, 2**8 - 1),
    "int16": (-(2**15), 2**15 - 1),
    "uint16": (0, 2**16 - 1),
    "int32": (-(2**31), 2**31 - 1),
    "uint32": (0, 2**32 - 1),
    "int64": (-(2**63), 2**63 - 1),
    "uint64": (0, 2**64 - 1),
}
BASE_TYPES = {"bool", "float", "double", "string", *INT_RANGES}

CPP_KEYWORDS = frozenset("""
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
    char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
    co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern
    false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr
    operator or or_eq private protected public register reinterpret_cast requires return short signed
    sizeof static static_assert static_cast struct switch template this thread_local throw true try
    typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq
""".split())
# Macros of common C and POSIX headers, which would break any code generated with these names.
CPP_MACROS = frozenset("""
    assert errno stdin stdout stderr linux unix major minor NULL EOF TRUE FALSE OVERFLOW UNDERFLOW
    DOMAIN RAND_MAX INFINITY NAN BUFSIZ
""".split())
# Field names whose generated members would clash with what every message class has.
RESERVED_FIELD_NAMES = frozenset({"field", "view", "has_field", "field_count"})
# Generated next to a message `X`, so a message of such a name could clash with them.
GENERATED_MESSAGE_SUFFIXES = ("PatchBuilder", "Builder", "Change", "Key")

MAX_NUMBER_LENGTH = 400
FLOAT_MAX = 3.4028234663852886e38
FLOAT_TRUE_MIN = 1.401298464324817e-45

_MESSAGE_NAME = re.compile(r"[A-Z][A-Za-z0-9]*\Z")
_FIELD_NAME = re.compile(r"[a-z][a-z0-9]*(_[a-z0-9]+)*\Z")
_ENUMERATOR_NAME = re.compile(r"[A-Za-z][A-Za-z0-9]*(_[A-Za-z0-9]+)*\Z")
_NAMESPACE_PART = re.compile(r"[a-z][a-z0-9]*(_[a-z0-9]+)*\Z")
_IDENTIFIER_START = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")
_IDENTIFIER_REST = _IDENTIFIER_START | set("0123456789")
_DIGITS = set("0123456789")
_NUMBER = re.compile(r"-?(0[xX][0-9a-fA-F]+|[0-9]+(\.[0-9]*)?([eE][+-]?[0-9]+)?)")
_ESCAPES = {"\\": b"\\", '"': b'"', "n": b"\n", "r": b"\r", "t": b"\t", "0": b"\0"}


class SchemaError(Exception):
    """What is wrong with a schema, and where."""

    def __init__(self, message: str, path: str = "", line: int = 0, column: int = 0):
        self.message = message
        self.path = path
        self.line = line
        self.column = column
        super().__init__(f"{path or '<schema>'}:{line}:{column}: {message}")


@dataclass(frozen=True)
class EnumeratorRef:
    """A default that names an enumerator."""
    name: str


Default = Union[bool, int, float, bytes, EnumeratorRef]


@dataclass
class ParsedField:
    number: int
    name: str
    type: str
    default: Optional[Default] = None
    deprecated: bool = False


@dataclass
class ParsedEnumerator:
    name: str
    value: int


@dataclass
class ParsedEnum:
    name: str
    underlying: str
    values: List[ParsedEnumerator] = dataclass_field(default_factory=list)


@dataclass
class ParsedMessage:
    name: str
    fields: List[ParsedField] = dataclass_field(default_factory=list)


@dataclass
class ParsedSchema:
    namespace: str = ""
    enums: List[ParsedEnum] = dataclass_field(default_factory=list)
    messages: List[ParsedMessage] = dataclass_field(default_factory=list)


@dataclass
class _Token:
    kind: str  # "name", "int", "float", "string", "punctuation" or "end".
    value: object
    line: int
    column: int
    text: str


class _Lexer:
    def __init__(self, text: str, path: str):
        self.text = text
        self.path = path
        self.at = 0
        self.line = 1
        self.line_start = 0

    def _error(self, message: str, at: Optional[int] = None) -> SchemaError:
        at = self.at if at is None else at
        return SchemaError(message, self.path, self.line, at - self.line_start + 1)

    def _skip_space(self):
        text = self.text
        while self.at < len(text):
            c = text[self.at]
            if c == "\n":
                self.at += 1
                self.line += 1
                self.line_start = self.at
            elif c in " \t\r":
                self.at += 1
            elif text.startswith("//", self.at):
                end = text.find("\n", self.at)
                self.at = len(text) if end < 0 else end
            else:
                return

    def next(self) -> _Token:
        self._skip_space()
        text = self.text
        start = self.at
        line, column = self.line, start - self.line_start + 1
        if start >= len(text):
            return _Token("end", None, line, column, "end of file")
        c = text[start]
        if c in _IDENTIFIER_START:
            end = start + 1
            while end < len(text) and text[end] in _IDENTIFIER_REST:
                end += 1
            self.at = end
            return _Token("name", text[start:end], line, column, text[start:end])
        if c in _DIGITS or (c == "-" and start + 1 < len(text) and text[start + 1] in _DIGITS):
            return self._number(start, line, column)
        if c == '"':
            return self._string(start, line, column)
        if c in "{}:;=.":
            self.at = start + 1
            return _Token("punctuation", c, line, column, c)
        raise self._error(f"Unexpected character {c!r}")

    def _number(self, start: int, line: int, column: int) -> _Token:
        match = _NUMBER.match(self.text, start)
        end = match.end()
        word = match.group(0)
        if end < len(self.text) and self.text[end] in _IDENTIFIER_REST:
            raise self._error(f"Malformed number starting with {word!r}")
        if len(word) > MAX_NUMBER_LENGTH:
            raise self._error("Number too long")
        self.at = end
        digits = word.lstrip("-")
        if digits[:2] in ("0x", "0X"):
            return _Token("int", int(word, 16), line, column, word)
        if "." in word or "e" in word or "E" in word:
            return _Token("float", float(word), line, column, word)
        if len(digits) > 1 and digits[0] == "0":
            raise self._error(f"Number {word} has a leading zero")
        return _Token("int", int(word), line, column, word)

    def _string(self, start: int, line: int, column: int) -> _Token:
        text = self.text
        value = bytearray()
        at = start + 1
        while True:
            if at >= len(text) or text[at] == "\n":
                raise self._error("Unterminated string", at)
            c = text[at]
            if c == '"':
                self.at = at + 1
                return _Token("string", bytes(value), line, column, text[start:at + 1])
            if c != "\\":
                value += c.encode("utf-8", "surrogateescape")
                at += 1
                continue
            escape = text[at + 1] if at + 1 < len(text) else ""
            if escape in _ESCAPES:
                value += _ESCAPES[escape]
                at += 2
            elif escape == "x":
                digits = text[at + 2:at + 4]
                if not re.fullmatch(r"[0-9a-fA-F]{2}", digits):
                    raise self._error(r"\x takes two hex digits", at)
                value.append(int(digits, 16))
                at += 4
            else:
                raise self._error(f"Unknown escape in a string: \\{escape}", at)


class _Parser:
    def __init__(self, text: str, path: str):
        self.path = path
        self.lexer = _Lexer(text, path)
        self.token = self.lexer.next()
        self._type_tokens = {}  # Where each field's type is, by message and field name.

    def _error(self, message: str, token: Optional[_Token] = None) -> SchemaError:
        token = token or self.token
        return SchemaError(message, self.path, token.line, token.column)

    def _advance(self) -> _Token:
        token = self.token
        self.token = self.lexer.next()
        return token

    def _is(self, text: str) -> bool:
        return self.token.kind in ("punctuation", "name") and self.token.value == text

    def _accept(self, text: str) -> bool:
        if self._is(text):
            self._advance()
            return True
        return False

    def _expect(self, text: str):
        if not self._accept(text):
            raise self._error(f"Expected '{text}', found {self.token.text}")

    def _name(self, what: str) -> _Token:
        if self.token.kind != "name":
            raise self._error(f"Expected {what}, found {self.token.text}")
        return self._advance()

    def parse(self) -> ParsedSchema:
        schema = ParsedSchema()
        if self._is("namespace"):
            self._advance()
            schema.namespace = self._namespace()
        while self.token.kind != "end":
            if self._is("enum"):
                self._advance()
                schema.enums.append(self._enum())
            elif self._is("message"):
                self._advance()
                schema.messages.append(self._message())
            elif self._is("namespace"):
                raise self._error("A file has one namespace, at its start")
            else:
                raise self._error(f"Expected 'message' or 'enum', found {self.token.text}")
        self._check_declarations(schema)
        for message in schema.messages:
            self._resolve_fields(schema, message)
        return schema

    def _namespace(self) -> str:
        parts = []
        while True:
            token = self._name("a namespace")
            if not _NAMESPACE_PART.match(token.value) or token.value in CPP_KEYWORDS:
                raise self._error(f"'{token.value}' cannot be part of a namespace", token)
            parts.append(token.value)
            if not self._accept("."):
                break
        self._expect(";")
        return ".".join(parts)

    def _enum(self) -> ParsedEnum:
        name = self._type_name("enum")
        self._expect(":")
        underlying = self._name("the enum's integer type")
        if underlying.value not in INT_RANGES:
            raise self._error(f"An enum's type is an integer type, not '{underlying.value}'", underlying)
        low, high = INT_RANGES[underlying.value]
        enum = ParsedEnum(name, underlying.value)
        names = set()
        self._expect("{")
        while not self._accept("}"):
            token = self._name("an enumerator")
            if not _ENUMERATOR_NAME.match(token.value) or self._is_reserved(token.value):
                raise self._error(f"'{token.value}' cannot be an enumerator", token)
            if token.value in names:
                raise self._error(f"Enumerator '{token.value}' is already in enum {name}", token)
            names.add(token.value)
            self._expect("=")
            value = self.token
            if value.kind != "int":
                raise self._error(f"Enumerator '{token.value}' needs an integer, found {value.text}")
            self._advance()
            expected = len(enum.values)
            if value.value != expected:
                raise self._error(
                    f"Enumerator '{token.value}' must be {expected}, not {value.text}: values go up by "
                    "one from 0, and one no longer used stays where it is", value)
            if not low <= value.value <= high:
                raise self._error(f"{value.text} does not fit in {underlying.value}", value)
            enum.values.append(ParsedEnumerator(token.value, value.value))
            self._expect(";")
        if not enum.values:
            raise self._error(f"Enum {name} has no enumerators")
        return enum

    def _message(self) -> ParsedMessage:
        message = ParsedMessage(self._type_name("message"))
        names = set()
        self._expect("{")
        while not self._accept("}"):
            number = self.token
            if number.kind != "int":
                raise self._error(
                    f"A field starts with its number, {len(message.fields) + 1} here, "
                    f"found {number.text}")
            self._advance()
            expected = len(message.fields) + 1
            if number.value != expected:
                raise self._error(
                    f"Field number must be {expected}, not {number.text}: numbers go up by one from 1. "
                    "A new field goes at the end, and one no longer used stays, marked `deprecated`",
                    number)
            if number.value > MAX_FIELD_ID:
                raise self._error(f"A message has at most {MAX_FIELD_ID} fields", number)
            name = self._name("a field name")
            self._check_field_name(message.name, name, names)
            names.add(name.value)
            self._expect(":")
            type_name = self._name("a field type")
            default = None
            if self._accept("="):
                default = self._literal()
            deprecated = self._accept("deprecated")
            self._expect(";")
            message.fields.append(ParsedField(number.value, name.value, type_name.value, default, deprecated))
            self._type_tokens[(message.name, name.value)] = type_name
        taken = {f.name for f in message.fields}
        for f in message.fields:
            if f.name.startswith("has_") and f.name[4:] in taken:
                token = self._type_tokens[(message.name, f.name)]
                raise self._error(f"Field '{f.name}' clashes with the has_ method of '{f.name[4:]}'", token)
        pascal = {}
        for f in message.fields:
            pascal_name = _pascal_case(f.name)
            if pascal_name in pascal:
                token = self._type_tokens[(message.name, f.name)]
                raise self._error(f"Fields '{f.name}' and '{pascal[pascal_name]}' clash", token)
            pascal[pascal_name] = f.name
        return message

    def _type_name(self, what: str) -> str:
        token = self._name(f"the {what}'s name")
        if not _MESSAGE_NAME.match(token.value) or self._is_reserved(token.value):
            raise self._error(f"'{token.value}' cannot name a {what}: it must be PascalCase", token)
        return token.value

    def _check_field_name(self, message: str, token: _Token, names: set):
        name = token.value
        if not _FIELD_NAME.match(name):
            raise self._error(f"Field name '{name}' must be snake_case", token)
        if self._is_reserved(name) or name in RESERVED_FIELD_NAMES:
            raise self._error(f"'{name}' cannot name a field", token)
        if name in names:
            raise self._error(f"Field '{name}' is already in message {message}", token)
        if _pascal_case(name) == message:
            raise self._error(f"Field '{name}' clashes with the name of its message", token)

    @staticmethod
    def _is_reserved(name: str) -> bool:
        return name in CPP_KEYWORDS or name in CPP_MACROS or name in BASE_TYPES

    def _literal(self) -> Default:
        token = self.token
        if token.kind == "name":
            self._advance()
            if token.value == "true":
                return True
            if token.value == "false":
                return False
            return EnumeratorRef(token.value)
        if token.kind in ("int", "float", "string"):
            self._advance()
            return token.value
        raise self._error(f"Expected a default value, found {token.text}")

    def _check_declarations(self, schema: ParsedSchema):
        names = set()
        for declaration in [*schema.enums, *schema.messages]:
            if declaration.name in names:
                raise SchemaError(f"'{declaration.name}' is declared twice", self.path)
            names.add(declaration.name)
        messages = {m.name for m in schema.messages}
        for name in messages:
            for suffix in GENERATED_MESSAGE_SUFFIXES:
                if name.endswith(suffix) and name[: -len(suffix)] in messages:
                    raise SchemaError(
                        f"Message '{name}' clashes with what is generated for '{name[:-len(suffix)]}'",
                        self.path)
        for name in names:
            if name in ("Field", "Message"):
                raise SchemaError(f"'{name}' cannot name a message or enum", self.path)

    def _resolve_fields(self, schema: ParsedSchema, message: ParsedMessage):
        enums = {e.name: e for e in schema.enums}
        for f in message.fields:
            token = self._type_tokens[(message.name, f.name)]
            if f.type not in BASE_TYPES and f.type not in enums:
                raise self._error(f"Field '{f.name}' has unknown type '{f.type}'", token)
            if f.default is not None:
                try:
                    f.default = _checked_default(f.type, f.default, enums)
                except ValueError as error:
                    raise self._error(f"Default of field '{f.name}': {error}", token) from None


def _pascal_case(name: str) -> str:
    return "".join(word.capitalize() for word in name.split("_"))


def _checked_default(type_name: str, value: Default, enums: dict) -> Default:
    if type_name in enums:
        names = [e.name for e in enums[type_name].values]
        if not isinstance(value, EnumeratorRef) or value.name not in names:
            raise ValueError(f"expected one of {type_name}: {', '.join(names)}")
        return value
    if isinstance(value, EnumeratorRef):
        raise ValueError(f"'{value.name}' is not a value of type {type_name}")
    if type_name == "bool":
        if not isinstance(value, bool):
            raise ValueError("expected true or false")
        return value
    if type_name == "string":
        if not isinstance(value, bytes):
            raise ValueError("expected a string")
        return value
    if isinstance(value, (bool, bytes)):
        raise ValueError(f"expected a number for type {type_name}")
    if type_name in ("float", "double"):
        try:
            number = float(value)
        except OverflowError:
            raise ValueError("too large") from None
        if not math.isfinite(number):
            raise ValueError("too large")
        if type_name == "float" and number != 0:
            # What a literal of that type can hold, as C++ takes anything else as an error or a warning.
            if abs(number) > FLOAT_MAX:
                raise ValueError("too large for float")
            if abs(number) < FLOAT_TRUE_MIN:
                raise ValueError("too small for float")
        return number
    if not isinstance(value, int):
        raise ValueError(f"expected an integer for type {type_name}")
    low, high = INT_RANGES[type_name]
    if not low <= value <= high:
        raise ValueError(f"{value} does not fit in {type_name}")
    return value


def parse_schema(source: Union[str, bytes], path: str = "") -> ParsedSchema:
    """Raises SchemaError, and nothing else, for anything that is not a valid schema."""
    if isinstance(source, bytes):
        try:
            source = source.decode("utf-8")
        except UnicodeDecodeError as error:
            raise SchemaError(f"Not UTF-8: {error.reason}", path) from None
    return _Parser(source, path).parse()
