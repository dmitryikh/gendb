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

// Message classes, which can refer to each other in any order, and to themselves
namespace gendb::tests {
class Session;
class SessionBuilder;
}  // namespace gendb::tests

namespace gendb::tests {
// Zero-copy view of a serialized Session. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class Session {
 public:
  enum Field : gendb::FieldId {
    SessionId = 1,
    Token = 2,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"session_id", SessionId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64,
                       "", std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"token", Token, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  Session() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Session(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const Session& other) const { return _view == other._view; }

  bool has_session_id() const { return _view.Has<SessionId, gendb::KindOf<uint64_t>>(); }
  uint64_t session_id() const { return _view.Get<SessionId, uint64_t>(0ULL); }
  bool has_token() const { return _view.Has<Token, gendb::Kind::Var>(); }
  std::string_view token() const { return _view.GetString<Token>(); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Session. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class SessionBuilder : public gendb::MessageBuilder {
 public:
  SessionBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit SessionBuilder(const Session& source) : MessageBuilder(source.View()) {}

  SessionBuilder& set_session_id(uint64_t value) {
    AddField<uint64_t>(Session::SessionId, value);
    return *this;
  }
  SessionBuilder& clear_session_id() {
    ClearField(Session::SessionId);
    return *this;
  }
  SessionBuilder& set_token(std::string_view value) {
    AddStringField(Session::Token, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  SessionBuilder& set_token(S&&) = delete;
  SessionBuilder& clear_token() {
    ClearField(Session::Token);
    return *this;
  }
};

// Builds a partial update of Session. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class SessionPatchBuilder {
 public:
  SessionPatchBuilder() = default;

  SessionPatchBuilder&& set_session_id(uint64_t value) && {
    _patch.Set<uint64_t>(Session::SessionId, value);
    return std::move(*this);
  }
  SessionPatchBuilder&& clear_session_id() && {
    _patch.Remove(Session::SessionId);
    return std::move(*this);
  }
  SessionPatchBuilder&& set_token(std::string_view value) && {
    _patch.CopyString(Session::Token, value);
    return std::move(*this);
  }
  SessionPatchBuilder&& clear_token() && {
    _patch.Remove(Session::Token);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests

// What needs every message class to be complete.
namespace gendb::tests {
inline bool Session::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests
