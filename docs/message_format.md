# Properties

1. (Almost) zero copy. Scalar fields (int, float) can be unaligned 
2. Random field access (no full msg parse)
3. Serialized massages shouldn't store unset fields.
4. Support backward and forward compatibility by adding new fields and deprecating old ones.
5. Support transparent passing the unknown field (deserialize-serialize loops shouldn't loss fields).
6. Support partial updates (including partial updates with unknown fields).
7. Support unknown enum values.
8. Flat messages are supported. Format should be extendable for adding vectors & nested messages in future.

# Solution

## Supported types
    UType : idk
    Bool : supported
    Byte : supported
    UByte : supported
    Short : supported
    UShort : supported
    Int : supported
    UInt : supported
    Long : supported
    ULong : supported
    Float : supported
    Double : supported
    String : supported
    Vector : not supported
    Obj : not supported
    Union : not supported
    Array : not supported
    Vector64 : not supported


Flatbuffer's VTable with type tag.

[Vtable]
[presence bits]
[type tags]
[value1]
[value2]
[value3]

field # 4:
bit 7 & 8 - quick presence check.
count 1 bits - quick offset computation
count all 1 bits - number of fields presented.
count the most left 1bit - the latest field. If it's outside - check the type.
type tags only for presented fields.


[2 bytes size]
[1 byte tag + 2 bytes offset + 1 byte padding]
..
[]

4 byte blocks
some types - two blocks (8 bytes).