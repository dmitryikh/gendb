import textwrap

import pytest

from fb_types import Collection, Index
from generate_message_class import make_template, render_fbs
import schema_validator
from store import Store
import flatc_to_store


def write_fbs(tmp_path, body):
    path = tmp_path / "schema.fbs"
    path.write_text(textwrap.dedent(body))
    return path


def test_getters_are_typed_by_storage_class(tmp_path):
    fbs = write_fbs(tmp_path, """
        namespace test;
        enum Color : ushort { kRed = 0, kBlue }
        table Item {
          id: ulong;
          count: int;
          active: bool;
          name: string;
          color: Color;
        }
    """)
    code = render_fbs(make_template(), fbs)
    assert "_view.Get<Id, uint64_t>(0ULL)" in code
    assert "_view.Get<Count, int32_t>(0)" in code
    assert "_view.Get<Active, bool>(false)" in code
    assert "_view.GetString<Name>()" in code
    assert "static_cast<test::Color>(_view.Get<Color, uint16_t>(0))" in code
    assert "gendb::FieldInfo::UINT16" in code


def test_string_setters_reject_temporaries_only_in_the_borrowing_builder(tmp_path):
    fbs = write_fbs(tmp_path, "table Item { name: string; }")
    code = render_fbs(make_template(), fbs)
    assert "ItemBuilder& set_name(S&&) = delete;" in code
    assert "_patch.CopyString(Item::Name, value);" in code


def test_deprecated_field_has_no_accessors_and_keeps_its_id_reserved(tmp_path):
    fbs = write_fbs(tmp_path, """
        table Item {
          old: int (deprecated);
          kept: int;
        }
    """)
    code = render_fbs(make_template(), fbs)
    assert "old()" not in code
    assert "set_old" not in code
    assert "1 is reserved: `old` is deprecated" in code
    assert "Kept = 2," in code


def test_field_id_above_format_limit_is_rejected(tmp_path):
    fields = "\n".join(f"f{i}: int;" for i in range(schema_validator.MAX_FIELD_ID + 1))
    fbs = write_fbs(tmp_path, f"table Item {{ {fields} }}")
    with pytest.raises(ValueError, match="outside 1..2040"):
        render_fbs(make_template(), fbs)


def test_highest_valid_field_id_is_accepted(tmp_path):
    fields = "\n".join(f"f{i}: int;" for i in range(schema_validator.MAX_FIELD_ID))
    fbs = write_fbs(tmp_path, f"table Item {{ {fields} }}")
    assert "F2039 = 2040," in render_fbs(make_template(), fbs)


def test_deprecated_field_cannot_be_a_key_or_indexed(tmp_path):
    fbs = write_fbs(tmp_path, """
        namespace test;
        table Item {
          id: int (deprecated);
          age: int (deprecated);
          kept: int;
        }
    """)
    store = Store()
    flatc_to_store.load_fbs_to_store(store, fbs)
    store.add_collection(Collection(name="items", type="test.Item", primary_key=["id"]))
    store.add_index(Index(name="by_age", collection="items", field="age"))
    errors = schema_validator.validate_collections(store)
    assert len(errors) == 2
