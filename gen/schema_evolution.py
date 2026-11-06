#!/usr/bin/env python3
"""Checks that schemas only grew in ways that keep existing data readable.

The syntax of a schema file rules out gaps and reordering. What it cannot see is what the file said
before, which this compares, for messages, enums and the database YAML files:

    gen/schema_evolution.py --base origin/main

Exit status 1 means a change breaks data that may already exist. See docs/schema.md for the rules.
"""
import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import yaml

from schema_parser import ParsedEnum, ParsedMessage, ParsedSchema, SchemaError, parse_schema

SCHEMA_SUFFIX = ".gendb"


@dataclass
class Report:
    errors: List[str]
    warnings: List[str]


def compare_messages(old: ParsedMessage, new: ParsedMessage, name: str, report: Report):
    new_fields = {f.number: f for f in new.fields}
    for before in old.fields:
        where = f"{name}: field {before.number} `{before.name}`"
        after = new_fields.get(before.number)
        if after is None:
            report.errors.append(f"{where} was removed. Mark it `deprecated` instead")
            continue
        if after.name != before.name:
            report.errors.append(f"{where} was renamed to `{after.name}`. Add a new field instead")
        if after.type != before.type:
            report.errors.append(f"{where} changed type from {before.type} to {after.type}, "
                                 "which would misread stored data. Add a new field instead")
        if after.default != before.default:
            report.errors.append(f"{where} changed its default from {_default(before.default)} to "
                                 f"{_default(after.default)}, which changes what every record that "
                                 "does not set it means. Add a new field instead")
        if before.deprecated and not after.deprecated:
            report.errors.append(f"{where} is no longer deprecated, but data written since may lack it "
                                 "or hold old values. Add a new field instead")


def _default(value) -> str:
    return "none" if value is None else repr(value)


def compare_enums(old: ParsedEnum, new: ParsedEnum, name: str, report: Report):
    if old.underlying != new.underlying:
        report.errors.append(f"{name}: type changed from {old.underlying} to {new.underlying}")
    new_values = {v.value: v.name for v in new.values}
    for before in old.values:
        where = f"{name}: value {before.value} `{before.name}`"
        if before.value not in new_values:
            report.errors.append(f"{where} was removed. Keep it, as records may hold it")
        elif new_values[before.value] != before.name:
            report.errors.append(f"{where} was renamed to `{new_values[before.value]}`")


def _declarations(schemas: List[ParsedSchema]) -> Tuple[Dict, Dict]:
    messages, enums = {}, {}
    for schema in schemas:
        prefix = f"{schema.namespace}." if schema.namespace else ""
        for message in schema.messages:
            messages[prefix + message.name] = message
        for enum in schema.enums:
            enums[prefix + enum.name] = enum
    return messages, enums


def compare_schemas(old: List[ParsedSchema], new: List[ParsedSchema], report: Report):
    """By qualified name, so a declaration can move to another file."""
    old_messages, old_enums = _declarations(old)
    new_messages, new_enums = _declarations(new)
    for name, message in old_messages.items():
        if name in new_messages:
            compare_messages(message, new_messages[name], name, report)
        else:
            report.errors.append(f"{name}: message was removed. Records of it may exist")
    for name, enum in old_enums.items():
        if name in new_enums:
            compare_enums(enum, new_enums[name], name, report)
        else:
            report.errors.append(f"{name}: enum was removed. Records may use it")


def _as_list(value) -> list:
    return value if isinstance(value, list) else [value]


def compare_databases(old: dict, new: dict, where: str, report: Report):
    old_collections = {c["name"]: c for c in old.get("collections") or []}
    new_collections = {c["name"]: c for c in new.get("collections") or []}
    for name, before in old_collections.items():
        after = new_collections.get(name)
        if after is None:
            report.warnings.append(f"{where}: collection `{name}` was removed. Its data stays, out of "
                                   "reach, until discard_unknown_collections drops it")
            continue
        if before["type"] != after["type"]:
            report.errors.append(f"{where}: collection `{name}` changed type from {before['type']} to "
                                 f"{after['type']}")
        if _as_list(before["primary_key"]) != _as_list(after["primary_key"]):
            report.errors.append(f"{where}: collection `{name}` changed its primary key, which is how "
                                 "stored records are found")
        if not before.get("ephemeral", False) and after.get("ephemeral", False):
            report.warnings.append(f"{where}: collection `{name}` became ephemeral. Its stored data "
                                   "stays, out of reach, until discard_unknown_collections drops it")

    # A sequence's identity in storage is its place in this list.
    old_sequences = old.get("sequences") or []
    new_sequences = new.get("sequences") or []
    for position, before in enumerate(old_sequences):
        if position >= len(new_sequences):
            report.errors.append(f"{where}: sequence `{before['name']}` was removed. Sequences are "
                                 "stored by their place in the list: keep it")
            continue
        after = new_sequences[position]
        if before["name"] != after["name"]:
            report.errors.append(f"{where}: sequence {position} changed from `{before['name']}` to "
                                 f"`{after['name']}`. Sequences are stored by their place in the list: "
                                 "new ones go at the end")
        elif before["type"] != after["type"]:
            report.errors.append(f"{where}: sequence `{before['name']}` changed type")

    old_indices = {i["name"]: i for i in old.get("indices") or []}
    for index in new.get("indices") or []:
        before = old_indices.get(index["name"])
        if index.get("unique", False) and not (before and before.get("unique", False)):
            report.warnings.append(f"{where}: index `{index['name']}` is newly unique. Existing records "
                                   "are not checked against it")


def _is_database(document) -> bool:
    return isinstance(document, dict) and "schema_files" in (document.get("types") or {})


def _git(root: Path, *arguments) -> str:
    return subprocess.run(["git", *arguments], cwd=root, check=True, capture_output=True,
                          text=True).stdout


def check_against_git(root: Path, base: str) -> Report:
    report = Report([], [])
    # What the branch started from, not what the base has done since.
    merge_base = _git(root, "merge-base", base, "HEAD").strip()
    old_paths = _git(root, "ls-tree", "-r", "--name-only", merge_base).splitlines()
    new_paths = _git(root, "ls-files").splitlines()

    old_schemas, new_schemas = [], []
    for path in old_paths:
        if path.endswith(SCHEMA_SUFFIX):
            old_schemas.append(parse_schema(_git(root, "show", f"{merge_base}:{path}"), path))
    for path in new_paths:
        if path.endswith(SCHEMA_SUFFIX) and (root / path).exists():
            new_schemas.append(parse_schema((root / path).read_bytes(), path))
    compare_schemas(old_schemas, new_schemas, report)

    for path in old_paths:
        if not path.endswith((".yaml", ".yml")):
            continue
        old = yaml.safe_load(_git(root, "show", f"{merge_base}:{path}"))
        if not _is_database(old):
            continue
        if not (root / path).exists():
            report.warnings.append(f"{path}: database schema was removed")
            continue
        compare_databases(old, yaml.safe_load((root / path).read_text()), path, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", default="origin/main", help="Git revision to compare with")
    parser.add_argument("--root", type=Path, default=Path("."), help="The repository")
    args = parser.parse_args()
    try:
        report = check_against_git(args.root, args.base)
    except SchemaError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"error: {error.stderr.strip()}", file=sys.stderr)
        return 2
    for warning in report.warnings:
        print(f"warning: {warning}")
    for error in report.errors:
        print(f"error: {error}")
    if report.errors:
        print(f"{len(report.errors)} incompatible schema change(s). See docs/schema.md.")
        return 1
    print("Schemas are compatible with " + args.base)
    return 0


if __name__ == "__main__":
    sys.exit(main())
