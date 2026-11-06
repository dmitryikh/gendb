#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace gendb {

// Struct used for enum reflection in generated message classes
struct EnumValueInfo {
  std::string_view name;
  int64_t value;
};

// Struct used for field reflection in generated message classes
struct FieldInfo {
  enum Type { SCALAR, STRING, ENUM, MESSAGE };
  enum ScalarType {
    BOOL,
    INT8,
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,
    FLOAT,
    DOUBLE,
    NOT_SCALAR
  };

  std::string_view name;
  uint32_t field_id;
  Type type;
  // For an enum field, the scalar type of its underlying integer.
  ScalarType scalar_type;
  std::string_view enum_name;
  std::span<const EnumValueInfo> enum_values;
  // A list of this type.
  bool repeated = false;
};

}  // namespace gendb
