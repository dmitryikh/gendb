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

// Message classes
namespace gendb::tests {
// Zero-copy view of a serialized Session. Does not own the bytes.
class Session {
 public:
  enum Field : gendb::FieldId {
    SessionId = 1,
    Token = 2,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"session_id", SessionId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"token", Token, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  Session() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Session(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

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

// Builds a partial update of Session. Owns its string values.
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
