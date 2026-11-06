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
// Zero-copy view of a serialized Config. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class Config {
 public:
  enum Field : gendb::FieldId {
    ConfigName = 1,
    MaxTradeVolume = 2,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"config_name", ConfigName, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"max_trade_volume", MaxTradeVolume, gendb::FieldInfo::SCALAR,
                       gendb::FieldInfo::DOUBLE, "", std::span<const gendb::EnumValueInfo>()},
  };

  Config() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Config(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_config_name() const { return _view.Has<ConfigName, gendb::Kind::Var>(); }
  std::string_view config_name() const { return _view.GetString<ConfigName>(); }
  bool has_max_trade_volume() const { return _view.Has<MaxTradeVolume, gendb::KindOf<double>>(); }
  double max_trade_volume() const { return _view.Get<MaxTradeVolume, double>(0.0); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Config. Strings are borrowed: they must stay alive until Build().
class ConfigBuilder : public gendb::MessageBuilder {
 public:
  ConfigBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit ConfigBuilder(const Config& source) : MessageBuilder(source.View()) {}

  ConfigBuilder& set_config_name(std::string_view value) {
    AddStringField(Config::ConfigName, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  ConfigBuilder& set_config_name(S&&) = delete;
  ConfigBuilder& clear_config_name() {
    ClearField(Config::ConfigName);
    return *this;
  }
  ConfigBuilder& set_max_trade_volume(double value) {
    AddField<double>(Config::MaxTradeVolume, value);
    return *this;
  }
  ConfigBuilder& clear_max_trade_volume() {
    ClearField(Config::MaxTradeVolume);
    return *this;
  }
};

// Builds a partial update of Config. Owns its string values.
class ConfigPatchBuilder {
 public:
  ConfigPatchBuilder() = default;

  ConfigPatchBuilder&& set_config_name(std::string_view value) && {
    _patch.CopyString(Config::ConfigName, value);
    return std::move(*this);
  }
  ConfigPatchBuilder&& clear_config_name() && {
    _patch.Remove(Config::ConfigName);
    return std::move(*this);
  }
  ConfigPatchBuilder&& set_max_trade_volume(double value) && {
    _patch.Set<double>(Config::MaxTradeVolume, value);
    return std::move(*this);
  }
  ConfigPatchBuilder&& clear_max_trade_volume() && {
    _patch.Remove(Config::MaxTradeVolume);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
