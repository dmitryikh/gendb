#pragma once

#include <span>
#include <string_view>

namespace gendb {

// Struct used for enum reflection in generated message classes
struct EnumValueInfo {
  std::string_view name;
  int value;
};

// Struct used for field reflection in generated message classes
struct FieldInfo {
  std::string_view name;
  int field_id;
  enum Type { SCALAR, STRING, ENUM } type;
  enum ScalarType { UINT64, INT32, BOOL, FLOAT, UNKNOWN_SCALAR } scalar_type;
  std::string_view enum_name;
  std::span<const EnumValueInfo> enum_values;
};

}  // namespace gendb