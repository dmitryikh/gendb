
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

// Message classes
namespace gendb::tests {
// GeneratedClass
class Config : private gendb::MessageBase {
 public:
  enum Field : int { ConfigName = 1, MaxTradeVolume = 2, MaxFields };

  static constexpr std::array<uint32_t, 1> kFixedSizeFields = gendb::MakeConstexprFieldBitmask<1>({
      MaxTradeVolume,
  });

  Config() = default;
  Config(std::span<const uint8_t> span) : MessageBase(span) {}

  bool has_config_name() const { return HasField(ConfigName); }
  std::string_view config_name() const { return ReadStringField(ConfigName, ""); }
  bool has_max_trade_volume() const { return HasField(MaxTradeVolume); }
  double max_trade_volume() const { return ReadScalarField<double>(MaxTradeVolume, 0.0); }

  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class ConfigBuilder;
};

class ConfigBuilder : public gendb::MessageBuilder {
 public:
  ConfigBuilder() : MessageBuilder() {}
  ConfigBuilder(Config& obj) : MessageBuilder(obj) {}

  void set_config_name(std::string_view value) { AddStringField(Config::ConfigName, value); }
  void set_max_trade_volume(double value) { AddField<double>(Config::MaxTradeVolume, value); }

  void clear_config_name() { ClearField(Config::ConfigName); }
  void clear_max_trade_volume() { ClearField(Config::MaxTradeVolume); }

  std::vector<uint8_t> Build() { return MessageBuilder::Build(); }
};

class ConfigPatchBuilder {
 public:
  ConfigPatchBuilder() = default;
  // TODO: Add constructor with primary key fields.

  ConfigPatchBuilder&& set_config_name(std::string_view value) && {
    _builder.set_config_name(value);
    SetFieldBit(modified, Config::ConfigName);
    UnsetFieldBit(removed, Config::ConfigName);
    return std::move(*this);
  }
  ConfigPatchBuilder&& clear_config_name() && {
    SetFieldBit(removed, Config::ConfigName);
    UnsetFieldBit(modified, Config::ConfigName);
    _builder.clear_config_name();
    return std::move(*this);
  }
  ConfigPatchBuilder&& set_max_trade_volume(double value) && {
    _builder.set_max_trade_volume(value);
    SetFieldBit(modified, Config::MaxTradeVolume);
    UnsetFieldBit(removed, Config::MaxTradeVolume);
    return std::move(*this);
  }
  ConfigPatchBuilder&& clear_max_trade_volume() && {
    SetFieldBit(removed, Config::MaxTradeVolume);
    UnsetFieldBit(modified, Config::MaxTradeVolume);
    _builder.clear_max_trade_volume();
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
  ConfigBuilder _builder;
};
}  // namespace gendb::tests
