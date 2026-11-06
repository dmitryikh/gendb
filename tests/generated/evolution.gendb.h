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
namespace gendb::tests::evolution {
class AccountV1;
class AccountV1Builder;
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
class AccountV2;
class AccountV2Builder;
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
class ContactV1;
class ContactV1Builder;
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
class ContactV2;
class ContactV2Builder;
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
class MemberV1;
class MemberV1Builder;
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
class MemberV2;
class MemberV2Builder;
}  // namespace gendb::tests::evolution

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
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"name", Name, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"nickname", Nickname, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"age", Age, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  AccountV1() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  AccountV1(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const AccountV1& other) const { return _view == other._view; }

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
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
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

// Builds a partial update of AccountV1. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
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
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"name", Name, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"email", Email, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"score", Score, gendb::FieldInfo::SCALAR, gendb::FieldInfo::DOUBLE, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"vip", Vip, gendb::FieldInfo::SCALAR, gendb::FieldInfo::BOOL, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  AccountV2() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  AccountV2(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const AccountV2& other) const { return _view == other._view; }

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
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
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

// Builds a partial update of AccountV2. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
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
namespace gendb::tests::evolution {
// Zero-copy view of a serialized ContactV1. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class ContactV1 {
 public:
  enum Field : gendb::FieldId {
    Email = 1,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 1> kFieldsInfo = {
      gendb::FieldInfo{"email", Email, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  ContactV1() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  ContactV1(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const ContactV1& other) const { return _view == other._view; }

  bool has_email() const { return _view.Has<Email, gendb::Kind::Var>(); }
  std::string_view email() const { return _view.GetString<Email>(); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized ContactV1. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class ContactV1Builder : public gendb::MessageBuilder {
 public:
  ContactV1Builder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit ContactV1Builder(const ContactV1& source) : MessageBuilder(source.View()) {}

  ContactV1Builder& set_email(std::string_view value) {
    AddStringField(ContactV1::Email, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  ContactV1Builder& set_email(S&&) = delete;
  ContactV1Builder& clear_email() {
    ClearField(ContactV1::Email);
    return *this;
  }
};

// Builds a partial update of ContactV1. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class ContactV1PatchBuilder {
 public:
  ContactV1PatchBuilder() = default;

  ContactV1PatchBuilder&& set_email(std::string_view value) && {
    _patch.CopyString(ContactV1::Email, value);
    return std::move(*this);
  }
  ContactV1PatchBuilder&& clear_email() && {
    _patch.Remove(ContactV1::Email);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
// Zero-copy view of a serialized ContactV2. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class ContactV2 {
 public:
  enum Field : gendb::FieldId {
    Email = 1,
    Phone = 2,
    Labels = 3,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 3> kFieldsInfo = {
      gendb::FieldInfo{"email", Email, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"phone", Phone, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"labels", Labels, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), true},
  };

  ContactV2() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  ContactV2(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const ContactV2& other) const { return _view == other._view; }

  bool has_email() const { return _view.Has<Email, gendb::Kind::Var>(); }
  std::string_view email() const { return _view.GetString<Email>(); }
  bool has_phone() const { return _view.Has<Phone, gendb::Kind::Var>(); }
  std::string_view phone() const { return _view.GetString<Phone>(); }
  // Empty if not set, which has_labels() tells apart from being set to an empty list.
  bool has_labels() const { return _view.Has<Labels, gendb::Kind::Var>(); }
  gendb::StringList labels() const {
    return gendb::StringList(_view.GetBytes<Labels>().value_or(std::span<const uint8_t>{}));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized ContactV2. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class ContactV2Builder : public gendb::MessageBuilder {
 public:
  ContactV2Builder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit ContactV2Builder(const ContactV2& source) : MessageBuilder(source.View()) {}

  ContactV2Builder& set_email(std::string_view value) {
    AddStringField(ContactV2::Email, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  ContactV2Builder& set_email(S&&) = delete;
  ContactV2Builder& clear_email() {
    ClearField(ContactV2::Email);
    return *this;
  }
  ContactV2Builder& set_phone(std::string_view value) {
    AddStringField(ContactV2::Phone, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  ContactV2Builder& set_phone(S&&) = delete;
  ContactV2Builder& clear_phone() {
    ClearField(ContactV2::Phone);
    return *this;
  }
  ContactV2Builder& add_labels(std::string_view value) {
    AddRepeatedString(ContactV2::Labels, value);
    return *this;
  }
  ContactV2Builder& set_labels(std::span<const std::string_view> value) {
    SetEmptyList(ContactV2::Labels);
    for (const std::string_view element : value) AddRepeatedString(ContactV2::Labels, element);
    return *this;
  }
  ContactV2Builder& set_labels(std::initializer_list<std::string_view> value) {
    return set_labels(std::span<const std::string_view>(value.begin(), value.size()));
  }
  ContactV2Builder& clear_labels() {
    ClearField(ContactV2::Labels);
    return *this;
  }
};

// Builds a partial update of ContactV2. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class ContactV2PatchBuilder {
 public:
  ContactV2PatchBuilder() = default;

  ContactV2PatchBuilder&& set_email(std::string_view value) && {
    _patch.CopyString(ContactV2::Email, value);
    return std::move(*this);
  }
  ContactV2PatchBuilder&& clear_email() && {
    _patch.Remove(ContactV2::Email);
    return std::move(*this);
  }
  ContactV2PatchBuilder&& set_phone(std::string_view value) && {
    _patch.CopyString(ContactV2::Phone, value);
    return std::move(*this);
  }
  ContactV2PatchBuilder&& clear_phone() && {
    _patch.Remove(ContactV2::Phone);
    return std::move(*this);
  }
  ContactV2PatchBuilder&& set_labels(std::span<const std::string_view> value) && {
    _patch.CopyBytes(ContactV2::Labels, gendb::PackList(value));
    return std::move(*this);
  }
  ContactV2PatchBuilder&& set_labels(std::initializer_list<std::string_view> value) && {
    return std::move(*this).set_labels(
        std::span<const std::string_view>(value.begin(), value.size()));
  }
  ContactV2PatchBuilder&& clear_labels() && {
    _patch.Remove(ContactV2::Labels);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
// Zero-copy view of a serialized MemberV1. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class MemberV1 {
 public:
  enum Field : gendb::FieldId {
    Id = 1,
    Contact = 2,
    Others = 3,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 3> kFieldsInfo = {
      gendb::FieldInfo{"id", Id, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"contact", Contact, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"others", Others, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>(), true},
  };

  MemberV1() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  MemberV1(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const MemberV1& other) const { return _view == other._view; }

  bool has_id() const { return _view.Has<Id, gendb::KindOf<uint64_t>>(); }
  uint64_t id() const { return _view.Get<Id, uint64_t>(0ULL); }
  // Empty if not set, which has_contact() tells apart from being set to an empty message.
  bool has_contact() const { return _view.Has<Contact, gendb::Kind::Var>(); }
  ::gendb::tests::evolution::ContactV1 contact() const;
  // Empty if not set, which has_others() tells apart from being set to an empty list.
  bool has_others() const { return _view.Has<Others, gendb::Kind::Var>(); }
  gendb::MessageList<::gendb::tests::evolution::ContactV1> others() const {
    return gendb::MessageList<::gendb::tests::evolution::ContactV1>(
        _view.GetBytes<Others>().value_or(std::span<const uint8_t>{}));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized MemberV1. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class MemberV1Builder : public gendb::MessageBuilder {
 public:
  MemberV1Builder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit MemberV1Builder(const MemberV1& source) : MessageBuilder(source.View()) {}

  MemberV1Builder& set_id(uint64_t value) {
    AddField<uint64_t>(MemberV1::Id, value);
    return *this;
  }
  MemberV1Builder& clear_id() {
    ClearField(MemberV1::Id);
    return *this;
  }
  MemberV1Builder& set_contact(::gendb::tests::evolution::ContactV1Builder& value);
  MemberV1Builder& set_contact(::gendb::tests::evolution::ContactV1Builder&& value);
  MemberV1Builder& set_contact(const ::gendb::tests::evolution::ContactV1& value);
  MemberV1Builder& clear_contact() {
    ClearField(MemberV1::Contact);
    return *this;
  }
  MemberV1Builder& add_others(::gendb::tests::evolution::ContactV1Builder& value);
  MemberV1Builder& add_others(::gendb::tests::evolution::ContactV1Builder&& value);
  MemberV1Builder& add_others(const ::gendb::tests::evolution::ContactV1& value);
  MemberV1Builder& set_others(const gendb::ListBuilder& value) {
    CopyBytesField(MemberV1::Others, value.Build());
    return *this;
  }
  MemberV1Builder& clear_others() {
    ClearField(MemberV1::Others);
    return *this;
  }
};

// Builds a partial update of MemberV1. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class MemberV1PatchBuilder {
 public:
  MemberV1PatchBuilder() = default;

  MemberV1PatchBuilder&& set_id(uint64_t value) && {
    _patch.Set<uint64_t>(MemberV1::Id, value);
    return std::move(*this);
  }
  MemberV1PatchBuilder&& clear_id() && {
    _patch.Remove(MemberV1::Id);
    return std::move(*this);
  }
  MemberV1PatchBuilder&& set_contact(::gendb::tests::evolution::ContactV1Builder& value) &&;
  MemberV1PatchBuilder&& set_contact(::gendb::tests::evolution::ContactV1Builder&& value) &&;
  MemberV1PatchBuilder&& set_contact(const ::gendb::tests::evolution::ContactV1& value) &&;
  MemberV1PatchBuilder&& clear_contact() && {
    _patch.Remove(MemberV1::Contact);
    return std::move(*this);
  }
  MemberV1PatchBuilder&& set_others(const gendb::ListBuilder& value) && {
    _patch.CopyBytes(MemberV1::Others, value.Build());
    return std::move(*this);
  }
  MemberV1PatchBuilder&& clear_others() && {
    _patch.Remove(MemberV1::Others);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
// Zero-copy view of a serialized MemberV2. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class MemberV2 {
 public:
  enum Field : gendb::FieldId {
    Id = 1,
    Contact = 2,
    Others = 3,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 3> kFieldsInfo = {
      gendb::FieldInfo{"id", Id, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"contact", Contact, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"others", Others, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>(), true},
  };

  MemberV2() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  MemberV2(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const MemberV2& other) const { return _view == other._view; }

  bool has_id() const { return _view.Has<Id, gendb::KindOf<uint64_t>>(); }
  uint64_t id() const { return _view.Get<Id, uint64_t>(0ULL); }
  // Empty if not set, which has_contact() tells apart from being set to an empty message.
  bool has_contact() const { return _view.Has<Contact, gendb::Kind::Var>(); }
  ::gendb::tests::evolution::ContactV2 contact() const;
  // Empty if not set, which has_others() tells apart from being set to an empty list.
  bool has_others() const { return _view.Has<Others, gendb::Kind::Var>(); }
  gendb::MessageList<::gendb::tests::evolution::ContactV2> others() const {
    return gendb::MessageList<::gendb::tests::evolution::ContactV2>(
        _view.GetBytes<Others>().value_or(std::span<const uint8_t>{}));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized MemberV2. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class MemberV2Builder : public gendb::MessageBuilder {
 public:
  MemberV2Builder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit MemberV2Builder(const MemberV2& source) : MessageBuilder(source.View()) {}

  MemberV2Builder& set_id(uint64_t value) {
    AddField<uint64_t>(MemberV2::Id, value);
    return *this;
  }
  MemberV2Builder& clear_id() {
    ClearField(MemberV2::Id);
    return *this;
  }
  MemberV2Builder& set_contact(::gendb::tests::evolution::ContactV2Builder& value);
  MemberV2Builder& set_contact(::gendb::tests::evolution::ContactV2Builder&& value);
  MemberV2Builder& set_contact(const ::gendb::tests::evolution::ContactV2& value);
  MemberV2Builder& clear_contact() {
    ClearField(MemberV2::Contact);
    return *this;
  }
  MemberV2Builder& add_others(::gendb::tests::evolution::ContactV2Builder& value);
  MemberV2Builder& add_others(::gendb::tests::evolution::ContactV2Builder&& value);
  MemberV2Builder& add_others(const ::gendb::tests::evolution::ContactV2& value);
  MemberV2Builder& set_others(const gendb::ListBuilder& value) {
    CopyBytesField(MemberV2::Others, value.Build());
    return *this;
  }
  MemberV2Builder& clear_others() {
    ClearField(MemberV2::Others);
    return *this;
  }
};

// Builds a partial update of MemberV2. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class MemberV2PatchBuilder {
 public:
  MemberV2PatchBuilder() = default;

  MemberV2PatchBuilder&& set_id(uint64_t value) && {
    _patch.Set<uint64_t>(MemberV2::Id, value);
    return std::move(*this);
  }
  MemberV2PatchBuilder&& clear_id() && {
    _patch.Remove(MemberV2::Id);
    return std::move(*this);
  }
  MemberV2PatchBuilder&& set_contact(::gendb::tests::evolution::ContactV2Builder& value) &&;
  MemberV2PatchBuilder&& set_contact(::gendb::tests::evolution::ContactV2Builder&& value) &&;
  MemberV2PatchBuilder&& set_contact(const ::gendb::tests::evolution::ContactV2& value) &&;
  MemberV2PatchBuilder&& clear_contact() && {
    _patch.Remove(MemberV2::Contact);
    return std::move(*this);
  }
  MemberV2PatchBuilder&& set_others(const gendb::ListBuilder& value) && {
    _patch.CopyBytes(MemberV2::Others, value.Build());
    return std::move(*this);
  }
  MemberV2PatchBuilder&& clear_others() && {
    _patch.Remove(MemberV2::Others);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::evolution

// What needs every message class to be complete.
namespace gendb::tests::evolution {
inline bool AccountV1::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
inline bool AccountV2::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
inline bool ContactV1::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
inline bool ContactV2::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  const gendb::MessageView message = gendb::MessageView::OpenOrEmpty(bytes);
  if (const auto f = message.GetBytes<Labels>(); f && !gendb::VarList::Verify(*f)) return false;
  return true;
}
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
inline bool MemberV1::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  const gendb::MessageView message = gendb::MessageView::OpenOrEmpty(bytes);
  if (const auto f = message.GetBytes<Contact>();
      f && !::gendb::tests::evolution::ContactV1::Verify(*f, depth - 1))
    return false;
  if (const auto f = message.GetBytes<Others>();
      f && !gendb::MessageList<::gendb::tests::evolution::ContactV1>::Verify(*f, depth))
    return false;
  return true;
}

inline ::gendb::tests::evolution::ContactV1 MemberV1::contact() const {
  return ::gendb::tests::evolution::ContactV1(
      _view.GetBytes<Contact>().value_or(std::span<const uint8_t>{}));
}
inline MemberV1Builder& MemberV1Builder::set_contact(
    ::gendb::tests::evolution::ContactV1Builder& value) {
  AddMessageField(MemberV1::Contact, value);
  return *this;
}
inline MemberV1Builder& MemberV1Builder::set_contact(
    ::gendb::tests::evolution::ContactV1Builder&& value) {
  AddMessageField(MemberV1::Contact, value);
  return *this;
}
inline MemberV1Builder& MemberV1Builder::set_contact(
    const ::gendb::tests::evolution::ContactV1& value) {
  AddMessageField(MemberV1::Contact, value.View());
  return *this;
}
inline MemberV1PatchBuilder&& MemberV1PatchBuilder::set_contact(
    ::gendb::tests::evolution::ContactV1Builder& value) && {
  _patch.CopyMessage(MemberV1::Contact, value);
  return std::move(*this);
}
inline MemberV1PatchBuilder&& MemberV1PatchBuilder::set_contact(
    ::gendb::tests::evolution::ContactV1Builder&& value) && {
  _patch.CopyMessage(MemberV1::Contact, value);
  return std::move(*this);
}
inline MemberV1PatchBuilder&& MemberV1PatchBuilder::set_contact(
    const ::gendb::tests::evolution::ContactV1& value) && {
  _patch.CopyMessage(MemberV1::Contact, value.View());
  return std::move(*this);
}

inline MemberV1Builder& MemberV1Builder::add_others(
    ::gendb::tests::evolution::ContactV1Builder& value) {
  AddRepeatedMessage(MemberV1::Others, value);
  return *this;
}
inline MemberV1Builder& MemberV1Builder::add_others(
    ::gendb::tests::evolution::ContactV1Builder&& value) {
  AddRepeatedMessage(MemberV1::Others, value);
  return *this;
}
inline MemberV1Builder& MemberV1Builder::add_others(
    const ::gendb::tests::evolution::ContactV1& value) {
  AddRepeatedMessage(MemberV1::Others, value.View());
  return *this;
}
}  // namespace gendb::tests::evolution
namespace gendb::tests::evolution {
inline bool MemberV2::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  const gendb::MessageView message = gendb::MessageView::OpenOrEmpty(bytes);
  if (const auto f = message.GetBytes<Contact>();
      f && !::gendb::tests::evolution::ContactV2::Verify(*f, depth - 1))
    return false;
  if (const auto f = message.GetBytes<Others>();
      f && !gendb::MessageList<::gendb::tests::evolution::ContactV2>::Verify(*f, depth))
    return false;
  return true;
}

inline ::gendb::tests::evolution::ContactV2 MemberV2::contact() const {
  return ::gendb::tests::evolution::ContactV2(
      _view.GetBytes<Contact>().value_or(std::span<const uint8_t>{}));
}
inline MemberV2Builder& MemberV2Builder::set_contact(
    ::gendb::tests::evolution::ContactV2Builder& value) {
  AddMessageField(MemberV2::Contact, value);
  return *this;
}
inline MemberV2Builder& MemberV2Builder::set_contact(
    ::gendb::tests::evolution::ContactV2Builder&& value) {
  AddMessageField(MemberV2::Contact, value);
  return *this;
}
inline MemberV2Builder& MemberV2Builder::set_contact(
    const ::gendb::tests::evolution::ContactV2& value) {
  AddMessageField(MemberV2::Contact, value.View());
  return *this;
}
inline MemberV2PatchBuilder&& MemberV2PatchBuilder::set_contact(
    ::gendb::tests::evolution::ContactV2Builder& value) && {
  _patch.CopyMessage(MemberV2::Contact, value);
  return std::move(*this);
}
inline MemberV2PatchBuilder&& MemberV2PatchBuilder::set_contact(
    ::gendb::tests::evolution::ContactV2Builder&& value) && {
  _patch.CopyMessage(MemberV2::Contact, value);
  return std::move(*this);
}
inline MemberV2PatchBuilder&& MemberV2PatchBuilder::set_contact(
    const ::gendb::tests::evolution::ContactV2& value) && {
  _patch.CopyMessage(MemberV2::Contact, value.View());
  return std::move(*this);
}

inline MemberV2Builder& MemberV2Builder::add_others(
    ::gendb::tests::evolution::ContactV2Builder& value) {
  AddRepeatedMessage(MemberV2::Others, value);
  return *this;
}
inline MemberV2Builder& MemberV2Builder::add_others(
    ::gendb::tests::evolution::ContactV2Builder&& value) {
  AddRepeatedMessage(MemberV2::Others, value);
  return *this;
}
inline MemberV2Builder& MemberV2Builder::add_others(
    const ::gendb::tests::evolution::ContactV2& value) {
  AddRepeatedMessage(MemberV2::Others, value.View());
  return *this;
}
}  // namespace gendb::tests::evolution
