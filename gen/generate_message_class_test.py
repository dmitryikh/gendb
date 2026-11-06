import textwrap

import pytest

from generate_message_class import make_template, render_schema
from schema_types import Collection, Index
from store import Store
import schema_loader
import schema_parser
import schema_validator


def write_schema(tmp_path, body):
    path = tmp_path / "schema.gendb"
    path.write_text(textwrap.dedent(body))
    return path


def test_getters_are_typed_by_storage_class(tmp_path):
    path = write_schema(tmp_path, """
        namespace test;
        enum Color : uint16 { kRed = 0; kBlue = 1; }
        message Item {
          1 id: uint64;
          2 count: int32;
          3 active: bool;
          4 name: string;
          5 color: Color;
        }
    """)
    code = render_schema(make_template(), path)
    assert "_view.Get<Id, uint64_t>(0ULL)" in code
    assert "_view.Get<Count, int32_t>(0)" in code
    assert "_view.Get<Active, bool>(false)" in code
    assert "_view.GetString<Name>()" in code
    assert "static_cast<test::Color>(_view.Get<Color, uint16_t>(0))" in code
    assert "gendb::FieldInfo::UINT16" in code


def test_defaults_reach_the_getters(tmp_path):
    path = write_schema(tmp_path, r"""
        namespace test;
        enum Color : uint16 { kRed = 0; kBlue = 1; }
        message Item {
          1 count: int32 = -3;
          2 huge: uint64 = 18446744073709551615;
          3 lowest: int64 = -9223372036854775808;
          4 active: bool = true;
          5 ratio: float = 0.25;
          6 name: string = "a\"b\x00c";
          7 color: Color = kBlue;
        }
    """)
    code = render_schema(make_template(), path)
    assert "_view.Get<Count, int32_t>(static_cast<int32_t>(-3LL))" in code
    assert "18446744073709551615ULL" in code
    assert "(-9223372036854775807LL - 1)" in code
    assert "_view.Get<Active, bool>(true)" in code
    assert "_view.Get<Ratio, float>(0.25f)" in code
    assert r'_view.GetString<Name>(std::string_view("a\"b\000c", 5))' in code
    assert "static_cast<uint16_t>(test::Color::kBlue)" in code


def test_string_setters_reject_temporaries_only_in_the_borrowing_builder(tmp_path):
    path = write_schema(tmp_path, "message Item { 1 name: string; }")
    code = render_schema(make_template(), path)
    assert "ItemBuilder& set_name(S&&) = delete;" in code
    assert "_patch.CopyString(Item::Name, value);" in code


def test_deprecated_field_has_no_accessors_and_keeps_its_id_reserved(tmp_path):
    path = write_schema(tmp_path, """
        message Item {
          1 old: int32 deprecated;
          2 kept: int32;
        }
    """)
    code = render_schema(make_template(), path)
    assert "old()" not in code
    assert "set_old" not in code
    assert "1 is reserved: `old` is deprecated" in code
    assert "Kept = 2," in code


def many_fields(count):
    return "\n".join(f"{i} f{i}: int32;" for i in range(1, count + 1))


def test_highest_valid_field_id_is_accepted(tmp_path):
    path = write_schema(tmp_path, f"message Item {{ {many_fields(schema_parser.MAX_FIELD_ID)} }}")
    assert "F2040 = 2040," in render_schema(make_template(), path)


def test_field_id_above_format_limit_is_rejected(tmp_path):
    path = write_schema(tmp_path, f"message Item {{ {many_fields(schema_parser.MAX_FIELD_ID + 1)} }}")
    with pytest.raises(schema_parser.SchemaError, match="at most 2040"):
        render_schema(make_template(), path)


def test_deprecated_field_cannot_be_a_key_or_indexed(tmp_path):
    path = write_schema(tmp_path, """
        namespace test;
        message Item {
          1 id: int32 deprecated;
          2 age: int32 deprecated;
          3 kept: int32;
        }
    """)
    store = Store()
    schema_loader.load_schema_to_store(store, path)
    store.add_collection(Collection(name="items", type="test.Item", primary_key=["id"]))
    store.add_index(Index(name="by_age", collection="items", fields=["age"]))
    errors = schema_validator.validate_collections(store)
    assert len(errors) == 2
