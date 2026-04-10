
// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "gendb/bits.h"
#include "gendb/message_base.h"
#include "gendb/message_builder.h"
#include "gendb/message_patch.h"
#include "gendb/reflection.h"

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

  static constexpr std::array<gendb::FixedFieldDescriptor, 1> kFixedFieldDescriptors = {
      gendb::FixedFieldDescriptor{MaxTradeVolume, 8, gendb::FixedStorageWidth::Byte8, 0, 0, 0u, 0},
  };

  static constexpr std::array<uint8_t, gendb::kFixedStorageClassCount> kFixedWordCounts = {
      0,
      0,
      1,
  };

  static constexpr gendb::MessageLayout kLayout{kFixedFieldDescriptors, kFixedWordCounts};

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"config_name", ConfigName, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::UNKNOWN_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"max_trade_volume", MaxTradeVolume, gendb::FieldInfo::SCALAR,
                       gendb::FieldInfo::UNKNOWN_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
  };

  Config() : MessageBase(&kLayout) {}
  Config(std::span<const uint8_t> span) : MessageBase(span, &kLayout) {}

  bool has_config_name() const { return HasField(ConfigName); }
  std::string_view config_name() const { return ReadSparseKnownStringField(ConfigName, ""); }
  bool has_max_trade_volume() const { return HasField(MaxTradeVolume); }
  double max_trade_volume() const {
    return ReadFixedScalarFieldByDescriptor<kFixedFieldDescriptors[0], double>(0.0);
  }

  // MessageBase methods.
  using gendb::MessageBase::FieldCount;
  using gendb::MessageBase::GetFieldsMask;
  using gendb::MessageBase::HasField;
  static constexpr const gendb::MessageLayout* LayoutPtr() { return &kLayout; }
  std::span<const uint8_t> FieldRaw(int field_id) const {
    return gendb::MessageBase::FieldRaw(field_id);
  }

  friend class ConfigBuilder;
};

class ConfigBuilder : public gendb::MessageBuilder {
 public:
  ConfigBuilder() : MessageBuilder(Config::LayoutPtr()) {}
  ConfigBuilder(Config& obj) : MessageBuilder(obj) {}

  ConfigBuilder& set_config_name(std::string_view value) {
    AddStringField(Config::ConfigName, value);
    return *this;
  }
  ConfigBuilder& set_max_trade_volume(double value) {
    AddField<double>(Config::MaxTradeVolume, value);
    return *this;
  }

  ConfigBuilder& clear_config_name() {
    ClearField(Config::ConfigName);
    return *this;
  }
  ConfigBuilder& clear_max_trade_volume() {
    ClearField(Config::MaxTradeVolume);
    return *this;
  }

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

  gendb::MessagePatch Build() && {
    gendb::MessagePatch patch;
    patch.modified = std::move(modified);
    patch.removed = std::move(removed);
    patch.layout = Config::LayoutPtr();
    patch.buffer = _builder.Build();
    return patch;
  }

 private:
  gendb::Bitmask modified;
  gendb::Bitmask removed;
  ConfigBuilder _builder;
};
}  // namespace gendb::tests
