import pytest

from schema_parser import EnumeratorRef, SchemaError, parse_schema


def rejects(source, match):
    with pytest.raises(SchemaError, match=match):
        parse_schema(source)


def test_a_whole_schema():
    schema = parse_schema("""
        // A comment.
        namespace app.core;

        enum Direction : uint32 {
          kUnknown = 0;
          kBuy = 1;  // Another one.
        }

        message Position {
          1 id: int64;
          2 name: string = "EURUSD";
          3 direction: Direction = kBuy;
          4 ratio: float = 0.5;
          5 enabled: bool = true;
          6 old: int32 = 5 deprecated;
        }
    """)
    assert schema.namespace == "app.core"
    assert [(v.name, v.value) for v in schema.enums[0].values] == [("kUnknown", 0), ("kBuy", 1)]
    fields = schema.messages[0].fields
    assert [f.number for f in fields] == [1, 2, 3, 4, 5, 6]
    assert fields[1].default == b"EURUSD"
    assert fields[2].default == EnumeratorRef("kBuy")
    assert fields[3].default == 0.5
    assert fields[4].default is True
    assert fields[5].default == 5 and fields[5].deprecated


def test_no_namespace_and_no_declarations_are_fine():
    assert parse_schema("").messages == []
    assert parse_schema("message Empty { }").messages[0].fields == []


# The invariants, as syntax.
@pytest.mark.parametrize("body", [
    "2 a: int32;",                              # Does not start from 1.
    "1 a: int32; 3 b: int32;",                  # A gap, as from removing a field.
    "2 b: int32; 1 a: int32;",                  # Out of order.
    "1 a: int32; 1 b: int32;",                  # A number used twice.
    "0 a: int32;",
])
def test_field_numbers_only_go_up_by_one(body):
    rejects(f"message M {{ {body} }}", "Field number must be")


def test_a_field_must_state_its_number():
    rejects("message M { a: int32; }", "starts with its number")


@pytest.mark.parametrize("body", ["kA = 1;", "kA = 0; kB = 2;", "kB = 1; kA = 0;"])
def test_enumerators_only_go_up_by_one_from_zero(body):
    rejects(f"enum E : uint8 {{ {body} }}", "must be")


@pytest.mark.parametrize("source,match", [
    ("message M { 1 a: int32 = 2147483648; }", "does not fit"),
    ("message M { 1 a: uint8 = -1; }", "does not fit"),
    ("message M { 1 a: int32 = 1.5; }", "expected an integer"),
    ("message M { 1 a: int32 = true; }", "expected a number"),
    ('message M { 1 a: int32 = "x"; }', "expected a number"),
    ("message M { 1 a: bool = 1; }", "expected true or false"),
    ("message M { 1 a: string = 1; }", "expected a string"),
    ("message M { 1 a: float = 1e39; }", "too large"),
    ("message M { 1 a: double = 1e999; }", "too large"),
    ("enum E : uint8 { kA = 0; } message M { 1 a: E = kB; }", "expected one of"),
    ("enum E : uint8 { kA = 0; } message M { 1 a: E = 0; }", "expected one of"),
    ("message M { 1 a: int32 = kA; }", "not a value"),
])
def test_a_default_must_suit_its_type(source, match):
    rejects(source, match)


def test_enums_can_come_after_the_messages_that_use_them():
    schema = parse_schema("message M { 1 a: E = kB; } enum E : uint8 { kA = 0; kB = 1; }")
    assert schema.messages[0].fields[0].default == EnumeratorRef("kB")


@pytest.mark.parametrize("source,match", [
    ("message m { }", "PascalCase"),
    ("message M { 1 Name: int32; }", "snake_case"),
    ("message M { 1 a__b: int32; }", "snake_case"),
    ("message M { 1 a_: int32; }", "snake_case"),
    ("message M { 1 class: int32; }", "cannot name a field"),
    ("message M { 1 errno: int32; }", "cannot name a field"),
    ("message M { 1 field: int32; }", "cannot name a field"),
    ("message Item { 1 item: int32; }", "name of its message"),
    ("message M { 1 a: int32; 2 a: string; }", "already in message"),
    ("message M { 1 a: int32; 2 has_a: bool; }", "has_ method"),
    ("message M { } message M { }", "declared twice"),
    ("message M { } enum M : uint8 { kA = 0; }", "declared twice"),
    ("message M { } message MBuilder { }", "generated for"),
    ("enum E : float { kA = 0; }", "integer type"),
    ("enum E : uint8 { }", "no enumerators"),
    ("enum E : uint8 { class = 0; }", "cannot be an enumerator"),
    ("enum E : uint8 { kA = 0; kA = 1; }", "already in enum"),
    ("namespace a.class;", "cannot be part"),
    ("message M { } namespace a;", "one namespace"),
    ("message M { 1 a: Unknown; }", "unknown type"),
])
def test_names_are_checked(source, match):
    rejects(source, match)


@pytest.mark.parametrize("source", [
    "message", "message M", "message M {", "message M { 1", "message M { 1 a", "message M { 1 a:",
    "message M { 1 a: int32", "message M { 1 a: int32 =", "message M { 1 a: int32 = 1",
    "enum E", "enum E :", "enum E : uint8", "enum E : uint8 {", "enum E : uint8 { kA", "enum E : uint8 { kA =",
    "namespace", "namespace a", "namespace a.", "wat", "@", 'message M { 1 a: string = "', "message M { 1 a: int32 = 12x; }",
    r'message M { 1 a: string = "\q"; }', r'message M { 1 a: string = "\x1"; }', "message M { 1 a: int32 = 007; }",
])
def test_broken_input_is_an_error_not_a_crash(source):
    with pytest.raises(SchemaError):
        parse_schema(source)


def test_errors_say_where():
    with pytest.raises(SchemaError) as error:
        parse_schema("message M {\n  1 a: int32;\n  3 b: int32;\n}", "file.gendb")
    assert str(error.value).startswith("file.gendb:3:3: Field number must be 2, not 3")


def test_string_escapes_become_bytes():
    schema = parse_schema(r'message M { 1 a: string = "q\" b\\ n\n t\t r\r z\0 x\xFFя"; }')
    assert schema.messages[0].fields[0].default == b'q" b\\ n\n t\t r\r z\0 x\xff' + "я".encode()


def test_source_that_is_not_utf8_is_an_error():
    rejects(b"message \xff { }", "Not UTF-8")


def test_a_very_long_number_is_an_error():
    rejects("message M { 1 a: int32 = " + "1" * 100000 + "; }", "too long")


def test_nested_messages_in_any_order():
    schema = parse_schema("""
        message Order {
          1 customer: Customer;
          2 lines: repeated Line;
          3 parent: Order;
          4 tags: repeated string;
          5 prices: repeated double;
          6 kinds: repeated Kind deprecated;
        }
        message Customer { 1 name: string; }
        message Line { 1 sku: string; }
        enum Kind : uint8 { kA = 0; }
    """)
    fields = schema.messages[0].fields
    assert [(f.type, f.repeated, f.deprecated) for f in fields] == [
        ("Customer", False, False), ("Line", True, False), ("Order", False, False),
        ("string", True, False), ("double", True, False), ("Kind", True, True)]


@pytest.mark.parametrize("field, error", [
    ("1 a: Other = 1;", "Message field 'a' cannot have a default"),
    ("1 a: repeated int32 = 1;", "Repeated field 'a' cannot have a default"),
    ('1 a: repeated string = "x";', "Repeated field 'a' cannot have a default"),
    ("1 a: repeated repeated int32;", "A list of lists"),
    ("1 a: repeated Missing;", "unknown type 'Missing'"),
    ("1 a: repeated;", "Expected a field type"),
    ("1 verify: int32;", "'verify' cannot name a field"),
])
def test_what_nested_and_repeated_fields_cannot_be(field, error):
    with pytest.raises(SchemaError, match=error):
        parse_schema(f"message Item {{ {field} }} message Other {{ }}")


def test_repeated_can_still_name_a_field():
    schema = parse_schema("message Item { 1 repeated: repeated int32; }")
    assert schema.messages[0].fields[0].name == "repeated"
    assert schema.messages[0].fields[0].repeated

