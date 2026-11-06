#!/usr/bin/env python3
"""Fuzzes the schema parser, and what is built on it.

    gen/fuzz_schema_parser.py --seconds 60 [--seed 1]
    gen/fuzz_schema_parser.py --compile 30 --compile-commands build/compile_commands.json

Checks that, for anything at all, parsing either works or raises SchemaError, that whatever parses
keeps the numbering invariants, that valid schemas survive being written out and read back and have
code generated from them, and that the evolution check tells growth from breakage. With --compile, the
generated code of random schemas also has to compile.
"""
import argparse
import json
import math
import random
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from generate_message_class import make_template, render_schema
from schema_evolution import Report, compare_schemas
from schema_parser import (BASE_TYPES, INT_RANGES, EnumeratorRef, ParsedEnum, ParsedEnumerator, ParsedField,
                           ParsedMessage, ParsedSchema, SchemaError, parse_schema)

# Names that clash with something, or look as though they might.
NAMES = """
    a b id key value data type name size kind count index item items x y z has_x has_y has_id set_x clear_x
    x_changed builder patch before after message bytes buffer view_x field_x fields_info k_values
    a_b ab a_bc ab_c s8 s4 var bit
""".split()
TYPE_NAMES = "Item Other Config Data Value Thing Color Level Kind State Change Builder Keys ItemList".split()
ENUMERATORS = "kUnknown kFirst kSecond Red RED None a_b x kX None2 VALUE".split()
NAMESPACES = ["", "app", "app.core", "a_b.c1"]
# What mutation splices in.
TOKENS = """
    message enum namespace deprecated true false { } : ; = . - 0 1 2 3 -1 0x 0xff 1e5 1.5 1e999 "" "x" "\\x00"
    "\\ // \n int32 uint64 string bool float double class field 99999999999999999999 2147483648
""".split(" ")
STRING_BYTES = [b"", b"x", b'"', b"\\", b"\n", b"\t", b"\0", b"\xff", b"??=", "имя".encode(), b"\0" * 3, b"%s",
                b"abc" * 40, b"\\n", b"*/", b"//", b'\\"']


def random_default(rng, type_name, enums):
    if rng.random() < 0.5:
        return None
    if type_name in enums:
        return EnumeratorRef(rng.choice(enums[type_name].values).name)
    if type_name == "bool":
        return rng.random() < 0.5
    if type_name == "string":
        return b"".join(rng.choice(STRING_BYTES) for _ in range(rng.randrange(3)))
    if type_name == "float":
        return rng.choice([0.0, 0.25, -1.5, 1e30, 3.4e38, -1.5e-45, 1.0])
    if type_name == "double":
        return rng.choice([0.0, 0.1, -2.5e300, 5e-324, 1.7976931348623157e308, rng.random()])
    low, high = INT_RANGES[type_name]
    return rng.choice([0, 1, low, high, rng.randint(low, high)])


def random_schema(rng) -> ParsedSchema:
    schema = ParsedSchema(rng.choice(NAMESPACES))
    names = rng.sample(TYPE_NAMES, rng.randint(1, 5))
    for name in names[:rng.randint(0, 2)]:
        values = rng.sample(ENUMERATORS, rng.randint(1, 4))
        schema.enums.append(ParsedEnum(name, rng.choice(sorted(INT_RANGES)),
                                       [ParsedEnumerator(v, i) for i, v in enumerate(values)]))
    enums = {e.name: e for e in schema.enums}
    for name in names[len(schema.enums):]:
        message = ParsedMessage(name)
        for i, field_name in enumerate(rng.sample(NAMES, rng.randint(0, 10))):
            type_name = rng.choice(sorted(BASE_TYPES) + sorted(enums))
            message.fields.append(ParsedField(i + 1, field_name, type_name, random_default(rng, type_name, enums),
                                              rng.random() < 0.15))
        schema.messages.append(message)
    return schema


def escape(value: bytes) -> str:
    out = []
    for byte in value:
        if byte in (0x22, 0x5c):
            out.append("\\" + chr(byte))
        elif 0x20 <= byte < 0x7f:
            out.append(chr(byte))
        else:
            out.append(f"\\x{byte:02x}")
    return '"' + "".join(out) + '"'


def write_default(value) -> str:
    if isinstance(value, EnumeratorRef):
        return value.name
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, bytes):
        return escape(value)
    return repr(value)


def write_schema(schema: ParsedSchema, rng=None) -> str:
    """As source text, with random spacing and comments if given a generator."""
    def gap():
        if rng is None:
            return " "
        return rng.choice([" ", "  ", "\n", "\t", " // comment \" { 1\n", "\r\n"])
    parts = []
    if schema.namespace:
        parts += ["namespace", gap(), schema.namespace, ";", gap()]
    for enum in schema.enums:
        parts += ["enum", gap(), enum.name, gap(), ":", gap(), enum.underlying, gap(), "{", gap()]
        for value in enum.values:
            parts += [value.name, gap(), "=", gap(), str(value.value), ";", gap()]
        parts += ["}", gap()]
    for message in schema.messages:
        parts += ["message", gap(), message.name, gap(), "{", gap()]
        for f in message.fields:
            parts += [str(f.number), gap(), f.name, gap(), ":", gap(), f.type, gap()]
            if f.default is not None:
                parts += ["=", gap(), write_default(f.default), gap()]
            if f.deprecated:
                parts += ["deprecated", gap()]
            parts += [";", gap()]
        parts += ["}", gap()]
    return "".join(parts)


def mutate(rng, source: bytes) -> bytes:
    data = bytearray(source)
    for _ in range(rng.randint(1, 4)):
        choice = rng.randrange(6)
        at = rng.randrange(len(data) + 1)
        if choice == 0 and data:
            del data[at % len(data)]
        elif choice == 1:
            data[at:at] = bytes([rng.randrange(256)])
        elif choice == 2 and data:
            data[at % len(data)] = rng.randrange(256)
        elif choice == 3:
            data[at:at] = rng.choice(TOKENS).encode() + b" "
        elif choice == 4 and data:
            end = min(len(data), at + rng.randrange(40))
            data[at:at] = data[at:end]
        elif choice == 5:
            del data[at:]
    return bytes(data)


def check_invariants(schema: ParsedSchema):
    """What must hold for anything that parses, whatever it was."""
    for message in schema.messages:
        assert [f.number for f in message.fields] == list(range(1, len(message.fields) + 1)), message
    for enum in schema.enums:
        assert [v.value for v in enum.values] == list(range(len(enum.values))), enum


def same(a, b) -> bool:
    if isinstance(a, float) and isinstance(b, float):
        return a == b or (math.isnan(a) and math.isnan(b))
    return a == b


def grow(rng, schema: ParsedSchema) -> ParsedSchema:
    """A compatible next version."""
    next_version = parse_schema(write_schema(schema))
    for message in next_version.messages:
        for f in message.fields:
            if rng.random() < 0.2:
                f.deprecated = True
        for i in range(rng.randrange(3)):
            message.fields.append(ParsedField(len(message.fields) + 1, f"added_{i}", "int32"))
    for enum in next_version.enums:
        enum.values.append(ParsedEnumerator("kAdded", len(enum.values)))
    next_version.messages.append(ParsedMessage("BrandNew"))
    return next_version


def break_it(rng, schema: ParsedSchema) -> ParsedSchema:
    """An incompatible next version, or None if there is nothing to break."""
    broken = parse_schema(write_schema(schema))
    candidates = [m for m in broken.messages if m.fields]
    if not candidates:
        return None
    message = rng.choice(candidates)
    f = rng.choice(message.fields)
    kind = rng.randrange(5)
    if kind == 0:
        message.fields.pop()
    elif kind == 1:
        f.name = "renamed_field"
    elif kind == 2:
        f.type = "bool" if f.type != "bool" else "int32"
        f.default = None
    elif kind == 3:
        f.default = b"changed" if f.default != b"changed" else b"other"
    else:
        broken.messages.remove(message)
    return broken


def one_round(rng, template, directory: Path, generate: bool):
    schema = random_schema(rng)
    source = write_schema(schema, rng)
    try:
        parsed = parse_schema(source)
    except SchemaError:
        return "rejected"
    check_invariants(parsed)
    again = parse_schema(write_schema(parsed))
    assert same(write_schema(again), write_schema(parsed)), source

    # Whatever was written is what is read.
    assert parsed.namespace == schema.namespace
    assert [m.name for m in parsed.messages] == [m.name for m in schema.messages]
    for ours, theirs in zip(parsed.messages, schema.messages):
        for a, b in zip(ours.fields, theirs.fields):
            assert (a.number, a.name, a.type, a.deprecated) == (b.number, b.name, b.type, b.deprecated), source
            assert same(a.default, b.default), (a.default, b.default, source)

    # Evolution: growth passes, and breakage is noticed.
    report = Report([], [])
    compare_schemas([parsed], [grow(rng, parsed)], report)
    assert report.errors == [], report.errors
    broken = break_it(rng, parsed)
    if broken is not None:
        report = Report([], [])
        compare_schemas([parsed], [broken], report)
        assert report.errors, write_schema(broken)

    # Mutations: an error or a schema, and nothing else.
    for _ in range(8):
        mutated = mutate(rng, source.encode())
        try:
            result = parse_schema(mutated)
        except SchemaError:
            continue
        check_invariants(result)
        assert write_schema(parse_schema(write_schema(result))) == write_schema(result)

    # Bytes that never were a schema.
    try:
        parse_schema(rng.randbytes(rng.randrange(64)))
    except SchemaError:
        pass

    if generate:
        path = directory / "fuzz.gendb"
        path.write_text(source, encoding="utf-8", errors="surrogateescape", newline="")
        render_schema(template, path)
    return "accepted"


def run(seconds: float, seed: int) -> dict:
    rng = random.Random(seed)
    template = make_template()
    counts = {"accepted": 0, "rejected": 0}
    deadline = time.monotonic() + seconds
    with tempfile.TemporaryDirectory() as directory:
        while time.monotonic() < deadline:
            counts[one_round(rng, template, Path(directory), counts["accepted"] % 8 == 0)] += 1
    return counts


def compile_flags(compile_commands: Path) -> list:
    for entry in json.loads(compile_commands.read_text()):
        if entry["file"].endswith("lib/gendb/message.cpp"):
            arguments = entry.get("arguments") or shlex.split(entry["command"])
            flags, i = [], 0
            while i < len(arguments):
                if arguments[i] in ("-isystem", "-I"):
                    flags += arguments[i:i + 2]
                    i += 2
                    continue
                if arguments[i].startswith(("-I", "-std=")):
                    flags.append(arguments[i])
                i += 1
            return flags
    raise SystemExit("message.cpp is not in " + str(compile_commands))


def compile_random(count: int, seed: int, compile_commands: Path) -> int:
    flags = compile_flags(compile_commands)
    rng = random.Random(seed)
    template = make_template()
    compiled = 0
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        while compiled < count:
            source = write_schema(random_schema(rng), rng)
            path = directory / "fuzz.gendb"
            path.write_text(source, encoding="utf-8", errors="surrogateescape", newline="")
            try:
                code = render_schema(template, path)
            except SchemaError:
                continue
            header = directory / "fuzz.gendb.h"
            header.write_text(code)
            result = subprocess.run(["clang++", *flags, "-fsyntax-only", "-x", "c++", str(header)],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                print(source, file=sys.stderr)
                print(result.stderr[:4000], file=sys.stderr)
                raise SystemExit("Generated code does not compile")
            compiled += 1
    return compiled


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--compile", type=int, default=0, help="How many random schemas to compile")
    parser.add_argument("--compile-commands", type=Path, help="To find the compiler flags")
    args = parser.parse_args()
    seed = random.randrange(2**32) if args.seed is None else args.seed
    print(f"Seed {seed}")
    if args.compile:
        print(f"Compiled {compile_random(args.compile, seed, args.compile_commands)} random schemas")
    counts = run(args.seconds, seed)
    print(f"Ran {counts['accepted']} schemas, and {counts['rejected']} rejected as invalid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
