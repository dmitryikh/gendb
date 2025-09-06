// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <cstdint>
#include <array>
#include <string_view>

#include "gendb/message_base.h"
#include "gendb/message_builder.h"
#include "gendb/bits.h"
#include "gendb/message_patch.h"

namespace gendb::tests {
// GeneratedClass
class PositionFb : private gendb::MessageBase {
 public:

  enum Field : int {
    AccountId = 1,
    Instrument = 2,
    OpenPrice = 3,
    PositionId = 4,
    Volume = 5,
    MaxFields
  };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      AccountId,
      OpenPrice,
      PositionId,
      Volume,
  });

  PositionFb() = default;
  PositionFb(std::span<const uint8_t> span) : MessageBase(span) {}

  bool has_account_id() const { return HasField(AccountId); }
  int32_t account_id() const { return ReadScalarField<int32_t>(AccountId, 0); }
  bool has_instrument() const { return HasField(Instrument); }
  std::string_view instrument() const { return ReadStringField(Instrument, ""); }
  bool has_open_price() const { return HasField(OpenPrice); }
  float open_price() const { return ReadScalarField<float>(OpenPrice, 0.0f); }
  bool has_position_id() const { return HasField(PositionId); }
  int32_t position_id() const { return ReadScalarField<int32_t>(PositionId, 0); }
  bool has_volume() const { return HasField(Volume); }
  int32_t volume() const { return ReadScalarField<int32_t>(Volume, 0); }


  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::HasField;
  using gendb::MessageBase::GetFieldsMask;
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class PositionFbBuilder;
};

class PositionFbBuilder : public gendb::MessageBuilder {
 public:
  PositionFbBuilder() : MessageBuilder() {}
  PositionFbBuilder(PositionFb& obj) : MessageBuilder(obj) {}

  void set_account_id(int32_t value) { AddField<int32_t>(PositionFb::AccountId, value); }
  void set_instrument(std::string_view value) { AddStringField(PositionFb::Instrument, value); }
  void set_open_price(float value) { AddField<float>(PositionFb::OpenPrice, value); }
  void set_position_id(int32_t value) { AddField<int32_t>(PositionFb::PositionId, value); }
  void set_volume(int32_t value) { AddField<int32_t>(PositionFb::Volume, value); }

  void clear_account_id() { ClearField(PositionFb::AccountId); }
  void clear_instrument() { ClearField(PositionFb::Instrument); }
  void clear_open_price() { ClearField(PositionFb::OpenPrice); }
  void clear_position_id() { ClearField(PositionFb::PositionId); }
  void clear_volume() { ClearField(PositionFb::Volume); }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class PositionFbPatchBuilder {
 public:
  PositionFbPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  PositionFbPatchBuilder&& set_account_id(int32_t value) && {
    _builder.set_account_id(value);
    SetFieldBit(modified, PositionFb::AccountId);
    UnsetFieldBit(removed, PositionFb::AccountId);
    return std::move(*this);
  }
  PositionFbPatchBuilder&& clear_account_id() && {
    SetFieldBit(removed, PositionFb::AccountId);
    UnsetFieldBit(modified, PositionFb::AccountId);
    _builder.clear_account_id();
    return std::move(*this);
  }
  PositionFbPatchBuilder&& set_instrument(std::string_view value) && {
    _builder.set_instrument(value);
    SetFieldBit(modified, PositionFb::Instrument);
    UnsetFieldBit(removed, PositionFb::Instrument);
    return std::move(*this);
  }
  PositionFbPatchBuilder&& clear_instrument() && {
    SetFieldBit(removed, PositionFb::Instrument);
    UnsetFieldBit(modified, PositionFb::Instrument);
    _builder.clear_instrument();
    return std::move(*this);
  }
  PositionFbPatchBuilder&& set_open_price(float value) && {
    _builder.set_open_price(value);
    SetFieldBit(modified, PositionFb::OpenPrice);
    UnsetFieldBit(removed, PositionFb::OpenPrice);
    return std::move(*this);
  }
  PositionFbPatchBuilder&& clear_open_price() && {
    SetFieldBit(removed, PositionFb::OpenPrice);
    UnsetFieldBit(modified, PositionFb::OpenPrice);
    _builder.clear_open_price();
    return std::move(*this);
  }
  PositionFbPatchBuilder&& set_position_id(int32_t value) && {
    _builder.set_position_id(value);
    SetFieldBit(modified, PositionFb::PositionId);
    UnsetFieldBit(removed, PositionFb::PositionId);
    return std::move(*this);
  }
  PositionFbPatchBuilder&& clear_position_id() && {
    SetFieldBit(removed, PositionFb::PositionId);
    UnsetFieldBit(modified, PositionFb::PositionId);
    _builder.clear_position_id();
    return std::move(*this);
  }
  PositionFbPatchBuilder&& set_volume(int32_t value) && {
    _builder.set_volume(value);
    SetFieldBit(modified, PositionFb::Volume);
    UnsetFieldBit(removed, PositionFb::Volume);
    return std::move(*this);
  }
  PositionFbPatchBuilder&& clear_volume() && {
    SetFieldBit(removed, PositionFb::Volume);
    UnsetFieldBit(modified, PositionFb::Volume);
    _builder.clear_volume();
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
  PositionFbBuilder _builder;
};
} // namespace gendb::tests
