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
| **Zero-copy reads** | A getter returns a scalar or a `string_view` into the stored bytes. There is no parsing step and no object to allocate. | Fields are addressable in place; `MessageView` only holds pointers. |
| **O(1) field access** | Reading any field, or learning it is absent, costs a bit test, a `popcount` and a load. | Field IDs index a bitmap; slots are fixed width. |
| **Self-describing** | A reader locates every field without the writer's schema. | Bitmap positions are field IDs, not schema-assigned ordinals. |
| **Backward compatibility** | New code reads old messages: fields it knows but the message lacks read as absent, with their defaults. | Absence is a clear bit. |
| **Forward compatibility** | Old code reads new messages: fields it does not know are ignored. | Sections are sized by the header; addressing does not depend on the reader's field list. |
| **Unknown field preservation** | Fields a reader does not know survive every read-modify-write byte for byte, whatever their class. | Patches and rebuilds operate on storage classes, never on a schema. |
| **Partial updates** | Any present scalar and any equal-length string can be overwritten in place. Other changes rebuild the message. | Slots are inline; offsets exist only for variable-length fields. |
| **Explicit presence** | A field set to `0`, `false` or `""` differs from an absent one. | Presence is a bit, independent of the value. |
| **Canonical encoding** | Equal field sets encode to equal bytes. | Section order, ID order and all widths are determined by the content. |
| **Safe on any input** | Reads of malformed bytes stay inside the buffer. `Validate` detects them. | `Open` checks extents; getters bound every index. |

## Schema evolution rules

Schemas are FlatBuffers `.fbs` files. **Only backward-compatible evolution is supported, by
design.** The rules:

1. **A field keeps its ID and type forever.** The type is never changed: `int32` to `int64`
   needs a new field. Tools do not check this; it is the schema author's responsibility.
2. **New fields are appended.** flatc assigns IDs in declaration order, or `(id: N)` sets them.
3. **A field that is no longer used is deprecated, not removed**, with `(deprecated)`. The
   generator then emits no accessors and keeps the ID reserved, while stored values remain in
   existing messages and are preserved by rewrites. Primary keys and indexed fields cannot be
   deprecated: the generator rejects it.
4. **IDs are limited to `1..2040`.** The generator rejects higher IDs.

A reader that expects a different storage class for an ID sees the field as absent: it never
reinterprets the bytes. `tests/schemas/evolution.fbs` and `SchemaEvolutionTest` in
`tests/message_test.cpp` exercise these rules through generated classes.

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

Enums use the class of their underlying integer. Nested messages are complete messages stored as
opaque `Var` bytes; the generator does not support them yet, nor vectors.

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

`Verify` does not descend into `Var` bytes, since they are opaque without a schema. Use it on
bytes from outside the process. Storage reads trust their own data and skip it.

## API

### Generated classes

`gen/generate_message_class.py` turns `tests/schemas/account.fbs` into
`tests/generated/account.fbs.h`, with three classes per table. This is what application code uses.

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

- `Account` constructs in O(1). Malformed bytes read as an empty message; call
  `gendb::MessageView::Verify` first on untrusted input.
- `AccountBuilder` **borrows strings until `Build()`**. Passing a `std::string` temporary does not
  compile; a `string_view` of something shorter-lived is still a use-after-free.
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
- Message size up to `UINT32_MAX` bytes.
- No message type ID on the wire: the collection identifies the type.

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
| `lib/gendb/message_test.cpp` | Core tests: golden bytes, malformed input, randomized model checks |
| `gen/templates/message_class_template.h.jinja2` | Generated classes |
| `gen/schema_validator.py` | Schema checks; `MAX_FIELD_ID` must match `kMaxFieldId` |
| `tests/message_test.cpp` | Generated API and schema evolution tests |
| `lib/gendb/message_test.cpp`, `ResultBytesArePartOfTheLogFormat` | Pins the bytes patches produce, which the log depends on: see [the storage engine](storage_engine.md#files) |
| `tests/database_test.cpp` | Messages through transactions, indices, sequences and recovery |
