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

namespace gendb::tests {
// GeneratedClass
class Account : private gendb::MessageBase {
 public:
  enum Field : int {
    AccountId = 1,
    Address = 2,
    Age = 3,
    Balance = 4,
    ConfigName = 5,
    IsActive = 6,
    Name = 7,
    TraderId = 8,
    MaxFields
  };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      AccountId,
      Age,
      Balance,
      IsActive,
  });

  Account() = default;
  Account(std::span<const uint8_t> span) : MessageBase(span) {}

  bool has_account_id() const { return HasField(AccountId); }
  int32_t account_id() const { return ReadScalarField<int32_t>(AccountId, 0); }
  bool has_address() const { return HasField(Address); }
  std::string_view address() const { return ReadStringField(Address, ""); }
  bool has_age() const { return HasField(Age); }
  int32_t age() const { return ReadScalarField<int32_t>(Age, 0); }
  bool has_balance() const { return HasField(Balance); }
  float balance() const { return ReadScalarField<float>(Balance, 0.0f); }
  bool has_config_name() const { return HasField(ConfigName); }
  std::string_view config_name() const { return ReadStringField(ConfigName, ""); }
  bool has_is_active() const { return HasField(IsActive); }
  bool is_active() const { return ReadScalarField<bool>(IsActive, false); }
  bool has_name() const { return HasField(Name); }
  std::string_view name() const { return ReadStringField(Name, ""); }
  bool has_trader_id() const { return HasField(TraderId); }
  std::string_view trader_id() const { return ReadStringField(TraderId, ""); }

  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class AccountBuilder;
};

class AccountBuilder : public gendb::MessageBuilder {
 public:
  AccountBuilder() : MessageBuilder() {}
  AccountBuilder(Account& obj) : MessageBuilder(obj) {}

  void set_account_id(int32_t value) { AddField<int32_t>(Account::AccountId, value); }
  void set_address(std::string_view value) { AddStringField(Account::Address, value); }
  void set_age(int32_t value) { AddField<int32_t>(Account::Age, value); }
  void set_balance(float value) { AddField<float>(Account::Balance, value); }
  void set_config_name(std::string_view value) { AddStringField(Account::ConfigName, value); }
  void set_is_active(bool value) { AddField<bool>(Account::IsActive, value); }
  void set_name(std::string_view value) { AddStringField(Account::Name, value); }
  void set_trader_id(std::string_view value) { AddStringField(Account::TraderId, value); }

  void clear_account_id() { ClearField(Account::AccountId); }
  void clear_address() { ClearField(Account::Address); }
  void clear_age() { ClearField(Account::Age); }
  void clear_balance() { ClearField(Account::Balance); }
  void clear_config_name() { ClearField(Account::ConfigName); }
  void clear_is_active() { ClearField(Account::IsActive); }
  void clear_name() { ClearField(Account::Name); }
  void clear_trader_id() { ClearField(Account::TraderId); }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class AccountPatchBuilder {
 public:
  AccountPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  AccountPatchBuilder&& set_account_id(int32_t value) && {
    _builder.set_account_id(value);
    SetFieldBit(modified, Account::AccountId);
    UnsetFieldBit(removed, Account::AccountId);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_account_id() && {
    SetFieldBit(removed, Account::AccountId);
    UnsetFieldBit(modified, Account::AccountId);
    _builder.clear_account_id();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_address(std::string_view value) && {
    _builder.set_address(value);
    SetFieldBit(modified, Account::Address);
    UnsetFieldBit(removed, Account::Address);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_address() && {
    SetFieldBit(removed, Account::Address);
    UnsetFieldBit(modified, Account::Address);
    _builder.clear_address();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_age(int32_t value) && {
    _builder.set_age(value);
    SetFieldBit(modified, Account::Age);
    UnsetFieldBit(removed, Account::Age);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_age() && {
    SetFieldBit(removed, Account::Age);
    UnsetFieldBit(modified, Account::Age);
    _builder.clear_age();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_balance(float value) && {
    _builder.set_balance(value);
    SetFieldBit(modified, Account::Balance);
    UnsetFieldBit(removed, Account::Balance);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_balance() && {
    SetFieldBit(removed, Account::Balance);
    UnsetFieldBit(modified, Account::Balance);
    _builder.clear_balance();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_config_name(std::string_view value) && {
    _builder.set_config_name(value);
    SetFieldBit(modified, Account::ConfigName);
    UnsetFieldBit(removed, Account::ConfigName);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_config_name() && {
    SetFieldBit(removed, Account::ConfigName);
    UnsetFieldBit(modified, Account::ConfigName);
    _builder.clear_config_name();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_is_active(bool value) && {
    _builder.set_is_active(value);
    SetFieldBit(modified, Account::IsActive);
    UnsetFieldBit(removed, Account::IsActive);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_is_active() && {
    SetFieldBit(removed, Account::IsActive);
    UnsetFieldBit(modified, Account::IsActive);
    _builder.clear_is_active();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_name(std::string_view value) && {
    _builder.set_name(value);
    SetFieldBit(modified, Account::Name);
    UnsetFieldBit(removed, Account::Name);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_name() && {
    SetFieldBit(removed, Account::Name);
    UnsetFieldBit(modified, Account::Name);
    _builder.clear_name();
    return std::move(*this);
  }
  AccountPatchBuilder&& set_trader_id(std::string_view value) && {
    _builder.set_trader_id(value);
    SetFieldBit(modified, Account::TraderId);
    UnsetFieldBit(removed, Account::TraderId);
    return std::move(*this);
  }
  AccountPatchBuilder&& clear_trader_id() && {
    SetFieldBit(removed, Account::TraderId);
    UnsetFieldBit(modified, Account::TraderId);
    _builder.clear_trader_id();
    return std::move(*this);
  }

  gendb::MessagePatch BuildPatch() && {
    gendb::MessagePatch patch;
    patch.modified = std::move(modified);
    patch.removed = std::move(removed);
    patch.buffer = _builder.Build();
    return patch;
  }

 private:
  gendb::Bitmask modified;
  gendb::Bitmask removed;
  AccountBuilder _builder;
};
}  // namespace gendb::tests
