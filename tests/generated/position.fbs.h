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
namespace gendb::tests {
enum class Direction : uint32_t {
  kUnknown = 0,
  kBuy = 1,
  kSell = 2,
};
}  // namespace gendb::tests

// Enum value arrays for reflection
static constexpr std::array<gendb::EnumValueInfo, 3> kDirectionValues = {
    gendb::EnumValueInfo{"kUnknown", 0},
    gendb::EnumValueInfo{"kBuy", 1},
    gendb::EnumValueInfo{"kSell", 2},
};

// Message classes
namespace gendb::tests {
// Zero-copy view of a serialized ClosedPosition. Does not own the bytes.
class ClosedPosition {
 public:
  enum Field : gendb::FieldId {
    PositionId = 1,
    AccountId = 2,
    Volume = 3,
    Instrument = 4,
    Profit = 5,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 5> kFieldsInfo = {
      gendb::FieldInfo{"position_id", PositionId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"account_id", AccountId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"volume", Volume, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"instrument", Instrument, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"profit", Profit, gendb::FieldInfo::SCALAR, gendb::FieldInfo::FLOAT, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  ClosedPosition() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  ClosedPosition(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_position_id() const { return _view.Has<PositionId, gendb::KindOf<int32_t>>(); }
  int32_t position_id() const { return _view.Get<PositionId, int32_t>(0); }
  bool has_account_id() const { return _view.Has<AccountId, gendb::KindOf<int32_t>>(); }
  int32_t account_id() const { return _view.Get<AccountId, int32_t>(0); }
  bool has_volume() const { return _view.Has<Volume, gendb::KindOf<int32_t>>(); }
  int32_t volume() const { return _view.Get<Volume, int32_t>(0); }
  bool has_instrument() const { return _view.Has<Instrument, gendb::Kind::Var>(); }
  std::string_view instrument() const { return _view.GetString<Instrument>(); }
  bool has_profit() const { return _view.Has<Profit, gendb::KindOf<float>>(); }
  float profit() const { return _view.Get<Profit, float>(0.0f); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized ClosedPosition. Strings are borrowed: they must stay alive until Build().
class ClosedPositionBuilder : public gendb::MessageBuilder {
 public:
  ClosedPositionBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit ClosedPositionBuilder(const ClosedPosition& source) : MessageBuilder(source.View()) {}

  ClosedPositionBuilder& set_position_id(int32_t value) {
    AddField<int32_t>(ClosedPosition::PositionId, value);
    return *this;
  }
  ClosedPositionBuilder& clear_position_id() {
    ClearField(ClosedPosition::PositionId);
    return *this;
  }
  ClosedPositionBuilder& set_account_id(int32_t value) {
    AddField<int32_t>(ClosedPosition::AccountId, value);
    return *this;
  }
  ClosedPositionBuilder& clear_account_id() {
    ClearField(ClosedPosition::AccountId);
    return *this;
  }
  ClosedPositionBuilder& set_volume(int32_t value) {
    AddField<int32_t>(ClosedPosition::Volume, value);
    return *this;
  }
  ClosedPositionBuilder& clear_volume() {
    ClearField(ClosedPosition::Volume);
    return *this;
  }
  ClosedPositionBuilder& set_instrument(std::string_view value) {
    AddStringField(ClosedPosition::Instrument, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  ClosedPositionBuilder& set_instrument(S&&) = delete;
  ClosedPositionBuilder& clear_instrument() {
    ClearField(ClosedPosition::Instrument);
    return *this;
  }
  ClosedPositionBuilder& set_profit(float value) {
    AddField<float>(ClosedPosition::Profit, value);
    return *this;
  }
  ClosedPositionBuilder& clear_profit() {
    ClearField(ClosedPosition::Profit);
    return *this;
  }
};

// Builds a partial update of ClosedPosition. Owns its string values.
class ClosedPositionPatchBuilder {
 public:
  ClosedPositionPatchBuilder() = default;

  ClosedPositionPatchBuilder&& set_position_id(int32_t value) && {
    _patch.Set<int32_t>(ClosedPosition::PositionId, value);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_position_id() && {
    _patch.Remove(ClosedPosition::PositionId);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_account_id(int32_t value) && {
    _patch.Set<int32_t>(ClosedPosition::AccountId, value);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_account_id() && {
    _patch.Remove(ClosedPosition::AccountId);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_volume(int32_t value) && {
    _patch.Set<int32_t>(ClosedPosition::Volume, value);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_volume() && {
    _patch.Remove(ClosedPosition::Volume);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_instrument(std::string_view value) && {
    _patch.CopyString(ClosedPosition::Instrument, value);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_instrument() && {
    _patch.Remove(ClosedPosition::Instrument);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& set_profit(float value) && {
    _patch.Set<float>(ClosedPosition::Profit, value);
    return std::move(*this);
  }
  ClosedPositionPatchBuilder&& clear_profit() && {
    _patch.Remove(ClosedPosition::Profit);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
namespace gendb::tests {
// Zero-copy view of a serialized Position. Does not own the bytes.
class Position {
 public:
  enum Field : gendb::FieldId {
    PositionId = 1,
    AccountId = 2,
    Volume = 3,
    Instrument = 4,
    OpenPrice = 5,
    Direction = 6,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 6> kFieldsInfo = {
      gendb::FieldInfo{"position_id", PositionId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"account_id", AccountId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"volume", Volume, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"instrument", Instrument, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"open_price", OpenPrice, gendb::FieldInfo::SCALAR, gendb::FieldInfo::FLOAT,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"direction", Direction, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT32,
                       "gendb::tests::Direction",
                       std::span<const gendb::EnumValueInfo>(kDirectionValues)},
  };

  Position() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Position(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_position_id() const { return _view.Has<PositionId, gendb::KindOf<int32_t>>(); }
  int32_t position_id() const { return _view.Get<PositionId, int32_t>(0); }
  bool has_account_id() const { return _view.Has<AccountId, gendb::KindOf<int32_t>>(); }
  int32_t account_id() const { return _view.Get<AccountId, int32_t>(0); }
  bool has_volume() const { return _view.Has<Volume, gendb::KindOf<int32_t>>(); }
  int32_t volume() const { return _view.Get<Volume, int32_t>(0); }
  bool has_instrument() const { return _view.Has<Instrument, gendb::Kind::Var>(); }
  std::string_view instrument() const { return _view.GetString<Instrument>(); }
  bool has_open_price() const { return _view.Has<OpenPrice, gendb::KindOf<float>>(); }
  float open_price() const { return _view.Get<OpenPrice, float>(0.0f); }
  bool has_direction() const { return _view.Has<Direction, gendb::KindOf<uint32_t>>(); }
  gendb::tests::Direction direction() const {
    return static_cast<gendb::tests::Direction>(_view.Get<Direction, uint32_t>(0));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Position. Strings are borrowed: they must stay alive until Build().
class PositionBuilder : public gendb::MessageBuilder {
 public:
  PositionBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit PositionBuilder(const Position& source) : MessageBuilder(source.View()) {}

  PositionBuilder& set_position_id(int32_t value) {
    AddField<int32_t>(Position::PositionId, value);
    return *this;
  }
  PositionBuilder& clear_position_id() {
    ClearField(Position::PositionId);
    return *this;
  }
  PositionBuilder& set_account_id(int32_t value) {
    AddField<int32_t>(Position::AccountId, value);
    return *this;
  }
  PositionBuilder& clear_account_id() {
    ClearField(Position::AccountId);
    return *this;
  }
  PositionBuilder& set_volume(int32_t value) {
    AddField<int32_t>(Position::Volume, value);
    return *this;
  }
  PositionBuilder& clear_volume() {
    ClearField(Position::Volume);
    return *this;
  }
  PositionBuilder& set_instrument(std::string_view value) {
    AddStringField(Position::Instrument, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  PositionBuilder& set_instrument(S&&) = delete;
  PositionBuilder& clear_instrument() {
    ClearField(Position::Instrument);
    return *this;
  }
  PositionBuilder& set_open_price(float value) {
    AddField<float>(Position::OpenPrice, value);
    return *this;
  }
  PositionBuilder& clear_open_price() {
    ClearField(Position::OpenPrice);
    return *this;
  }
  PositionBuilder& set_direction(gendb::tests::Direction value) {
    AddField<uint32_t>(Position::Direction, static_cast<uint32_t>(value));
    return *this;
  }
  PositionBuilder& clear_direction() {
    ClearField(Position::Direction);
    return *this;
  }
};

// Builds a partial update of Position. Owns its string values.
class PositionPatchBuilder {
 public:
  PositionPatchBuilder() = default;

  PositionPatchBuilder&& set_position_id(int32_t value) && {
    _patch.Set<int32_t>(Position::PositionId, value);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_position_id() && {
    _patch.Remove(Position::PositionId);
    return std::move(*this);
  }
  PositionPatchBuilder&& set_account_id(int32_t value) && {
    _patch.Set<int32_t>(Position::AccountId, value);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_account_id() && {
    _patch.Remove(Position::AccountId);
    return std::move(*this);
  }
  PositionPatchBuilder&& set_volume(int32_t value) && {
    _patch.Set<int32_t>(Position::Volume, value);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_volume() && {
    _patch.Remove(Position::Volume);
    return std::move(*this);
  }
  PositionPatchBuilder&& set_instrument(std::string_view value) && {
    _patch.CopyString(Position::Instrument, value);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_instrument() && {
    _patch.Remove(Position::Instrument);
    return std::move(*this);
  }
  PositionPatchBuilder&& set_open_price(float value) && {
    _patch.Set<float>(Position::OpenPrice, value);
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_open_price() && {
    _patch.Remove(Position::OpenPrice);
    return std::move(*this);
  }
  PositionPatchBuilder&& set_direction(gendb::tests::Direction value) && {
    _patch.Set<uint32_t>(Position::Direction, static_cast<uint32_t>(value));
    return std::move(*this);
  }
  PositionPatchBuilder&& clear_direction() && {
    _patch.Remove(Position::Direction);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
