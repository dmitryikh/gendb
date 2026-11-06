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
namespace gendb::tests::defaults {
enum class Level : uint16_t {
  kNone = 0,
  kLow = 1,
  kHigh = 2,
};

// Enum value arrays for reflection
inline constexpr std::array<gendb::EnumValueInfo, 3> kLevelValues = {
    gendb::EnumValueInfo{"kNone", 0},
    gendb::EnumValueInfo{"kLow", 1},
    gendb::EnumValueInfo{"kHigh", 2},
};
}  // namespace gendb::tests::defaults

// Message classes
namespace gendb::tests::defaults {
// Zero-copy view of a serialized Settings. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class Settings {
 public:
  enum Field : gendb::FieldId {
    Id = 1,
    Enabled = 2,
    Retries = 3,
    Tiny = 4,
    Huge = 5,
    Lowest = 6,
    Ratio = 7,
    Weight = 8,
    Mask = 9,
    Level = 10,
    Title = 11,
    Tricky = 12,
    Unicode = 13,
    PlainCount = 14,
    PlainName = 15,
    PlainLevel = 16,
    // 17 is reserved: `old_setting` is deprecated. Never reuse this ID.
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 16> kFieldsInfo = {
      gendb::FieldInfo{"id", Id, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"enabled", Enabled, gendb::FieldInfo::SCALAR, gendb::FieldInfo::BOOL, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"retries", Retries, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"tiny", Tiny, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT8, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"huge", Huge, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"lowest", Lowest, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT64, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"ratio", Ratio, gendb::FieldInfo::SCALAR, gendb::FieldInfo::FLOAT, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"weight", Weight, gendb::FieldInfo::SCALAR, gendb::FieldInfo::DOUBLE, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"mask", Mask, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT32, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"level", Level, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT16,
                       "gendb::tests::defaults::Level",
                       std::span<const gendb::EnumValueInfo>(kLevelValues)},
      gendb::FieldInfo{"title", Title, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"tricky", Tricky, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"unicode", Unicode, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"plain_count", PlainCount, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32,
                       "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"plain_name", PlainName, gendb::FieldInfo::STRING,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>()},
      gendb::FieldInfo{"plain_level", PlainLevel, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT16,
                       "gendb::tests::defaults::Level",
                       std::span<const gendb::EnumValueInfo>(kLevelValues)},
  };

  Settings() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Settings(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  bool has_id() const { return _view.Has<Id, gendb::KindOf<uint64_t>>(); }
  uint64_t id() const { return _view.Get<Id, uint64_t>(0ULL); }
  bool has_enabled() const { return _view.Has<Enabled, gendb::KindOf<bool>>(); }
  bool enabled() const { return _view.Get<Enabled, bool>(true); }
  bool has_retries() const { return _view.Has<Retries, gendb::KindOf<int32_t>>(); }
  int32_t retries() const { return _view.Get<Retries, int32_t>(static_cast<int32_t>(-3LL)); }
  bool has_tiny() const { return _view.Has<Tiny, gendb::KindOf<int8_t>>(); }
  int8_t tiny() const { return _view.Get<Tiny, int8_t>(static_cast<int8_t>(-128LL)); }
  bool has_huge() const { return _view.Has<Huge, gendb::KindOf<uint64_t>>(); }
  uint64_t huge() const {
    return _view.Get<Huge, uint64_t>(static_cast<uint64_t>(18446744073709551615ULL));
  }
  bool has_lowest() const { return _view.Has<Lowest, gendb::KindOf<int64_t>>(); }
  int64_t lowest() const { return _view.Get<Lowest, int64_t>((-9223372036854775807LL - 1)); }
  bool has_ratio() const { return _view.Has<Ratio, gendb::KindOf<float>>(); }
  float ratio() const { return _view.Get<Ratio, float>(0.25f); }
  bool has_weight() const { return _view.Has<Weight, gendb::KindOf<double>>(); }
  double weight() const { return _view.Get<Weight, double>(-1.5e+300); }
  bool has_mask() const { return _view.Has<Mask, gendb::KindOf<uint32_t>>(); }
  uint32_t mask() const { return _view.Get<Mask, uint32_t>(static_cast<uint32_t>(65280ULL)); }
  bool has_level() const { return _view.Has<Level, gendb::KindOf<uint16_t>>(); }
  gendb::tests::defaults::Level level() const {
    return static_cast<gendb::tests::defaults::Level>(
        _view.Get<Level, uint16_t>(static_cast<uint16_t>(gendb::tests::defaults::Level::kHigh)));
  }
  bool has_title() const { return _view.Has<Title, gendb::Kind::Var>(); }
  std::string_view title() const { return _view.GetString<Title>(std::string_view("untitled", 8)); }
  bool has_tricky() const { return _view.Has<Tricky, gendb::Kind::Var>(); }
  std::string_view tricky() const {
    return _view.GetString<Tricky>(
        std::string_view("quote\" back\\slash \?\?= \012 tab\011 \377\000 end", 35));
  }
  bool has_unicode() const { return _view.Has<Unicode, gendb::Kind::Var>(); }
  std::string_view unicode() const {
    return _view.GetString<Unicode>(std::string_view("\320\270\320\274\321\217", 6));
  }
  bool has_plain_count() const { return _view.Has<PlainCount, gendb::KindOf<int32_t>>(); }
  int32_t plain_count() const { return _view.Get<PlainCount, int32_t>(0); }
  bool has_plain_name() const { return _view.Has<PlainName, gendb::Kind::Var>(); }
  std::string_view plain_name() const { return _view.GetString<PlainName>(); }
  bool has_plain_level() const { return _view.Has<PlainLevel, gendb::KindOf<uint16_t>>(); }
  gendb::tests::defaults::Level plain_level() const {
    return static_cast<gendb::tests::defaults::Level>(_view.Get<PlainLevel, uint16_t>(0));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Settings. Strings are borrowed: they must stay alive until Build().
class SettingsBuilder : public gendb::MessageBuilder {
 public:
  SettingsBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit SettingsBuilder(const Settings& source) : MessageBuilder(source.View()) {}

  SettingsBuilder& set_id(uint64_t value) {
    AddField<uint64_t>(Settings::Id, value);
    return *this;
  }
  SettingsBuilder& clear_id() {
    ClearField(Settings::Id);
    return *this;
  }
  SettingsBuilder& set_enabled(bool value) {
    AddField<bool>(Settings::Enabled, value);
    return *this;
  }
  SettingsBuilder& clear_enabled() {
    ClearField(Settings::Enabled);
    return *this;
  }
  SettingsBuilder& set_retries(int32_t value) {
    AddField<int32_t>(Settings::Retries, value);
    return *this;
  }
  SettingsBuilder& clear_retries() {
    ClearField(Settings::Retries);
    return *this;
  }
  SettingsBuilder& set_tiny(int8_t value) {
    AddField<int8_t>(Settings::Tiny, value);
    return *this;
  }
  SettingsBuilder& clear_tiny() {
    ClearField(Settings::Tiny);
    return *this;
  }
  SettingsBuilder& set_huge(uint64_t value) {
    AddField<uint64_t>(Settings::Huge, value);
    return *this;
  }
  SettingsBuilder& clear_huge() {
    ClearField(Settings::Huge);
    return *this;
  }
  SettingsBuilder& set_lowest(int64_t value) {
    AddField<int64_t>(Settings::Lowest, value);
    return *this;
  }
  SettingsBuilder& clear_lowest() {
    ClearField(Settings::Lowest);
    return *this;
  }
  SettingsBuilder& set_ratio(float value) {
    AddField<float>(Settings::Ratio, value);
    return *this;
  }
  SettingsBuilder& clear_ratio() {
    ClearField(Settings::Ratio);
    return *this;
  }
  SettingsBuilder& set_weight(double value) {
    AddField<double>(Settings::Weight, value);
    return *this;
  }
  SettingsBuilder& clear_weight() {
    ClearField(Settings::Weight);
    return *this;
  }
  SettingsBuilder& set_mask(uint32_t value) {
    AddField<uint32_t>(Settings::Mask, value);
    return *this;
  }
  SettingsBuilder& clear_mask() {
    ClearField(Settings::Mask);
    return *this;
  }
  SettingsBuilder& set_level(gendb::tests::defaults::Level value) {
    AddField<uint16_t>(Settings::Level, static_cast<uint16_t>(value));
    return *this;
  }
  SettingsBuilder& clear_level() {
    ClearField(Settings::Level);
    return *this;
  }
  SettingsBuilder& set_title(std::string_view value) {
    AddStringField(Settings::Title, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  SettingsBuilder& set_title(S&&) = delete;
  SettingsBuilder& clear_title() {
    ClearField(Settings::Title);
    return *this;
  }
  SettingsBuilder& set_tricky(std::string_view value) {
    AddStringField(Settings::Tricky, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  SettingsBuilder& set_tricky(S&&) = delete;
  SettingsBuilder& clear_tricky() {
    ClearField(Settings::Tricky);
    return *this;
  }
  SettingsBuilder& set_unicode(std::string_view value) {
    AddStringField(Settings::Unicode, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  SettingsBuilder& set_unicode(S&&) = delete;
  SettingsBuilder& clear_unicode() {
    ClearField(Settings::Unicode);
    return *this;
  }
  SettingsBuilder& set_plain_count(int32_t value) {
    AddField<int32_t>(Settings::PlainCount, value);
    return *this;
  }
  SettingsBuilder& clear_plain_count() {
    ClearField(Settings::PlainCount);
    return *this;
  }
  SettingsBuilder& set_plain_name(std::string_view value) {
    AddStringField(Settings::PlainName, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  SettingsBuilder& set_plain_name(S&&) = delete;
  SettingsBuilder& clear_plain_name() {
    ClearField(Settings::PlainName);
    return *this;
  }
  SettingsBuilder& set_plain_level(gendb::tests::defaults::Level value) {
    AddField<uint16_t>(Settings::PlainLevel, static_cast<uint16_t>(value));
    return *this;
  }
  SettingsBuilder& clear_plain_level() {
    ClearField(Settings::PlainLevel);
    return *this;
  }
};

// Builds a partial update of Settings. Owns its string values.
class SettingsPatchBuilder {
 public:
  SettingsPatchBuilder() = default;

  SettingsPatchBuilder&& set_id(uint64_t value) && {
    _patch.Set<uint64_t>(Settings::Id, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_id() && {
    _patch.Remove(Settings::Id);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_enabled(bool value) && {
    _patch.Set<bool>(Settings::Enabled, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_enabled() && {
    _patch.Remove(Settings::Enabled);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_retries(int32_t value) && {
    _patch.Set<int32_t>(Settings::Retries, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_retries() && {
    _patch.Remove(Settings::Retries);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_tiny(int8_t value) && {
    _patch.Set<int8_t>(Settings::Tiny, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_tiny() && {
    _patch.Remove(Settings::Tiny);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_huge(uint64_t value) && {
    _patch.Set<uint64_t>(Settings::Huge, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_huge() && {
    _patch.Remove(Settings::Huge);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_lowest(int64_t value) && {
    _patch.Set<int64_t>(Settings::Lowest, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_lowest() && {
    _patch.Remove(Settings::Lowest);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_ratio(float value) && {
    _patch.Set<float>(Settings::Ratio, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_ratio() && {
    _patch.Remove(Settings::Ratio);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_weight(double value) && {
    _patch.Set<double>(Settings::Weight, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_weight() && {
    _patch.Remove(Settings::Weight);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_mask(uint32_t value) && {
    _patch.Set<uint32_t>(Settings::Mask, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_mask() && {
    _patch.Remove(Settings::Mask);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_level(gendb::tests::defaults::Level value) && {
    _patch.Set<uint16_t>(Settings::Level, static_cast<uint16_t>(value));
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_level() && {
    _patch.Remove(Settings::Level);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_title(std::string_view value) && {
    _patch.CopyString(Settings::Title, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_title() && {
    _patch.Remove(Settings::Title);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_tricky(std::string_view value) && {
    _patch.CopyString(Settings::Tricky, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_tricky() && {
    _patch.Remove(Settings::Tricky);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_unicode(std::string_view value) && {
    _patch.CopyString(Settings::Unicode, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_unicode() && {
    _patch.Remove(Settings::Unicode);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_plain_count(int32_t value) && {
    _patch.Set<int32_t>(Settings::PlainCount, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_plain_count() && {
    _patch.Remove(Settings::PlainCount);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_plain_name(std::string_view value) && {
    _patch.CopyString(Settings::PlainName, value);
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_plain_name() && {
    _patch.Remove(Settings::PlainName);
    return std::move(*this);
  }
  SettingsPatchBuilder&& set_plain_level(gendb::tests::defaults::Level value) && {
    _patch.Set<uint16_t>(Settings::PlainLevel, static_cast<uint16_t>(value));
    return std::move(*this);
  }
  SettingsPatchBuilder&& clear_plain_level() && {
    _patch.Remove(Settings::PlainLevel);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests::defaults
