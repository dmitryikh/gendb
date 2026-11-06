// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>

#include "gendb/list.h"
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

// Message classes, which can refer to each other in any order, and to themselves
namespace gendb::tests::primitive {
class MessageA;
class MessageABuilder;
}  // namespace gendb::tests::primitive

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
                       std::span<const gendb::EnumValueInfo>(kKeyEnumValues), false},
      gendb::FieldInfo{"data", Data, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  MessageA() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  MessageA(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const MessageA& other) const { return _view == other._view; }

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
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
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

// Builds a partial update of MessageA. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
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

// What needs every message class to be complete.
namespace gendb::tests::primitive {
inline bool MessageA::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests::primitive
