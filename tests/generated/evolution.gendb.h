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
namespace gendb::tests::evolution {
// Zero-copy view of a serialized AccountV1. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class AccountV1 {
 public:
  enum Field : gendb::FieldId {
    Id = 1,
    Name = 2,
    Nickname = 3,
    Age = 4,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 4> kFieldsInfo = {
      gendb::FieldInfo{"id", Id, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"name", Name, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"nickname", Nickname, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"age", Age, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  AccountV1() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  AccountV1(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_id() const { return _view.Has<Id, gendb::KindOf<uint64_t>>(); }
  uint64_t id() const { return _view.Get<Id, uint64_t>(0ULL); }
  bool has_name() const { return _view.Has<Name, gendb::Kind::Var>(); }
  std::string_view name() const { return _view.GetString<Name>(); }
  bool has_nickname() const { return _view.Has<Nickname, gendb::Kind::Var>(); }
  std::string_view nickname() const { return _view.GetString<Nickname>(); }
  bool has_age() const { return _view.Has<Age, gendb::KindOf<int32_t>>(); }
  int32_t age() const { return _view.Get<Age, int32_t>(0); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized AccountV1. Strings are borrowed: they must stay alive until Build().
class AccountV1Builder : public gendb::MessageBuilder {
 public:
  AccountV1Builder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit AccountV1Builder(const AccountV1& source) : MessageBuilder(source.View()) {}

  AccountV1Builder& set_id(uint64_t value) {
    AddField<uint64_t>(AccountV1::Id, value);
    return *this;
  }
  AccountV1Builder& clear_id() {
    ClearField(AccountV1::Id);
    return *this;
  }
  AccountV1Builder& set_name(std::string_view value) {
    AddStringField(AccountV1::Name, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountV1Builder& set_name(S&&) = delete;
  AccountV1Builder& clear_name() {
    ClearField(AccountV1::Name);
    return *this;
  }
  AccountV1Builder& set_nickname(std::string_view value) {
    AddStringField(AccountV1::Nickname, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountV1Builder& set_nickname(S&&) = delete;
  AccountV1Builder& clear_nickname() {
    ClearField(AccountV1::Nickname);
    return *this;
  }
  AccountV1Builder& set_age(int32_t value) {
    AddField<int32_t>(AccountV1::Age, value);
    return *this;
  }
  AccountV1Builder& clear_age() {
    ClearField(AccountV1::Age);
    return *this;
  }
};

// Builds a partial update of AccountV1. Owns its string values.
class AccountV1PatchBuilder {
 public:
  AccountV1PatchBuilder() = default;

  AccountV1PatchBuilder&& set_id(uint64_t value) && {
    _patch.Set<uint64_t>(AccountV1::Id, value);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& clear_id() && {
    _patch.Remove(AccountV1::Id);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& set_name(std::string_view value) && {
    _patch.CopyString(AccountV1::Name, value);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& clear_name() && {
    _patch.Remove(AccountV1::Name);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& set_nickname(std::string_view value) && {
    _patch.CopyString(AccountV1::Nickname, value);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& clear_nickname() && {
    _patch.Remove(AccountV1::Nickname);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& set_age(int32_t value) && {
    _patch.Set<int32_t>(AccountV1::Age, value);
    return std::move(*this);
  }
  AccountV1PatchBuilder&& clear_age() && {
    _patch.Remove(AccountV1::Age);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
// Zero-copy view of a serialized AccountV2. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class AccountV2 {
 public:
  enum Field : gendb::FieldId {
    Id = 1,
    Name = 2,
    Email = 5,
    Score = 6,
    Vip = 7,
    // 3 is reserved: `nickname` is deprecated. Never reuse this ID.
    // 4 is reserved: `age` is deprecated. Never reuse this ID.
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 5> kFieldsInfo = {
      gendb::FieldInfo{"id", Id, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"name", Name, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"email", Email, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"score", Score, gendb::FieldInfo::SCALAR, gendb::FieldInfo::DOUBLE, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"vip", Vip, gendb::FieldInfo::SCALAR, gendb::FieldInfo::BOOL, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  AccountV2() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  AccountV2(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_id() const { return _view.Has<Id, gendb::KindOf<uint64_t>>(); }
  uint64_t id() const { return _view.Get<Id, uint64_t>(0ULL); }
  bool has_name() const { return _view.Has<Name, gendb::Kind::Var>(); }
  std::string_view name() const { return _view.GetString<Name>(); }
  bool has_email() const { return _view.Has<Email, gendb::Kind::Var>(); }
  std::string_view email() const { return _view.GetString<Email>(); }
  bool has_score() const { return _view.Has<Score, gendb::KindOf<double>>(); }
  double score() const { return _view.Get<Score, double>(0.0); }
  bool has_vip() const { return _view.Has<Vip, gendb::KindOf<bool>>(); }
  bool vip() const { return _view.Get<Vip, bool>(false); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized AccountV2. Strings are borrowed: they must stay alive until Build().
class AccountV2Builder : public gendb::MessageBuilder {
 public:
  AccountV2Builder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit AccountV2Builder(const AccountV2& source) : MessageBuilder(source.View()) {}

  AccountV2Builder& set_id(uint64_t value) {
    AddField<uint64_t>(AccountV2::Id, value);
    return *this;
  }
  AccountV2Builder& clear_id() {
    ClearField(AccountV2::Id);
    return *this;
  }
  AccountV2Builder& set_name(std::string_view value) {
    AddStringField(AccountV2::Name, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountV2Builder& set_name(S&&) = delete;
  AccountV2Builder& clear_name() {
    ClearField(AccountV2::Name);
    return *this;
  }
  AccountV2Builder& set_email(std::string_view value) {
    AddStringField(AccountV2::Email, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountV2Builder& set_email(S&&) = delete;
  AccountV2Builder& clear_email() {
    ClearField(AccountV2::Email);
    return *this;
  }
  AccountV2Builder& set_score(double value) {
    AddField<double>(AccountV2::Score, value);
    return *this;
  }
  AccountV2Builder& clear_score() {
    ClearField(AccountV2::Score);
    return *this;
  }
  AccountV2Builder& set_vip(bool value) {
    AddField<bool>(AccountV2::Vip, value);
    return *this;
  }
  AccountV2Builder& clear_vip() {
    ClearField(AccountV2::Vip);
    return *this;
  }
};

// Builds a partial update of AccountV2. Owns its string values.
class AccountV2PatchBuilder {
 public:
  AccountV2PatchBuilder() = default;

  AccountV2PatchBuilder&& set_id(uint64_t value) && {
    _patch.Set<uint64_t>(AccountV2::Id, value);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& clear_id() && {
    _patch.Remove(AccountV2::Id);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& set_name(std::string_view value) && {
    _patch.CopyString(AccountV2::Name, value);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& clear_name() && {
    _patch.Remove(AccountV2::Name);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& set_email(std::string_view value) && {
    _patch.CopyString(AccountV2::Email, value);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& clear_email() && {
    _patch.Remove(AccountV2::Email);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& set_score(double value) && {
    _patch.Set<double>(AccountV2::Score, value);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& clear_score() && {
    _patch.Remove(AccountV2::Score);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& set_vip(bool value) && {
    _patch.Set<bool>(AccountV2::Vip, value);
    return std::move(*this);
  }
  AccountV2PatchBuilder&& clear_vip() && {
    _patch.Remove(AccountV2::Vip);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::evolution
