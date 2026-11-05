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
// Zero-copy view of a serialized Account. Does not own the bytes.
class Account {
 public:
  enum Field : gendb::FieldId {
    AccountId = 1,
    TraderId = 2,
    Name = 3,
    Address = 4,
    Age = 5,
    IsActive = 6,
    Balance = 7,
    ConfigName = 8,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 8> kFieldsInfo = {
      gendb::FieldInfo{"account_id", AccountId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"trader_id", TraderId, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"name", Name, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"address", Address, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"age", Age, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"is_active", IsActive, gendb::FieldInfo::SCALAR, gendb::FieldInfo::BOOL, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"balance", Balance, gendb::FieldInfo::SCALAR, gendb::FieldInfo::FLOAT, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"config_name", ConfigName, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
  };

  Account() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Account(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_account_id() const { return _view.Has<AccountId, gendb::KindOf<uint64_t>>(); }
  uint64_t account_id() const { return _view.Get<AccountId, uint64_t>(0ULL); }
  bool has_trader_id() const { return _view.Has<TraderId, gendb::Kind::Var>(); }
  std::string_view trader_id() const { return _view.GetString<TraderId>(); }
  bool has_name() const { return _view.Has<Name, gendb::Kind::Var>(); }
  std::string_view name() const { return _view.GetString<Name>(); }
  bool has_address() const { return _view.Has<Address, gendb::Kind::Var>(); }
  std::string_view address() const { return _view.GetString<Address>(); }
  bool has_age() const { return _view.Has<Age, gendb::KindOf<int32_t>>(); }
  int32_t age() const { return _view.Get<Age, int32_t>(0); }
  bool has_is_active() const { return _view.Has<IsActive, gendb::KindOf<bool>>(); }
  bool is_active() const { return _view.Get<IsActive, bool>(false); }
  bool has_balance() const { return _view.Has<Balance, gendb::KindOf<float>>(); }
  float balance() const { return _view.Get<Balance, float>(0.0f); }
  bool has_config_name() const { return _view.Has<ConfigName, gendb::Kind::Var>(); }
  std::string_view config_name() const { return _view.GetString<ConfigName>(); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Account. Strings are borrowed: they must stay alive until Build().
class AccountBuilder : public gendb::MessageBuilder {
 public:
  AccountBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit AccountBuilder(const Account& source) : MessageBuilder(source.View()) {}

  AccountBuilder& set_account_id(uint64_t value) {
    AddField<uint64_t>(Account::AccountId, value);
    return *this;
  }
  AccountBuilder& clear_account_id() {
    ClearField(Account::AccountId);
    return *this;
  }
  AccountBuilder& set_trader_id(std::string_view value) {
    AddStringField(Account::TraderId, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountBuilder& set_trader_id(S&&) = delete;
  AccountBuilder& clear_trader_id() {
    ClearField(Account::TraderId);
    return *this;
  }
  AccountBuilder& set_name(std::string_view value) {
    AddStringField(Account::Name, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountBuilder& set_name(S&&) = delete;
  AccountBuilder& clear_name() {
    ClearField(Account::Name);
    return *this;
  }
  AccountBuilder& set_address(std::string_view value) {
    AddStringField(Account::Address, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountBuilder& set_address(S&&) = delete;
  AccountBuilder& clear_address() {
    ClearField(Account::Address);
    return *this;
  }
  AccountBuilder& set_age(int32_t value) {
    AddField<int32_t>(Account::Age, value);
    return *this;
  }
  AccountBuilder& clear_age() {
    ClearField(Account::Age);
    return *this;
  }
  AccountBuilder& set_is_active(bool value) {
    AddField<bool>(Account::IsActive, value);
    return *this;
  }
  AccountBuilder& clear_is_active() {
    ClearField(Account::IsActive);
    return *this;
  }
  AccountBuilder& set_balance(float value) {
    AddField<float>(Account::Balance, value);
    return *this;
  }
  AccountBuilder& clear_balance() {
    ClearField(Account::Balance);
    return *this;
  }
  AccountBuilder& set_config_name(std::string_view value) {
    AddStringField(Account::ConfigName, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AccountBuilder& set_config_name(S&&) = delete;
  AccountBuilder& clear_config_name() {
    ClearField(Account::ConfigName);
    return *this;
  }
};

// Builds a partial update of Account. Owns its string values.
class AccountPatchBuilder {
 public:
  AccountPatchBuilder() = default;

  AccountPatchBuilder&& set_account_id(uint64_t value) && {
    _patch.Set<uint64_t>(Account::AccountId, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_account_id() && {
    _patch.Remove(Account::AccountId);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_trader_id(std::string_view value) && {
    _patch.CopyString(Account::TraderId, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_trader_id() && {
    _patch.Remove(Account::TraderId);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_name(std::string_view value) && {
    _patch.CopyString(Account::Name, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_name() && {
    _patch.Remove(Account::Name);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_address(std::string_view value) && {
    _patch.CopyString(Account::Address, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_address() && {
    _patch.Remove(Account::Address);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_age(int32_t value) && {
    _patch.Set<int32_t>(Account::Age, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_age() && {
    _patch.Remove(Account::Age);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_is_active(bool value) && {
    _patch.Set<bool>(Account::IsActive, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_is_active() && {
    _patch.Remove(Account::IsActive);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_balance(float value) && {
    _patch.Set<float>(Account::Balance, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_balance() && {
    _patch.Remove(Account::Balance);
    return std::move(*this);
  }
  AccountPatchBuilder&& set_config_name(std::string_view value) && {
    _patch.CopyString(Account::ConfigName, value);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_config_name() && {
    _patch.Remove(Account::ConfigName);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
