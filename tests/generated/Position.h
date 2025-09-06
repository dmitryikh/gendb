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
class Position : private gendb::MessageBase {
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

  Position() = default;
  Position(std::span<const uint8_t> span) : MessageBase(span) {}

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
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class PositionBuilder;
};

class PositionBuilder : public gendb::MessageBuilder {
 public:
  PositionBuilder() : MessageBuilder() {}
  PositionBuilder(Position& obj) : MessageBuilder(obj) {}

  void set_account_id(int32_t value) { AddField<int32_t>(Position::AccountId, value); }
  void set_instrument(std::string_view value) { AddStringField(Position::Instrument, value); }
  void set_open_price(float value) { AddField<float>(Position::OpenPrice, value); }
  void set_position_id(int32_t value) { AddField<int32_t>(Position::PositionId, value); }
  void set_volume(int32_t value) { AddField<int32_t>(Position::Volume, value); }

  void clear_account_id() { ClearField(Position::AccountId); }
  void clear_instrument() { ClearField(Position::Instrument); }
  void clear_open_price() { ClearField(Position::OpenPrice); }
  void clear_position_id() { ClearField(Position::PositionId); }
  void clear_volume() { ClearField(Position::Volume); }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class PositionPatchBuilder {
 public:
  PositionPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  PositionPatchBuilder&& set_account_id(int32_t value) && {
    _builder.set_account_id(value);
    SetFieldBit(modified, Position::AccountId);
    UnsetFieldBit(removed, Position::AccountId);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_account_id() && {
    SetFieldBit(removed, Position::AccountId);
    UnsetFieldBit(modified, Position::AccountId);
    _builder.clear_account_id();
    return std::move(*this);
  }
  PositionPatchBuilder&& set_instrument(std::string_view value) && {
    _builder.set_instrument(value);
    SetFieldBit(modified, Position::Instrument);
    UnsetFieldBit(removed, Position::Instrument);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_instrument() && {
    SetFieldBit(removed, Position::Instrument);
    UnsetFieldBit(modified, Position::Instrument);
    _builder.clear_instrument();
    return std::move(*this);
  }
  PositionPatchBuilder&& set_open_price(float value) && {
    _builder.set_open_price(value);
    SetFieldBit(modified, Position::OpenPrice);
    UnsetFieldBit(removed, Position::OpenPrice);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_open_price() && {
    SetFieldBit(removed, Position::OpenPrice);
    UnsetFieldBit(modified, Position::OpenPrice);
    _builder.clear_open_price();
    return std::move(*this);
  }
  PositionPatchBuilder&& set_position_id(int32_t value) && {
    _builder.set_position_id(value);
    SetFieldBit(modified, Position::PositionId);
    UnsetFieldBit(removed, Position::PositionId);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_position_id() && {
    SetFieldBit(removed, Position::PositionId);
    UnsetFieldBit(modified, Position::PositionId);
    _builder.clear_position_id();
    return std::move(*this);
  }
  PositionPatchBuilder&& set_volume(int32_t value) && {
    _builder.set_volume(value);
    SetFieldBit(modified, Position::Volume);
    UnsetFieldBit(removed, Position::Volume);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_volume() && {
    SetFieldBit(removed, Position::Volume);
    UnsetFieldBit(modified, Position::Volume);
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
  PositionBuilder _builder;
};
}  // namespace gendb::tests
