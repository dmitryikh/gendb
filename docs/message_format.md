# Message Format v2

## Goals

1. Zero-copy reads for generated known fields.
2. Compact encoding for sparse and future field ids.
3. No storage for unset fields.
4. Forward and backward compatibility for schema evolution.
5. Unknown-field passthrough during read-modify-write cycles.
6. Partial updates with in-place overwrite when byte length is unchanged.

## Wire layout

All integers are little-endian.

```text
[version:u8]
[flags:u8]
[fixed1_word_count:u8]
[fixed4_word_count:u8]
[fixed8_word_count:u8]
[reserved:u8]
[sparse_count:u16]
[fixed1_payload_size:u16]
[fixed4_payload_size:u16]
[fixed8_payload_size:u16]
[sparse_dir_size:u16]
[total_size:u32]
[fixed1_presence_bitmap]
[fixed4_presence_bitmap]
[fixed8_presence_bitmap]
[fixed1_payload]
[fixed4_payload]
[fixed8_payload]
[sparse_directory]
[sparse_payload]
```

### Fixed section

The fixed section stores only known fixed-size fields from the current schema:

- one presence bit per fixed-known field within its width class
- payload bytes packed separately for `1`, `4`, and `8` byte storage classes
- no per-field offsets or type tags

Generated readers use schema metadata to:

- select the storage class at compile time
- test the presence bit in that class bitmap
- compute the byte offset with `popcount` over earlier set bits in the same class
- return a zero-copy slice or scalar value

Logical `2`-byte fixed fields are stored in the `4`-byte section using a 4-byte slot with the
value encoded in the low 2 bytes.

Unknown fields are never stored in the fixed section.

### Sparse section

The sparse section stores:

- known variable-size fields
- unknown future fields
- any field that is not eligible for fixed packing

Entries are sorted by field id and delta encoded:

```text
[field_id_delta:varint]
[payload_offset_delta:varint]
[payload_len:varint]
```

Payload bytes are stored in one blob after the sparse directory.

Unknown fields are opaque byte slices. The runtime does not store a kind/type tag for them.

## Compatibility

- Known fields from the current schema can move between fixed and sparse storage as the schema evolves.
- Unknown fields are preserved byte-for-byte unless explicitly updated or removed.
- Large future field ids stay compact because sparse storage cost depends on present fields, not `max_field_id`.

## Patching

Two patch paths are supported:

- in-place overwrite
- rebuild

In-place overwrite is allowed only when:

- the field already exists in the message
- the patch does not remove the field
- the new payload has the same byte length as the old payload

This rule is the same for known and unknown fields.

Rebuild is used for:

- removals
- insertions
- payload length changes

During rebuild, untouched unknown sparse fields are copied through unchanged.
