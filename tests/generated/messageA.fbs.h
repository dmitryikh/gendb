
// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "gendb/bits.h"
#include "gendb/message_base.h"
#include "gendb/message_builder.h"
#include "gendb/message_patch.h"
#include "gendb/reflection.h"

// Enum definitions
namespace gendb::tests::primitive {
enum class KeyEnum : uint32_t {
  kUnknown = 0,
  kFirstValue = 1,
  kSecondValue = 2,
};
}  // namespace gendb::tests::primitive

// Enum value arrays for reflection
static constexpr std::array<gendb::EnumValueInfo, 3> kKeyEnumValues = {
    gendb::EnumValueInfo{"kUnknown", 0},
    gendb::EnumValueInfo{"kFirstValue", 1},
    gendb::EnumValueInfo{"kSecondValue", 2},
};

// Message classes
namespace gendb::tests::primitive {
// GeneratedClass
class MessageA : private gendb::MessageBase {
 public:
  enum Field : int { Key = 1, Data = 2, MaxFields };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      Key,
  });

  static constexpr std::array<gendb::FixedFieldDescriptor, 1> kFixedFieldDescriptors = {
      gendb::FixedFieldDescriptor{Key, 4, gendb::FixedStorageWidth::Byte4, 0, 0, 0u, 0},
  };

  static constexpr std::array<uint8_t, gendb::kFixedStorageClassCount> kFixedWordCounts = {
      0,
      1,
      0,
  };

  static constexpr gendb::MessageLayout kLayout{kFixedFieldDescriptors, kFixedWordCounts};

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"key", Key, gendb::FieldInfo::ENUM, gendb::FieldInfo::UNKNOWN_SCALAR,
                       "gendb::tests::primitive::KeyEnum",
                       std::span<const gendb::EnumValueInfo>(kKeyEnumValues)},
      gendb::FieldInfo{"data", Data, gendb::FieldInfo::STRING, gendb::FieldInfo::UNKNOWN_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  MessageA() : MessageBase(&kLayout) {}
  MessageA(std::span<const uint8_t> span) : MessageBase(span, &kLayout) {}

  bool has_key() const { return HasField(Key); }
  gendb::tests::primitive::KeyEnum key() const {
    return static_cast<gendb::tests::primitive::KeyEnum>(
        ReadFixedScalarFieldByDescriptor<kFixedFieldDescriptors[0], uint32_t>(0));
  }
  bool has_data() const { return HasField(Data); }
  std::string_view data() const { return ReadSparseKnownStringField(Data, ""); }

  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  static constexpr const gendb::MessageLayout* LayoutPtr() { return &kLayout; }
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class MessageABuilder;
};

class MessageABuilder : public gendb::MessageBuilder {
 public:
  MessageABuilder() : MessageBuilder(MessageA::LayoutPtr()) {}
  MessageABuilder(MessageA& obj) : MessageBuilder(obj) {}

  MessageABuilder& set_key(gendb::tests::primitive::KeyEnum value) {
    AddField<uint32_t>(MessageA::Key, static_cast<uint32_t>(value));
    return *this;
  }
  MessageABuilder& set_data(std::string_view value) {
    AddStringField(MessageA::Data, value);
    return *this;
  }

  MessageABuilder& clear_key() {
    ClearField(MessageA::Key);
    return *this;
  }
  MessageABuilder& clear_data() {
    ClearField(MessageA::Data);
    return *this;
  }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class MessageAPatchBuilder {
 public:
  MessageAPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  MessageAPatchBuilder&& set_key(gendb::tests::primitive::KeyEnum value) && {
    _builder.set_key(value);
    SetFieldBit(modified, MessageA::Key);
    UnsetFieldBit(removed, MessageA::Key);
    return std::move(*this);
  }
  MessageAPatchBuilder&& clear_key() && {
    SetFieldBit(removed, MessageA::Key);
    UnsetFieldBit(modified, MessageA::Key);
    _builder.clear_key();
    return std::move(*this);
  }
  MessageAPatchBuilder&& set_data(std::string_view value) && {
    _builder.set_data(value);
    SetFieldBit(modified, MessageA::Data);
    UnsetFieldBit(removed, MessageA::Data);
    return std::move(*this);
  }
  MessageAPatchBuilder&& clear_data() && {
    SetFieldBit(removed, MessageA::Data);
    UnsetFieldBit(modified, MessageA::Data);
    _builder.clear_data();
    return std::move(*this);
  }

  gendb::MessagePatch Build() && {
    gendb::MessagePatch patch;
    patch.modified = std::move(modified);
    patch.removed = std::move(removed);
    patch.layout = MessageA::LayoutPtr();
    patch.buffer = _builder.Build();
    return patch;
  }

 private:
  gendb::Bitmask modified;
  gendb::Bitmask removed;
  MessageABuilder _builder;
};
}  // namespace gendb::tests::primitive
