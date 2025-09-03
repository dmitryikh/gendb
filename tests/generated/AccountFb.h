// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <cstdint>
#include <array>
#include <string_view>

#include "message_base.h"
#include "message_builder.h"
#include "bits.h"
#include "message_patch.h"

namespace gendb::tests {
// GeneratedClass
class AccountFb : public gendb::MessageBase {
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

  AccountFb() = default;
  AccountFb(std::span<uint8_t> span) : MessageBase(span) {}

  bool has_account_id() const { return HasField(AccountId); }
  int32_t account_id() const { return ReadScalarField<int32_t>(AccountId, 0); }
  bool set_account_id(int32_t value) { return SetScalarField<int32_t>(AccountId, value); }
  bool has_address() const { return HasField(Address); }
  std::string_view address() const { return ReadStringField(Address, ""); }
  bool has_age() const { return HasField(Age); }
  int32_t age() const { return ReadScalarField<int32_t>(Age, 0); }
  bool set_age(int32_t value) { return SetScalarField<int32_t>(Age, value); }
  bool has_balance() const { return HasField(Balance); }
  float balance() const { return ReadScalarField<float>(Balance, 0.0f); }
  bool set_balance(float value) { return SetScalarField<float>(Balance, value); }
  bool has_config_name() const { return HasField(ConfigName); }
  std::string_view config_name() const { return ReadStringField(ConfigName, ""); }
  bool has_is_active() const { return HasField(IsActive); }
  bool is_active() const { return ReadScalarField<bool>(IsActive, false); }
  bool set_is_active(bool value) { return SetScalarField<bool>(IsActive, value); }
  bool has_name() const { return HasField(Name); }
  std::string_view name() const { return ReadStringField(Name, ""); }
  bool has_trader_id() const { return HasField(TraderId); }
  std::string_view trader_id() const { return ReadStringField(TraderId, ""); }

  bool CanApplyPatchInplace(const gendb::MessagePatch& patch) const {
    return gendb::MessageBase::CanApplyPatchInplace(patch, kFixedSizeFields);
  }
};

class AccountFbBuilder : public gendb::MessageBuilder {
 public:
  AccountFbBuilder() : MessageBuilder() {}
  AccountFbBuilder(AccountFb& obj) : MessageBuilder(obj) {}

  void set_account_id(int32_t value) { AddField<int32_t>(AccountFb::AccountId, value); }
  void set_address(std::string_view value) { AddStringField(AccountFb::Address, value); }
  void set_age(int32_t value) { AddField<int32_t>(AccountFb::Age, value); }
  void set_balance(float value) { AddField<float>(AccountFb::Balance, value); }
  void set_config_name(std::string_view value) { AddStringField(AccountFb::ConfigName, value); }
  void set_is_active(bool value) { AddField<bool>(AccountFb::IsActive, value); }
  void set_name(std::string_view value) { AddStringField(AccountFb::Name, value); }
  void set_trader_id(std::string_view value) { AddStringField(AccountFb::TraderId, value); }

  void clear_account_id() { ClearField(AccountFb::AccountId); }
  void clear_address() { ClearField(AccountFb::Address); }
  void clear_age() { ClearField(AccountFb::Age); }
  void clear_balance() { ClearField(AccountFb::Balance); }
  void clear_config_name() { ClearField(AccountFb::ConfigName); }
  void clear_is_active() { ClearField(AccountFb::IsActive); }
  void clear_name() { ClearField(AccountFb::Name); }
  void clear_trader_id() { ClearField(AccountFb::TraderId); }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class AccountFbPatchBuilder {
 public:
  AccountFbPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  AccountFbPatchBuilder&& set_account_id(int32_t value) && {
    _builder.set_account_id(value);
    SetFieldBit(modified, AccountFb::AccountId);
    UnsetFieldBit(removed, AccountFb::AccountId);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_account_id() && {
    SetFieldBit(removed, AccountFb::AccountId);
    UnsetFieldBit(modified, AccountFb::AccountId);
    _builder.clear_account_id();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_address(std::string_view value) && {
    _builder.set_address(value);
    SetFieldBit(modified, AccountFb::Address);
    UnsetFieldBit(removed, AccountFb::Address);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_address() && {
    SetFieldBit(removed, AccountFb::Address);
    UnsetFieldBit(modified, AccountFb::Address);
    _builder.clear_address();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_age(int32_t value) && {
    _builder.set_age(value);
    SetFieldBit(modified, AccountFb::Age);
    UnsetFieldBit(removed, AccountFb::Age);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_age() && {
    SetFieldBit(removed, AccountFb::Age);
    UnsetFieldBit(modified, AccountFb::Age);
    _builder.clear_age();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_balance(float value) && {
    _builder.set_balance(value);
    SetFieldBit(modified, AccountFb::Balance);
    UnsetFieldBit(removed, AccountFb::Balance);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_balance() && {
    SetFieldBit(removed, AccountFb::Balance);
    UnsetFieldBit(modified, AccountFb::Balance);
    _builder.clear_balance();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_config_name(std::string_view value) && {
    _builder.set_config_name(value);
    SetFieldBit(modified, AccountFb::ConfigName);
    UnsetFieldBit(removed, AccountFb::ConfigName);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_config_name() && {
    SetFieldBit(removed, AccountFb::ConfigName);
    UnsetFieldBit(modified, AccountFb::ConfigName);
    _builder.clear_config_name();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_is_active(bool value) && {
    _builder.set_is_active(value);
    SetFieldBit(modified, AccountFb::IsActive);
    UnsetFieldBit(removed, AccountFb::IsActive);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_is_active() && {
    SetFieldBit(removed, AccountFb::IsActive);
    UnsetFieldBit(modified, AccountFb::IsActive);
    _builder.clear_is_active();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_name(std::string_view value) && {
    _builder.set_name(value);
    SetFieldBit(modified, AccountFb::Name);
    UnsetFieldBit(removed, AccountFb::Name);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_name() && {
    SetFieldBit(removed, AccountFb::Name);
    UnsetFieldBit(modified, AccountFb::Name);
    _builder.clear_name();
    return std::move(*this);
  }
  AccountFbPatchBuilder&& set_trader_id(std::string_view value) && {
    _builder.set_trader_id(value);
    SetFieldBit(modified, AccountFb::TraderId);
    UnsetFieldBit(removed, AccountFb::TraderId);
    return std::move(*this);
  }
  AccountFbPatchBuilder&& clear_trader_id() && {
    SetFieldBit(removed, AccountFb::TraderId);
    UnsetFieldBit(modified, AccountFb::TraderId);
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
  AccountFbBuilder _builder;
};
} // namespace gendb::tests
