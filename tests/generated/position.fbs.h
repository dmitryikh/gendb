
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

// Enum definitions
namespace gendb::tests {
enum class Direction : uint32_t {
  kUnknown = 0,
  kBuy = 1,
  kSell = 2,
};
}  // namespace gendb::tests

// Message classes
namespace gendb::tests {
// GeneratedClass
class ClosedPosition : private gendb::MessageBase {
 public:
  enum Field : int {
    PositionId = 1,
    AccountId = 2,
    Volume = 3,
    Instrument = 4,
    Profit = 5,
    MaxFields
  };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      PositionId,
      AccountId,
      Volume,
      Profit,
  });

  ClosedPosition() = default;
  ClosedPosition(std::span<const uint8_t> span) : MessageBase(span) {}

  bool has_position_id() const { return HasField(PositionId); }
  int32_t position_id() const { return ReadScalarField<int32_t>(PositionId, 0); }
  bool has_account_id() const { return HasField(AccountId); }
  int32_t account_id() const { return ReadScalarField<int32_t>(AccountId, 0); }
  bool has_volume() const { return HasField(Volume); }
  int32_t volume() const { return ReadScalarField<int32_t>(Volume, 0); }
  bool has_instrument() const { return HasField(Instrument); }
  std::string_view instrument() const { return ReadStringField(Instrument, ""); }
  bool has_profit() const { return HasField(Profit); }
  float profit() const { return ReadScalarField<float>(Profit, 0.0f); }

  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class ClosedPositionBuilder;
};

class ClosedPositionBuilder : public gendb::MessageBuilder {
 public:
  ClosedPositionBuilder() : MessageBuilder() {}
  ClosedPositionBuilder(ClosedPosition& obj) : MessageBuilder(obj) {}

  ClosedPositionBuilder& set_position_id(int32_t value) {
    AddField<int32_t>(ClosedPosition::PositionId, value);
    return *this;
  }
  ClosedPositionBuilder& set_account_id(int32_t value) {
    AddField<int32_t>(ClosedPosition::AccountId, value);
    return *this;
  }
  ClosedPositionBuilder& set_volume(int32_t value) {
    AddField<int32_t>(ClosedPosition::Volume, value);
    return *this;
  }
  ClosedPositionBuilder& set_instrument(std::string_view value) {
    AddStringField(ClosedPosition::Instrument, value);
    return *this;
  }
  ClosedPositionBuilder& set_profit(float value) {
    AddField<float>(ClosedPosition::Profit, value);
    return *this;
  }

  ClosedPositionBuilder& clear_position_id() {
    ClearField(ClosedPosition::PositionId);
    return *this;
  }
  ClosedPositionBuilder& clear_account_id() {
    ClearField(ClosedPosition::AccountId);
    return *this;
  }
  ClosedPositionBuilder& clear_volume() {
    ClearField(ClosedPosition::Volume);
    return *this;
  }
  ClosedPositionBuilder& clear_instrument() {
    ClearField(ClosedPosition::Instrument);
    return *this;
  }
  ClosedPositionBuilder& clear_profit() {
    ClearField(ClosedPosition::Profit);
    return *this;
  }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class ClosedPositionPatchBuilder {
 public:
  ClosedPositionPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  ClosedPositionPatchBuilder&& set_position_id(int32_t value) && {
    _builder.set_position_id(value);
    SetFieldBit(modified, ClosedPosition::PositionId);
    UnsetFieldBit(removed, ClosedPosition::PositionId);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_position_id() && {
    SetFieldBit(removed, ClosedPosition::PositionId);
    UnsetFieldBit(modified, ClosedPosition::PositionId);
    _builder.clear_position_id();
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_account_id(int32_t value) && {
    _builder.set_account_id(value);
    SetFieldBit(modified, ClosedPosition::AccountId);
    UnsetFieldBit(removed, ClosedPosition::AccountId);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_account_id() && {
    SetFieldBit(removed, ClosedPosition::AccountId);
    UnsetFieldBit(modified, ClosedPosition::AccountId);
    _builder.clear_account_id();
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_volume(int32_t value) && {
    _builder.set_volume(value);
    SetFieldBit(modified, ClosedPosition::Volume);
    UnsetFieldBit(removed, ClosedPosition::Volume);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_volume() && {
    SetFieldBit(removed, ClosedPosition::Volume);
    UnsetFieldBit(modified, ClosedPosition::Volume);
    _builder.clear_volume();
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_instrument(std::string_view value) && {
    _builder.set_instrument(value);
    SetFieldBit(modified, ClosedPosition::Instrument);
    UnsetFieldBit(removed, ClosedPosition::Instrument);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_instrument() && {
    SetFieldBit(removed, ClosedPosition::Instrument);
    UnsetFieldBit(modified, ClosedPosition::Instrument);
    _builder.clear_instrument();
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_profit(float value) && {
    _builder.set_profit(value);
    SetFieldBit(modified, ClosedPosition::Profit);
    UnsetFieldBit(removed, ClosedPosition::Profit);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_profit() && {
    SetFieldBit(removed, ClosedPosition::Profit);
    UnsetFieldBit(modified, ClosedPosition::Profit);
    _builder.clear_profit();
    return std::move(*this);
  }

  gendb::MessagePatch Build() && {
    gendb::MessagePatch patch;
    patch.modified = std::move(modified);
    patch.removed = std::move(removed);
    patch.buffer = _builder.Build();
    return patch;
  }

 private:
  gendb::Bitmask modified;
  gendb::Bitmask removed;
  ClosedPositionBuilder _builder;
};
}  // namespace gendb::tests
namespace gendb::tests {
// GeneratedClass
class Position : private gendb::MessageBase {
 public:
  enum Field : int {
    PositionId = 1,
    AccountId = 2,
    Volume = 3,
    Instrument = 4,
    OpenPrice = 5,
    Direction = 6,
    MaxFields
  };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      PositionId,
      AccountId,
      Volume,
      OpenPrice,
      Direction,
  });

  Position() = default;
  Position(std::span<const uint8_t> span) : MessageBase(span) {}

  bool has_position_id() const { return HasField(PositionId); }
  int32_t position_id() const { return ReadScalarField<int32_t>(PositionId, 0); }
  bool has_account_id() const { return HasField(AccountId); }
  int32_t account_id() const { return ReadScalarField<int32_t>(AccountId, 0); }
  bool has_volume() const { return HasField(Volume); }
  int32_t volume() const { return ReadScalarField<int32_t>(Volume, 0); }
  bool has_instrument() const { return HasField(Instrument); }
  std::string_view instrument() const { return ReadStringField(Instrument, ""); }
  bool has_open_price() const { return HasField(OpenPrice); }
  float open_price() const { return ReadScalarField<float>(OpenPrice, 0.0f); }
  bool has_direction() const { return HasField(Direction); }
  gendb::tests::Direction direction() const {
    return static_cast<gendb::tests::Direction>(ReadScalarField<uint32_t>(Direction, 0));
  }

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

  PositionBuilder& set_position_id(int32_t value) {
    AddField<int32_t>(Position::PositionId, value);
    return *this;
  }
  PositionBuilder& set_account_id(int32_t value) {
    AddField<int32_t>(Position::AccountId, value);
    return *this;
  }
  PositionBuilder& set_volume(int32_t value) {
    AddField<int32_t>(Position::Volume, value);
    return *this;
  }
  PositionBuilder& set_instrument(std::string_view value) {
    AddStringField(Position::Instrument, value);
    return *this;
  }
  PositionBuilder& set_open_price(float value) {
    AddField<float>(Position::OpenPrice, value);
    return *this;
  }
  PositionBuilder& set_direction(gendb::tests::Direction value) {
    AddField<uint32_t>(Position::Direction, static_cast<uint32_t>(value));
    return *this;
  }

  PositionBuilder& clear_position_id() {
    ClearField(Position::PositionId);
    return *this;
  }
  PositionBuilder& clear_account_id() {
    ClearField(Position::AccountId);
    return *this;
  }
  PositionBuilder& clear_volume() {
    ClearField(Position::Volume);
    return *this;
  }
  PositionBuilder& clear_instrument() {
    ClearField(Position::Instrument);
    return *this;
  }
  PositionBuilder& clear_open_price() {
    ClearField(Position::OpenPrice);
    return *this;
  }
  PositionBuilder& clear_direction() {
    ClearField(Position::Direction);
    return *this;
  }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class PositionPatchBuilder {
 public:
  PositionPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

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
  PositionPatchBuilder&& set_direction(gendb::tests::Direction value) && {
    _builder.set_direction(value);
    SetFieldBit(modified, Position::Direction);
    UnsetFieldBit(removed, Position::Direction);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_direction() && {
    SetFieldBit(removed, Position::Direction);
    UnsetFieldBit(modified, Position::Direction);
    _builder.clear_direction();
    return std::move(*this);
  }

  gendb::MessagePatch Build() && {
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
