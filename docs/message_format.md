# Message format

This is the specification of the bytes GenDB stores for every record, and the reference for
anyone changing `lib/gendb/message.h`, `lib/gendb/message.cpp` or the generator in `gen/`.
[message_format.html](message_format.html) shows the same layout interactively: open it in a
browser, edit fields, and watch the bytes change.

## Principles

These are the guarantees the format exists to provide. A change that breaks one of them is a
format redesign, not a refactoring.

| Principle | What it means | How the format provides it |
| --- | --- | --- |
| **Zero-copy reads** | A getter returns a scalar, a `string_view` into the stored bytes, or a view of a nested message or a list. There is no parsing step and no object to allocate. | Fields are addressable in place; views only hold pointers. |
| **O(1) field access** | Reading any field, or learning it is absent, costs a bit test, a `popcount` and a load. | Field IDs index a bitmap; slots are fixed width. |
| **Self-describing** | A reader locates every field without the writer's schema. | Bitmap positions are field IDs, not schema-assigned ordinals. |
| **Backward compatibility** | New code reads old messages: fields it knows but the message lacks read as absent, with their defaults. | Absence is a clear bit. |
| **Forward compatibility** | Old code reads new messages: fields it does not know are ignored. | Sections are sized by the header; addressing does not depend on the reader's field list. |
| **Unknown field preservation** | Fields a reader does not know survive every read-modify-write byte for byte, whatever their class. | Patches and rebuilds operate on storage classes, never on a schema. |
| **Partial updates** | Any present scalar and any equal-length string can be overwritten in place. Other changes rebuild the message. | Slots are inline; offsets exist only for variable-length fields. |
| **Explicit presence** | A field set to `0`, `false` or `""` differs from an absent one. | Presence is a bit, independent of the value. |
| **Canonical encoding** | Equal field sets encode to equal bytes. | Section order, ID order and all widths are determined by the content. |
| **Safe on any input** | Reads of malformed bytes stay inside the buffer. `Validate` and generated `Verify` detect them. | `Open` checks extents; getters bound every index. |

## Schema evolution rules

Schemas are [`.gendb` files](schema.md). **Only backward-compatible evolution is supported, by
design.** The rules:

1. **A field keeps its ID and type forever.** The type is never changed: `int32` to `int64` needs a new
   field. `gen/schema_evolution.py` checks this against what a branch started from.
2. **New fields are appended.** A field states its ID, and IDs go up by one, which the parser enforces.
3. **A field that is no longer used is deprecated, not removed**, with `deprecated`. The generator then
   emits no accessors and keeps the ID reserved, while stored values remain in existing messages and are
   preserved by rewrites. Primary keys and indexed fields cannot be deprecated: the generator rejects it.
4. **IDs are limited to `1..2040`.** The parser rejects higher IDs.

A nested message is a message, so it grows by these same rules, and a parent's reader keeps what it does
not know inside it. Whether a field is `repeated` is part of its type, and never changes either. Unlike
a change of storage class, a mistake there could not be seen in the bytes, as a nested message, a list
and a string are all `Var`: `gen/schema_evolution.py` is what catches it.

A reader that expects a different storage class for an ID sees the field as absent: it never
reinterprets the bytes. `tests/schemas/evolution.gendb` and `SchemaEvolutionTest` in
`tests/message_test.cpp` exercise these rules through generated classes.

**Defaults are not part of the format.** A schema's default is what a generated getter returns for an
absent field. It is not stored, and the field stays absent: presence in the bitmaps is the only thing that
says a field is set.

## Wire layout

Integers are little-endian. A message is a sequence of sections without padding:

```text
[header: 9 or 12 bytes]
[S8 presence bitmap: L8 bytes]
[S4 presence bitmap: L4 bytes]
[Bit presence bitmap: LB bytes]
[Bit value bitmap: LB bytes]
[Var presence bitmap: LV bytes]
[S8 slots: 8 * N8]
[S4 slots: 4 * N4]
[Var end offsets: width * NV]
[payload: concatenated Var field bytes, in ID order]
```

### Header

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 1 | Version, `1` |
| 1 | 1 | Flags: bits 0–1 end-offset width code (`0` = 1 byte, `1` = 2, `2` = 4; `3` rejected); bit 2 `WIDE_COUNTS`; other bits must be zero |
| 2 | 1 | `L8`: S8 bitmap length in bytes |
| 3 | 1 | `L4`: S4 bitmap length in bytes |
| 4 | 1 | `LB`: Bit bitmap length in bytes (presence and values) |
| 5 | 1 | `LV`: Var bitmap length in bytes |
| 6 | 1 or 2 | `N8`: number of S8 fields |
| 7 or 8 | 1 or 2 | `N4`: number of S4 fields |
| 8 or 10 | 1 or 2 | `NV`: number of Var fields |

Counts are one byte each (9-byte header) unless any count exceeds 255; then `WIDE_COUNTS` is set
and they are `u16` (12-byte header). Bools have no slots, so their count is not stored.

### Storage classes

| Class | Types | Value location |
| --- | --- | --- |
| `S8` | int64, uint64, double | 8-byte slot |
| `S4` | int8, uint8, int16, uint16, int32, uint32, float | 4-byte slot, value in the low bytes, rest zero |
| `Bit` | bool | the value bitmap; no slot |
| `Var` | strings, bytes, nested messages | end offset + payload slice |

Enums use the class of their underlying integer. Nested messages and [lists](#nested-messages-and-lists)
are `Var` bytes with a structure of their own.

A field ID is present in at most one class. Bit `i` of a bitmap (byte `i / 8`, bit `i % 8`)
represents field ID `i + 1`.

### Addressing

```text
index      = popcount(class_bitmap & ((1 << (id - 1)) - 1))   // one word for id <= 64
S8 value   = load64(S8_slots + 8 * index)
S4 value   = load<T>(S4_slots + 4 * index)
bool       = value_bitmap bit (id - 1)
Var start  = index == 0 ? 0 : end[index - 1]
Var end    = end[index]
```

There is no padding and no alignment guarantee, so every load is a `memcpy`. Never cast a slot or
payload pointer to a typed pointer.

### Worked example

A `Position` with `position_id = 1001`, `account_id = 42`, `volume = 250`,
`instrument = "EURUSD"`, `open_price = 1.2345`, `direction = 1`, in 38 bytes. The test
`DocumentedPositionExample` pins these bytes.

```text
01 00 00 01 00 01 00 05 01     header: version 1, 1-byte offsets, L4 = 1, LV = 1, N4 = 5, NV = 1
37                             S4 presence: 0b00110111 = IDs 1, 2, 3, 5, 6
08                             Var presence: 0b00001000 = ID 4
e9 03 00 00                    S4 slot 0: ID 1, 1001
2a 00 00 00                    S4 slot 1: ID 2, 42
fa 00 00 00                    S4 slot 2: ID 3, 250
19 04 9e 3f                    S4 slot 3: ID 5, 1.2345f
01 00 00 00                    S4 slot 4: ID 6, 1
06                             end offset of Var 0 (ID 4)
45 55 52 55 53 44              payload: "EURUSD"
```

Reading ID 5: bit 4 of `0x37` is set, and `popcount(0x37 & 0b1111) = 3`, so the value is S4 slot 3.

### Nested messages and lists

**The layout above does not change for them.** They are payloads of `Var` fields, so a message without
them is what it always was, reading a scalar costs what it always did, and the version byte stays `1`.
What is inside a `Var` field takes the schema to know, which is why a reader that does not know the field
can only keep its bytes, and why a patch replaces it as a whole.

| Field | Bytes of the `Var` field |
| --- | --- |
| A message | A complete message, as described here |
| A list of `int8` to `uint64`, `float`, `double`, enums | The elements, each of its type's natural size, without a header: the field's size gives the count. An enum takes its underlying integer's size. A `bool` is one byte, `0` or `1` |
| A list of `string` or messages | `[header: varint]` `[end offsets: width * count]` `[payload]`, with the header being `count * 4 + width code`, the codes as in the [message header](#header) |
| An empty list of any type | Zero bytes |

A field that is not set has no bit in the bitmap, and differs from one set to an empty list or an empty
message, as with every other type. An empty message is 9 bytes, like any message with no fields.

Nothing is aligned, so a list of scalars cannot be a `span<const double>`. `ScalarList` reads each element
through a `memcpy`, which compiles to a plain load on the usual machines, and copies out with
`ToVector()`.

For a list of strings or messages, the offsets work as in a message: index `i` runs from `end[i - 1]` (0
for the first) to `end[i]`, the last end frames the payload so that truncation and trailing bytes are
found in O(1), and malformed bytes read as an empty list. An element costs its offset on top of its own
bytes, and a message element carries its own header and bitmaps: a message with one string in it takes 11
bytes on top of the string.
There is no sharing of repeated strings or of a schema between elements. It would save room on long lists,
but would mean elements are no longer messages that can be read, copied and verified on their own.

An `Order` with `order_id = 7`, `customer = {name: "Ann"}`, `tags = ["x", "yz"]` and
`quantities = [-1, 300]`, in 46 bytes. The test `DocumentedOrderExample` pins these bytes.

```text
01 00 01 00 00 01 01 00 03     header: L8 = 1, LV = 1, N8 = 1, NV = 3
01                             S8 presence: ID 1
a8                             Var presence: 0b10101000 = IDs 4, 6, 8
07 00 00 00 00 00 00 00        S8 slot 0: ID 1, 7
0e 14 18                       end offsets: 14, 20, 24
01 00 00 00 00 01 00 00 01     ID 4, a message: header: LV = 1, NV = 1
01                                                presence: ID 1
03                                                end offset
41 6e 6e                                          "Ann"
08                             ID 6, strings: header: 2 * 4 + code 0
01 03                                         end offsets
78 79 7a                                      "x", "yz"
ff ff 2c 01                    ID 8, int16s: -1, 300
```

### Size

```text
size = header + L8 + L4 + 2 * LB + LV + 8 * N8 + 4 * N4 + width * NV + payload
```

| Record | Bytes | Notes |
| --- | ---: | --- |
| Position (5 scalars + 1 string) | 38 | 9 + 2 bitmaps + 20 slots + 1 offset + 6 payload |
| AccountFull (8 fields, 4 strings) | 98 | |
| AccountSparse (u64 + bool) | 20 | the bool costs no slot |
| Wide (64 × u64) | 529 | 17 bytes of framing for 512 bytes of data |
| WideSparse (8 of 64 × u64) | 81 | |

The cost model favours dense field IDs: bitmaps grow with the highest ID present in each class, so
a single field 2000 costs 250 bitmap bytes.

### Framing, validation and canonical form

`end[last]` must equal the payload size; with no Var fields the payload is empty. Total size is
therefore determined by the header and bitmaps, so every truncation and every trailing byte is
rejected in O(1).

`Open` reads only the header and the first word of the S8, S4 and Var bitmaps. It does not check
that header counts match the bitmaps (that would cost a popcount per bitmap), so getters bound
every slot and offset index by the header count. A contradicting message reads as absent or as
another field's bytes, but never outside the buffer.

`Validate()` and `MessageView::Verify(bytes)` additionally check, in O(number of Var fields) for
IDs up to 64 and O(bitmap bytes) otherwise:

- header counts equal bitmap popcounts;
- classes are disjoint, and no Bit value is set without Bit presence;
- end offsets are monotonic;
- canonical form: each non-empty bitmap ends in a non-zero byte, the end-offset width is the
  smallest that holds the payload size, and `WIDE_COUNTS` is set only when needed.

`MessageView::Verify` does not descend into `Var` bytes, since they are opaque without a schema.
Generated classes have a `Verify` that does, in time linear in the size of the message: `Order::Verify(bytes)`
checks the message, each message nested in it, each list and each of its elements, as to all of the
above. For a list, that adds the shortest header, the smallest width, monotonic offsets, a whole number of
elements, and `bool`s of `0` or `1`. It does not go deeper than `gendb::kMaxMessageDepth` levels, 64, unless
told to, so that a hostile message cannot overflow the stack. It cannot look into a field it does not know.

Use generated `Verify` on bytes from outside the process. Storage reads trust their own data and skip it.

## API

### Generated classes

`gen/generate_message_class.py` turns `tests/schemas/account.gendb` into
`tests/generated/account.gendb.h`, with three classes per message. This is what application code uses.

```cpp
AccountBuilder builder;
builder.set_account_id(42).set_name("Ann").set_is_active(true);
builder.AddStringField(1000, "unknown field");   // the core API stays available
std::vector<uint8_t> bytes = builder.Build();

Account account{bytes};                          // zero-copy view; the bytes must outlive it
account.account_id();                            // 42
account.has_age();                               // false
account.name();                                  // string_view into bytes
account.View().GetString(1000);                  // any field by ID, through the core API

auto patch = AccountPatchBuilder().set_age(30).clear_name().Build();
gendb::ApplyPatch(patch, bytes);
```

[The API reference](api.md#nested-messages-and-lists) has the same for nested messages and lists.

- `Account` constructs in O(1). Malformed bytes read as an empty message; call `Account::Verify`
  first on untrusted input.
- `AccountBuilder` **borrows strings until `Build()`**. Passing a `std::string` temporary does not
  compile; a `string_view` of something shorter-lived is still a use-after-free. Nested messages and
  repeated fields are the exception: they are serialized or copied when given.
- `AccountPatchBuilder` **owns its strings**, because a patch outlives its arguments.

### Core API

`gendb/message.h`, for code without a schema.

```cpp
gendb::MessageBuilder builder;
builder.AddField<int32_t>(1, 1001);
builder.AddStringField(4, instrument);   // borrowed until Build()/SerializeTo() returns
builder.AddField<bool>(7, true);
gendb::Bytes bytes = builder.Build();    // or SerializedSize() + SerializeTo(span)

auto view = gendb::MessageView::Open(bytes);  // std::optional; O(1), header only
int32_t id = view->Get<1, int32_t>();    // compile-time ID: bit test + popcount + load
std::string_view s = view->GetString<4>();
view->Get<int32_t>(runtime_id, -1);      // runtime ID variants exist for every getter
view->ForEach([](const gendb::Field& f) { /* every field, known or unknown */ });

auto mutable_view = gendb::MutableView::Open(bytes);
mutable_view->Set<1, int32_t>(1002);     // O(1) in place; false if absent or another class

builder.AddMessageField(9, child_builder);    // serialized now, and owned
builder.AddRepeated<double>(10, 1.5);         // appended; gendb/list.h reads it
builder.AddRepeatedString(11, instrument);    // appended, and owned

gendb::MessagePatch patch;
patch.SetString(4, "GBPUSD-long");       // borrowed; CopyString owns the value
patch.Remove(7);
gendb::ApplyPatch(patch, bytes);         // in place when possible, otherwise rebuilds
```

### Patch paths

| Path | Used when | Cost |
| --- | --- | --- |
| `ApplyPatchInplace` | Every change overwrites a present field of the same class and length | O(changes). All or nothing: it preflights first and writes nothing on failure. |
| `Rebuild`, splice | One present Var field changes length, and the offset width stays the same | Header, bitmaps and slots are copied as a block, later offsets shift, the payload is joined around the new bytes. |
| `Rebuild`, re-encode | Anything else: insertion, removal, class change, several changes, offset width change | Goes through the builder, borrowing untouched payloads from the source. |

Failures: the read path, `Open`, getters, `Validate` and `MutableView` are `noexcept`. The builder
and `Rebuild` throw on programming errors: ID out of range, malformed source, output buffer too
small. In-place patching is not atomic for concurrent readers.

## Limits

- Field IDs `1..2040`. IDs above 64 use a byte-wise rank: slower, still allocation-free.
- Message size up to `UINT32_MAX` bytes, nested messages and lists included.
- No message type ID on the wire: the collection identifies the type.
- A patch sets a field as a whole, so changing one element of a list or one field of a nested message
  rewrites that field, and the parent's offsets if its size changes. Patches do not reach inside.
- A primary key or an index holds scalars and strings of the message itself, not what is nested or
  repeated.

## Changing the format

Stored data depends on these bytes. When changing `message.h` or `message.cpp`:

1. Keep the principles above true, and update this file and `message_format.html`, whose
   JavaScript encoder is a second implementation of the layout.
2. Any change to the bytes needs a new version byte and a migration plan. `Open` rejects unknown
   versions and unknown flag bits, so old readers refuse new messages rather than misread them.
3. Update `ExactWireBytes` and `DocumentedPositionExample` in `lib/gendb/message_test.cpp` only
   together with the version.
4. Run the tests under ASan and UBSan and the benchmarks; see [docker_build.md](docker_build.md)
   and [benchmarks/README.md](../benchmarks/README.md).

| Where | What |
| --- | --- |
| `lib/gendb/message.h`, `message.cpp` | Core: `MessageView`, `MutableView`, `MessageBuilder`, `MessagePatch` |
| `lib/gendb/list.h`, `message.cpp` | Repeated fields: `ScalarList`, `StringList`, `MessageList`, `ListBuilder` |
| `lib/gendb/message_test.cpp` | Core tests: golden bytes, malformed input, randomized model checks |
| `gen/templates/message_class_template.h.jinja2` | Generated classes |
| `gen/schema_validator.py` | Schema checks; `MAX_FIELD_ID` must match `kMaxFieldId` |
| `tests/message_test.cpp` | Generated API and schema evolution tests |
| `tests/nested_test.cpp` | Nested messages and lists: golden bytes, malformed input, `Verify`, evolution |
| `lib/gendb/message_test.cpp`, `ResultBytesArePartOfTheLogFormat` | Pins the bytes patches produce, which the log depends on: see [the storage engine](storage_engine.md#files) |
| `tests/database_test.cpp` | Messages through transactions, indices, sequences and recovery |
