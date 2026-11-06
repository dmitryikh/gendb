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
namespace gendb::tests::primitive {
enum class KeyEnum : uint32_t {
  kUnknown = 0,
  kFirstValue = 1,
  kSecondValue = 2,
};

// Enum value arrays for reflection
inline constexpr std::array<gendb::EnumValueInfo, 3> kKeyEnumValues = {
    gendb::EnumValueInfo{"kUnknown", 0},
    gendb::EnumValueInfo{"kFirstValue", 1},
    gendb::EnumValueInfo{"kSecondValue", 2},
};
}  // namespace gendb::tests::primitive

// Message classes
namespace gendb::tests::primitive {
// Zero-copy view of a serialized MessageA. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class MessageA {
 public:
  enum Field : gendb::FieldId {
    Key = 1,
    Data = 2,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"key", Key, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT32,
                       "gendb::tests::primitive::KeyEnum",
                       std::span<const gendb::EnumValueInfo>(kKeyEnumValues)},
      gendb::FieldInfo{"data", Data, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  MessageA() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  MessageA(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_key() const { return _view.Has<Key, gendb::KindOf<uint32_t>>(); }
  gendb::tests::primitive::KeyEnum key() const {
    return static_cast<gendb::tests::primitive::KeyEnum>(_view.Get<Key, uint32_t>(0));
  }
  bool has_data() const { return _view.Has<Data, gendb::Kind::Var>(); }
  std::string_view data() const { return _view.GetString<Data>(); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized MessageA. Strings are borrowed: they must stay alive until Build().
class MessageABuilder : public gendb::MessageBuilder {
 public:
  MessageABuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit MessageABuilder(const MessageA& source) : MessageBuilder(source.View()) {}

  MessageABuilder& set_key(gendb::tests::primitive::KeyEnum value) {
    AddField<uint32_t>(MessageA::Key, static_cast<uint32_t>(value));
    return *this;
  }
  MessageABuilder& clear_key() {
    ClearField(MessageA::Key);
    return *this;
  }
  MessageABuilder& set_data(std::string_view value) {
    AddStringField(MessageA::Data, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  MessageABuilder& set_data(S&&) = delete;
  MessageABuilder& clear_data() {
    ClearField(MessageA::Data);
    return *this;
  }
};

// Builds a partial update of MessageA. Owns its string values.
class MessageAPatchBuilder {
 public:
  MessageAPatchBuilder() = default;

  MessageAPatchBuilder&& set_key(gendb::tests::primitive::KeyEnum value) && {
    _patch.Set<uint32_t>(MessageA::Key, static_cast<uint32_t>(value));
    return std::move(*this);
  }
  MessageAPatchBuilder&& clear_key() && {
    _patch.Remove(MessageA::Key);
    return std::move(*this);
  }
  MessageAPatchBuilder&& set_data(std::string_view value) && {
    _patch.CopyString(MessageA::Data, value);
    return std::move(*this);
  }
  MessageAPatchBuilder&& clear_data() && {
    _patch.Remove(MessageA::Data);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::primitive
