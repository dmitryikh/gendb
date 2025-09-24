
// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include "gendb/bits.h"
#include "gendb/message_base.h"
#include "gendb/message_builder.h"
#include "gendb/message_patch.h"
#include "gendb/reflection.h"

// Enum definitions
namespace gendb {
enum class MetadataType : uint32_t {
  kUnknown = 0,
  kSequence = 1,
};
}  // namespace gendb

// Enum value arrays for reflection
static constexpr gendb::EnumValueInfo kMetadataTypeValues[] = {
    {"kUnknown", 0},
    {"kSequence", 1},
};

// Message classes
namespace gendb {
// GeneratedClass
class MetadataValue : private gendb::MessageBase {
 public:
  enum Field : int { Type = 1, Id = 2, IntValue = 3, StringValue = 4, FloatValue = 5, MaxFields };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      Type,
      Id,
      IntValue,
      FloatValue,
  });

  // Reflection metadata for ParseText
  struct FieldInfo {
    const char* name;
    int field_id;
    enum Type { SCALAR, STRING, ENUM } type;
    enum ScalarType { UINT64, INT32, BOOL, FLOAT, UNKNOWN_SCALAR } scalar_type;
    const char* enum_name;
    const gendb::EnumValueInfo* enum_values;
    size_t enum_values_count;
  };

  static constexpr std::array<FieldInfo, 5> kFieldsInfo = {
      FieldInfo{"type", Type, FieldInfo::ENUM, FieldInfo::UNKNOWN_SCALAR, "gendb::MetadataType",
                kMetadataTypeValues, sizeof(kMetadataTypeValues) / sizeof(gendb::EnumValueInfo)},
      FieldInfo{"id", Id, FieldInfo::SCALAR, FieldInfo::UNKNOWN_SCALAR, nullptr, nullptr, 0},
      FieldInfo{"int_value", IntValue, FieldInfo::SCALAR, FieldInfo::INT32, nullptr, nullptr, 0},
      FieldInfo{"string_value", StringValue, FieldInfo::STRING, FieldInfo::UNKNOWN_SCALAR, nullptr,
                nullptr, 0},
      FieldInfo{"float_value", FloatValue, FieldInfo::SCALAR, FieldInfo::FLOAT, nullptr, nullptr,
                0},
  };

  MetadataValue() = default;
  MetadataValue(std::span<const uint8_t> span) : MessageBase(span) {}

  bool has_type() const { return HasField(Type); }
  gendb::MetadataType type() const {
    return static_cast<gendb::MetadataType>(ReadScalarField<uint32_t>(Type, 0));
  }
  bool has_id() const { return HasField(Id); }
  uint32_t id() const { return ReadScalarField<uint32_t>(Id, 0); }
  bool has_int_value() const { return HasField(IntValue); }
  int32_t int_value() const { return ReadScalarField<int32_t>(IntValue, 0); }
  bool has_string_value() const { return HasField(StringValue); }
  std::string_view string_value() const { return ReadStringField(StringValue, ""); }
  bool has_float_value() const { return HasField(FloatValue); }
  float float_value() const { return ReadScalarField<float>(FloatValue, 0.0f); }

  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class MetadataValueBuilder;
};

class MetadataValueBuilder : public gendb::MessageBuilder {
 public:
  MetadataValueBuilder() : MessageBuilder() {}
  MetadataValueBuilder(MetadataValue& obj) : MessageBuilder(obj) {}

  MetadataValueBuilder& set_type(gendb::MetadataType value) {
    AddField<uint32_t>(MetadataValue::Type, static_cast<uint32_t>(value));
    return *this;
  }
  MetadataValueBuilder& set_id(uint32_t value) {
    AddField<uint32_t>(MetadataValue::Id, value);
    return *this;
  }
  MetadataValueBuilder& set_int_value(int32_t value) {
    AddField<int32_t>(MetadataValue::IntValue, value);
    return *this;
  }
  MetadataValueBuilder& set_string_value(std::string_view value) {
    AddStringField(MetadataValue::StringValue, value);
    return *this;
  }
  MetadataValueBuilder& set_float_value(float value) {
    AddField<float>(MetadataValue::FloatValue, value);
    return *this;
  }

  MetadataValueBuilder& clear_type() {
    ClearField(MetadataValue::Type);
    return *this;
  }
  MetadataValueBuilder& clear_id() {
    ClearField(MetadataValue::Id);
    return *this;
  }
  MetadataValueBuilder& clear_int_value() {
    ClearField(MetadataValue::IntValue);
    return *this;
  }
  MetadataValueBuilder& clear_string_value() {
    ClearField(MetadataValue::StringValue);
    return *this;
  }
  MetadataValueBuilder& clear_float_value() {
    ClearField(MetadataValue::FloatValue);
    return *this;
  }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class MetadataValuePatchBuilder {
 public:
  MetadataValuePatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  MetadataValuePatchBuilder&& set_type(gendb::MetadataType value) && {
    _builder.set_type(value);
    SetFieldBit(modified, MetadataValue::Type);
    UnsetFieldBit(removed, MetadataValue::Type);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_type() && {
    SetFieldBit(removed, MetadataValue::Type);
    UnsetFieldBit(modified, MetadataValue::Type);
    _builder.clear_type();
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_id(uint32_t value) && {
    _builder.set_id(value);
    SetFieldBit(modified, MetadataValue::Id);
    UnsetFieldBit(removed, MetadataValue::Id);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_id() && {
    SetFieldBit(removed, MetadataValue::Id);
    UnsetFieldBit(modified, MetadataValue::Id);
    _builder.clear_id();
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_int_value(int32_t value) && {
    _builder.set_int_value(value);
    SetFieldBit(modified, MetadataValue::IntValue);
    UnsetFieldBit(removed, MetadataValue::IntValue);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_int_value() && {
    SetFieldBit(removed, MetadataValue::IntValue);
    UnsetFieldBit(modified, MetadataValue::IntValue);
    _builder.clear_int_value();
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_string_value(std::string_view value) && {
    _builder.set_string_value(value);
    SetFieldBit(modified, MetadataValue::StringValue);
    UnsetFieldBit(removed, MetadataValue::StringValue);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_string_value() && {
    SetFieldBit(removed, MetadataValue::StringValue);
    UnsetFieldBit(modified, MetadataValue::StringValue);
    _builder.clear_string_value();
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_float_value(float value) && {
    _builder.set_float_value(value);
    SetFieldBit(modified, MetadataValue::FloatValue);
    UnsetFieldBit(removed, MetadataValue::FloatValue);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_float_value() && {
    SetFieldBit(removed, MetadataValue::FloatValue);
    UnsetFieldBit(modified, MetadataValue::FloatValue);
    _builder.clear_float_value();
    return std::move(*this);
  }

  gendb::MessagePatch Build() && {
    gendb::MessagePatch patch;
    patch.modified = std::move(modified);
    patch.removed = std::move(removed);
    patch.buffer = _builder.Build();
    return patch;
  }

 private:
  gendb::Bitmask modified;
  gendb::Bitmask removed;
  MetadataValueBuilder _builder;
};
}  // namespace gendb
