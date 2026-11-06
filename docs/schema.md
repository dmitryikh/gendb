# Schema language

Records are described in `.gendb` files. The generator needs nothing but Python: there is no external
schema compiler. The parser is `gen/schema_parser.py`.

```text
// Comments run to the end of the line.
namespace app.trading;

enum Direction : uint32 {
  kUnknown = 0;
  kBuy = 1;
  kSell = 2;
}

message Position {
  1 position_id: int32;
  2 instrument: string = "EURUSD";
  3 volume: int32 = 1;
  4 direction: Direction = kBuy;
  5 open_price: float;
  6 old_comment: string deprecated;
}
```

A file has at most one namespace, at its start, and any number of messages and enums. A message can
use the enums of its own file.

| Types | |
| --- | --- |
| Integers | `int8` `uint8` `int16` `uint16` `int32` `uint32` `int64` `uint64` |
| Other scalars | `bool` `float` `double` |
| Text and bytes | `string` |
| Enums | Declared with one of the integer types |

## Evolution as syntax

Schemas only grow, as stored data outlives any version of the program. The syntax is made so that most
mistakes against this are errors:

- **A field states its number, which is the one thing that is stored.** Numbers go up by one from 1, in
  the order written.
- So **a new field can only go at the end**, as anything else leaves numbers out of order.
- So **a field cannot be removed**, as that leaves a gap. It stays, marked `deprecated`: it then has no
  accessors, its number stays taken, and stored values survive rewrites.
- **Enumerators follow the same rule from 0.**

A message has at most 2040 fields, which is what the [message format](message_format.md) can address.

## What the syntax cannot see

Whether a field used to say something else takes knowing what the file said before.
`gen/schema_evolution.py` compares with what the branch started from, and CI runs it as the `schema`
job:

```bash
python3 gen/schema_evolution.py --base origin/main
```

| Change | Result |
| --- | --- |
| New field, enumerator, message, enum, collection, sequence at the end, index | Fine |
| Marking a field `deprecated` | Fine |
| Moving a declaration to another file | Fine: they are compared by qualified name |
| Removing a field, message, enum or enumerator | Error |
| Renaming a field or enumerator | Error |
| Changing a field's type, or an enum's | Error |
| Changing or adding a default of an existing field | Error |
| Undoing `deprecated` | Error |
| Changing a collection's type or primary key | Error |
| Removing, reordering or retyping sequences, which are stored by their place in the list | Error |
| Removing a collection, or making it ephemeral | Warning: its data stays, as a [foreign collection](storage_engine.md#collections-the-schema-does-not-have) |
| Making an index unique | Warning: existing records are not checked |

The `schema-evolution` skill in `.claude/skills` gives an AI assistant these rules, with the allowed form
of each change.

## Defaults

```text
3 volume: int32 = 1;
```

A default is what a getter returns for a field that is not set. **It does not make the field set:**

```cpp
Position position{PositionBuilder().set_position_id(1).Build()};
position.volume();      // 1, the default
position.has_volume();  // false

Position explicit_one{PositionBuilder().set_position_id(1).set_volume(1).Build()};
explicit_one.volume();      // 1
explicit_one.has_volume();  // true
```

Without a default, a getter returns the type's zero. Defaults are not stored, which saves the room, but
this has consequences:

- **They cannot change**, as that would change what every record without the field means.
- **Clearing a field goes back to its default.**
- **A primary key field must be set.** A default does not count, as the key must be in the record.
- **An index holds only records with the field set.** A record relying on a default is not found through
  an index on that field.
- **`<field>_changed()` compares set and unset as different**, even if the value read is the same.

| Literal | For |
| --- | --- |
| `true` `false` | `bool` |
| `42` `-1` `0xff` | Integers, checked against the type's range |
| `1.5` `-2` `1e10` | `float` and `double`, which must be finite and fit the type |
| `"text"` | `string`. Escapes are `\"` `\\` `\n` `\r` `\t` `\0` and `\xHH`, and text is UTF-8 |
| `kBuy` | An enumerator of the field's enum |

## Names

Generated code must compile, so names are checked here rather than left to fail there. Messages and enums
are PascalCase, and fields and namespaces are snake_case. C++ keywords and common macros are not allowed,
nor are names that would clash with generated code: a field named after its message, `has_x` next to `x`,
`field`, `view`, `has_field` and `field_count`, and a message named `Field`, `Message` or `XBuilder` next
to `X`.

## Generated code

`schema.gendb` generates `schema.gendb.h`, with an `X`, `XBuilder` and `XPatchBuilder` for each message.
See [api.md](api.md).

## Testing

`gen/schema_parser_test.py` covers the language, and `tests/message_test.cpp` covers defaults through
generated code. `gen/fuzz_schema_parser.py` throws random and mutated input at the parser, which must either
work or raise `SchemaError`. It also checks that whatever parses keeps the numbering rules, that schemas
survive being written out and read back, that the evolution check tells growth from breakage, and, with
`--compile`, that code generated from random schemas compiles. The `fuzz` CI job runs it.
