#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "gendb/message_base.h"
namespace gendb {
class MessageBuilder {
 public:
  MessageBuilder();
  MessageBuilder(const MessageBase& src);
  template <typename T>
    requires IsSupportedScalar<T>
  void AddField(int field_id, T value) {
    if constexpr (!kLittleEndianArch) {
      // Convert from host order to LittleEndian.
      value = std::byteswap(value);
    }
    AddFieldRaw(field_id,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), sizeof(value)));
  }

  void AddStringField(int field_id, std::string_view value) {
    AddFieldRaw(field_id, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data()),
                                                   value.size()));
  }
  void AddFieldRaw(int field_id, std::span<const uint8_t> data);
  void ClearField(int field_id);
  std::vector<uint8_t> Build();
  void MergeFields(const MessageBase& rhs);
  bool NeedRebuild() const { return !_fields.empty(); }
  std::map<int, std::span<const uint8_t>> GetFieldData() const;

 private:
  std::map<int, std::string> _fields;
};
}  // namespace gendb
