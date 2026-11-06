import subprocess

import pytest

from schema_evolution import Report, check_against_git, compare_databases, compare_schemas
from schema_parser import parse_schema

BASE = """
    namespace app;
    enum Color : uint8 { kRed = 0; kBlue = 1; }
    message Item {
      1 id: uint64;
      2 name: string = "x";
      3 color: Color;
      4 old: int32 deprecated;
    }
"""


def problems(new_source, old_source=BASE):
    report = Report([], [])
    compare_schemas([parse_schema(old_source)], [parse_schema(new_source)], report)
    return report.errors


def edit(old, new):
    assert old in BASE
    return BASE.replace(old, new)


@pytest.mark.parametrize("source", [
    BASE,
    edit("4 old: int32 deprecated;", '4 old: int32 deprecated; 5 added: string = "new";'),
    edit("3 color: Color;", "3 color: Color deprecated;"),
    edit("kBlue = 1;", "kBlue = 1; kGreen = 2;"),
    BASE + "message Other { 1 id: int32; }",
])
def test_growing_is_allowed(source):
    assert problems(source) == []


@pytest.mark.parametrize("source,match", [
    (edit("4 old: int32 deprecated;", ""), "was removed"),
    (edit("1 id: uint64;", "1 id: int64;"), "changed type"),
    (edit("3 color: Color;", "3 color: int32;"), "changed type"),
    (edit("1 id: uint64;", "1 key: uint64;"), "was renamed"),
    (edit('= "x"', '= "y"'), "changed its default"),
    (edit('= "x"', ""), "changed its default"),
    (edit("1 id: uint64;", "1 id: uint64 = 5;"), "changed its default"),
    (edit("4 old: int32 deprecated;", "4 old: int32;"), "no longer deprecated"),
    (edit("kBlue = 1;", ""), "was removed"),
    (edit("kBlue = 1;", "kGreen = 1;"), "was renamed"),
    (edit("Color : uint8", "Color : uint16"), "type changed"),
    ("namespace app; enum Color : uint8 { kRed = 0; kBlue = 1; }", "message was removed"),
    ("namespace other;" + BASE.split("namespace app;")[1], "was removed"),
])
def test_breaking_changes_are_errors(source, match):
    errors = problems(source)
    assert any(match in error for error in errors), errors


def test_a_declaration_can_move_to_another_file():
    report = Report([], [])
    old = [parse_schema("namespace app; message A { 1 id: int32; } message B { 1 id: int32; }")]
    new = [parse_schema("namespace app; message A { 1 id: int32; }"),
           parse_schema("namespace app; message B { 1 id: int32; }")]
    compare_schemas(old, new, report)
    assert report.errors == []


DATABASE = {
    "collections": [{"name": "items", "type": "app.Item", "primary_key": ["id"]}],
    "sequences": [{"name": "first", "type": "uint64"}, {"name": "second", "type": "int32"}],
    "indices": [{"name": "by_name", "collection": "items", "fields": ["name"]}],
}


def compare(**changes):
    report = Report([], [])
    compare_databases(DATABASE, {**DATABASE, **changes}, "db.yaml", report)
    return report


def test_database_growth_is_allowed():
    report = compare(
        collections=DATABASE["collections"] + [{"name": "more", "type": "app.More", "primary_key": "id"}],
        sequences=DATABASE["sequences"] + [{"name": "third", "type": "uint64"}],
        indices=[{"name": "by_id", "collection": "items", "fields": ["id"]}],
    )
    assert report.errors == [] and report.warnings == []


@pytest.mark.parametrize("changes,match", [
    ({"collections": [{"name": "items", "type": "app.Other", "primary_key": ["id"]}]}, "changed type"),
    ({"collections": [{"name": "items", "type": "app.Item", "primary_key": ["name"]}]}, "primary key"),
    ({"sequences": [{"name": "first", "type": "uint64"}]}, "was removed"),
    ({"sequences": list(reversed(DATABASE["sequences"]))}, "changed from"),
    ({"sequences": [{"name": "first", "type": "int32"}, DATABASE["sequences"][1]]}, "changed type"),
])
def test_database_breaking_changes_are_errors(changes, match):
    assert any(match in error for error in compare(**changes).errors)


@pytest.mark.parametrize("changes,match", [
    ({"collections": []}, "was removed"),
    ({"collections": [{"name": "items", "type": "app.Item", "primary_key": ["id"], "ephemeral": True}]}, "ephemeral"),
    ({"indices": [{"name": "by_name", "collection": "items", "fields": ["name"], "unique": True}]}, "newly unique"),
])
def test_what_loses_no_data_is_a_warning(changes, match):
    report = compare(**changes)
    assert report.errors == []
    assert any(match in warning for warning in report.warnings)


def git(root, *arguments):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *arguments], cwd=root, check=True,
                   capture_output=True)


def test_compares_the_working_tree_with_where_the_branch_started(tmp_path):
    git(tmp_path, "init", "-q", "-b", "main")
    (tmp_path / "a.gendb").write_text("message A { 1 id: int32; }")
    (tmp_path / "db.yaml").write_text(
        "types: {schema_files: [a.gendb]}\nsequences: [{name: s, type: uint64}]\n")
    git(tmp_path, "add", ".")
    git(tmp_path, "commit", "-qm", "base")
    git(tmp_path, "checkout", "-qb", "work")

    (tmp_path / "a.gendb").write_text("message A { 1 id: int32; 2 name: string; }")
    assert check_against_git(tmp_path, "main").errors == []

    (tmp_path / "a.gendb").write_text("message A { 1 id: int64; }")
    assert any("changed type" in e for e in check_against_git(tmp_path, "main").errors)

    (tmp_path / "a.gendb").write_text("message A { 1 id: int32; }")
    (tmp_path / "db.yaml").write_text("types: {schema_files: [a.gendb]}\nsequences: []\n")
    assert any("was removed" in e for e in check_against_git(tmp_path, "main").errors)


def test_a_field_cannot_become_repeated_or_stop_being():
    old = "message Item { 1 a: int32; 2 b: repeated int32; 3 c: Other; } message Other { }"
    new = "message Item { 1 a: repeated int32; 2 b: int32; 3 c: repeated Other; } message Other { }"
    errors = problems(new, old)
    assert len(errors) == 3
    assert all("repeated" in error for error in errors)


def test_a_field_cannot_change_which_message_it_holds():
    old = "message Item { 1 a: Other; } message Other { } message Third { }"
    new = "message Item { 1 a: Third; } message Other { } message Third { }"
    assert "changed type" in problems(new, old)[0]


def test_nested_messages_and_lists_can_be_added_and_grow():
    old = "message Item { 1 a: Other; } message Other { 1 x: int32; }"
    new = """message Item { 1 a: Other; 2 b: repeated Other; 3 c: repeated string; }
             message Other { 1 x: int32; 2 y: repeated int32; }"""
    assert problems(new, old) == []

