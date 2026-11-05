// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "gendb/message.h"
#include "gendb/reflection.h"

// Enum definitions
namespace gendb {
enum class MetadataType : uint32_t {
  kUnknown = 0,
  kSequence = 1,
};
}  // namespace gendb

// Enum value arrays for reflection
static constexpr std::array<gendb::EnumValueInfo, 2> kMetadataTypeValues = {
    gendb::EnumValueInfo{"kUnknown", 0},
    gendb::EnumValueInfo{"kSequence", 1},
};

// Message classes
namespace gendb {
// Zero-copy view of a serialized MetadataValue. Does not own the bytes.
class MetadataValue {
 public:
  enum Field : gendb::FieldId {
    Type = 1,
    Id = 2,
    IntValue = 3,
    StringValue = 4,
    FloatValue = 5,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 5> kFieldsInfo = {
      gendb::FieldInfo{"type", Type, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT32,
                       "gendb::MetadataType",
                       std::span<const gendb::EnumValueInfo>(kMetadataTypeValues)},
      gendb::FieldInfo{"id", Id, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"int_value", IntValue, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"string_value", StringValue, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"float_value", FloatValue, gendb::FieldInfo::SCALAR, gendb::FieldInfo::FLOAT,
                       "", std::span<const gendb::EnumValueInfo>()},
  };

  MetadataValue() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  MetadataValue(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_type() const { return _view.Has<Type, gendb::KindOf<uint32_t>>(); }
  gendb::MetadataType type() const {
    return static_cast<gendb::MetadataType>(_view.Get<Type, uint32_t>(0));
  }
  bool has_id() const { return _view.Has<Id, gendb::KindOf<uint32_t>>(); }
  uint32_t id() const { return _view.Get<Id, uint32_t>(0); }
  bool has_int_value() const { return _view.Has<IntValue, gendb::KindOf<int32_t>>(); }
  int32_t int_value() const { return _view.Get<IntValue, int32_t>(0); }
  bool has_string_value() const { return _view.Has<StringValue, gendb::Kind::Var>(); }
  std::string_view string_value() const { return _view.GetString<StringValue>(); }
  bool has_float_value() const { return _view.Has<FloatValue, gendb::KindOf<float>>(); }
  float float_value() const { return _view.Get<FloatValue, float>(0.0f); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized MetadataValue. Strings are borrowed: they must stay alive until Build().
class MetadataValueBuilder : public gendb::MessageBuilder {
 public:
  MetadataValueBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit MetadataValueBuilder(const MetadataValue& source) : MessageBuilder(source.View()) {}

  MetadataValueBuilder& set_type(gendb::MetadataType value) {
    AddField<uint32_t>(MetadataValue::Type, static_cast<uint32_t>(value));
    return *this;
  }
  MetadataValueBuilder& clear_type() {
    ClearField(MetadataValue::Type);
    return *this;
  }
  MetadataValueBuilder& set_id(uint32_t value) {
    AddField<uint32_t>(MetadataValue::Id, value);
    return *this;
  }
  MetadataValueBuilder& clear_id() {
    ClearField(MetadataValue::Id);
    return *this;
  }
  MetadataValueBuilder& set_int_value(int32_t value) {
    AddField<int32_t>(MetadataValue::IntValue, value);
    return *this;
  }
  MetadataValueBuilder& clear_int_value() {
    ClearField(MetadataValue::IntValue);
    return *this;
  }
  MetadataValueBuilder& set_string_value(std::string_view value) {
    AddStringField(MetadataValue::StringValue, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  MetadataValueBuilder& set_string_value(S&&) = delete;
  MetadataValueBuilder& clear_string_value() {
    ClearField(MetadataValue::StringValue);
    return *this;
  }
  MetadataValueBuilder& set_float_value(float value) {
    AddField<float>(MetadataValue::FloatValue, value);
    return *this;
  }
  MetadataValueBuilder& clear_float_value() {
    ClearField(MetadataValue::FloatValue);
    return *this;
  }
};

// Builds a partial update of MetadataValue. Owns its string values.
class MetadataValuePatchBuilder {
 public:
  MetadataValuePatchBuilder() = default;

  MetadataValuePatchBuilder&& set_type(gendb::MetadataType value) && {
    _patch.Set<uint32_t>(MetadataValue::Type, static_cast<uint32_t>(value));
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_type() && {
    _patch.Remove(MetadataValue::Type);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_id(uint32_t value) && {
    _patch.Set<uint32_t>(MetadataValue::Id, value);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_id() && {
    _patch.Remove(MetadataValue::Id);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_int_value(int32_t value) && {
    _patch.Set<int32_t>(MetadataValue::IntValue, value);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_int_value() && {
    _patch.Remove(MetadataValue::IntValue);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_string_value(std::string_view value) && {
    _patch.CopyString(MetadataValue::StringValue, value);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_string_value() && {
    _patch.Remove(MetadataValue::StringValue);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& set_float_value(float value) && {
    _patch.Set<float>(MetadataValue::FloatValue, value);
    return std::move(*this);
  }
  MetadataValuePatchBuilder&& clear_float_value() && {
    _patch.Remove(MetadataValue::FloatValue);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb
