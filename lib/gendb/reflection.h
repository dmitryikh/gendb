#pragma once

namespace gendb {

// Struct used for enum reflection in generated message classes
struct EnumValueInfo {
  const char* name;
  int value;
};

}  // namespace gendb