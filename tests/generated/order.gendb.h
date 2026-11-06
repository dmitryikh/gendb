// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>

#include "gendb/list.h"
#include "gendb/message.h"
#include "gendb/reflection.h"

// Enum definitions
namespace gendb::tests {
enum class OrderStatus : uint16_t {
  kNew = 0,
  kPaid = 1,
  kShipped = 2,
};

// Enum value arrays for reflection
inline constexpr std::array<gendb::EnumValueInfo, 3> kOrderStatusValues = {
    gendb::EnumValueInfo{"kNew", 0},
    gendb::EnumValueInfo{"kPaid", 1},
    gendb::EnumValueInfo{"kShipped", 2},
};
}  // namespace gendb::tests

// Message classes, which can refer to each other in any order, and to themselves
namespace gendb::tests {
class Order;
class OrderBuilder;
}  // namespace gendb::tests
namespace gendb::tests {
class Customer;
class CustomerBuilder;
}  // namespace gendb::tests
namespace gendb::tests {
class Address;
class AddressBuilder;
}  // namespace gendb::tests
namespace gendb::tests {
class OrderLine;
class OrderLineBuilder;
}  // namespace gendb::tests
namespace gendb::tests {
class TreeNode;
class TreeNodeBuilder;
}  // namespace gendb::tests

namespace gendb::tests {
// Zero-copy view of a serialized Order. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class Order {
 public:
  enum Field : gendb::FieldId {
    OrderId = 1,
    Note = 2,
    Status = 3,
    Customer = 4,
    Lines = 5,
    Tags = 6,
    Prices = 7,
    Quantities = 8,
    Flags = 9,
    History = 10,
    // 11 is reserved: `old_lines` is deprecated. Never reuse this ID.
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 10> kFieldsInfo = {
      gendb::FieldInfo{"order_id", OrderId, gendb::FieldInfo::SCALAR, gendb::FieldInfo::UINT64, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"note", Note, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"status", Status, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT16,
                       "gendb::tests::OrderStatus",
                       std::span<const gendb::EnumValueInfo>(kOrderStatusValues), false},
      gendb::FieldInfo{"customer", Customer, gendb::FieldInfo::MESSAGE,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>(),
                       false},
      gendb::FieldInfo{"lines", Lines, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), true},
      gendb::FieldInfo{"tags", Tags, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), true},
      gendb::FieldInfo{"prices", Prices, gendb::FieldInfo::SCALAR, gendb::FieldInfo::DOUBLE, "",
                       std::span<const gendb::EnumValueInfo>(), true},
      gendb::FieldInfo{"quantities", Quantities, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT16,
                       "", std::span<const gendb::EnumValueInfo>(), true},
      gendb::FieldInfo{"flags", Flags, gendb::FieldInfo::SCALAR, gendb::FieldInfo::BOOL, "",
                       std::span<const gendb::EnumValueInfo>(), true},
      gendb::FieldInfo{"history", History, gendb::FieldInfo::ENUM, gendb::FieldInfo::UINT16,
                       "gendb::tests::OrderStatus",
                       std::span<const gendb::EnumValueInfo>(kOrderStatusValues), true},
  };

  Order() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Order(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const Order& other) const { return _view == other._view; }

  bool has_order_id() const { return _view.Has<OrderId, gendb::KindOf<uint64_t>>(); }
  uint64_t order_id() const { return _view.Get<OrderId, uint64_t>(0ULL); }
  bool has_note() const { return _view.Has<Note, gendb::Kind::Var>(); }
  std::string_view note() const { return _view.GetString<Note>(std::string_view("none", 4)); }
  bool has_status() const { return _view.Has<Status, gendb::KindOf<uint16_t>>(); }
  gendb::tests::OrderStatus status() const {
    return static_cast<gendb::tests::OrderStatus>(_view.Get<Status, uint16_t>(0));
  }
  // Empty if not set, which has_customer() tells apart from being set to an empty message.
  bool has_customer() const { return _view.Has<Customer, gendb::Kind::Var>(); }
  ::gendb::tests::Customer customer() const;
  // Empty if not set, which has_lines() tells apart from being set to an empty list.
  bool has_lines() const { return _view.Has<Lines, gendb::Kind::Var>(); }
  gendb::MessageList<::gendb::tests::OrderLine> lines() const {
    return gendb::MessageList<::gendb::tests::OrderLine>(
        _view.GetBytes<Lines>().value_or(std::span<const uint8_t>{}));
  }
  // Empty if not set, which has_tags() tells apart from being set to an empty list.
  bool has_tags() const { return _view.Has<Tags, gendb::Kind::Var>(); }
  gendb::StringList tags() const {
    return gendb::StringList(_view.GetBytes<Tags>().value_or(std::span<const uint8_t>{}));
  }
  // Empty if not set, which has_prices() tells apart from being set to an empty list.
  bool has_prices() const { return _view.Has<Prices, gendb::Kind::Var>(); }
  gendb::ScalarList<double> prices() const {
    return gendb::ScalarList<double>(_view.GetBytes<Prices>().value_or(std::span<const uint8_t>{}));
  }
  // Empty if not set, which has_quantities() tells apart from being set to an empty list.
  bool has_quantities() const { return _view.Has<Quantities, gendb::Kind::Var>(); }
  gendb::ScalarList<int16_t> quantities() const {
    return gendb::ScalarList<int16_t>(
        _view.GetBytes<Quantities>().value_or(std::span<const uint8_t>{}));
  }
  // Empty if not set, which has_flags() tells apart from being set to an empty list.
  bool has_flags() const { return _view.Has<Flags, gendb::Kind::Var>(); }
  gendb::ScalarList<bool> flags() const {
    return gendb::ScalarList<bool>(_view.GetBytes<Flags>().value_or(std::span<const uint8_t>{}));
  }
  // Empty if not set, which has_history() tells apart from being set to an empty list.
  bool has_history() const { return _view.Has<History, gendb::Kind::Var>(); }
  gendb::ScalarList<gendb::tests::OrderStatus> history() const {
    return gendb::ScalarList<gendb::tests::OrderStatus>(
        _view.GetBytes<History>().value_or(std::span<const uint8_t>{}));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Order. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class OrderBuilder : public gendb::MessageBuilder {
 public:
  OrderBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit OrderBuilder(const Order& source) : MessageBuilder(source.View()) {}

  OrderBuilder& set_order_id(uint64_t value) {
    AddField<uint64_t>(Order::OrderId, value);
    return *this;
  }
  OrderBuilder& clear_order_id() {
    ClearField(Order::OrderId);
    return *this;
  }
  OrderBuilder& set_note(std::string_view value) {
    AddStringField(Order::Note, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  OrderBuilder& set_note(S&&) = delete;
  OrderBuilder& clear_note() {
    ClearField(Order::Note);
    return *this;
  }
  OrderBuilder& set_status(gendb::tests::OrderStatus value) {
    AddField<uint16_t>(Order::Status, static_cast<uint16_t>(value));
    return *this;
  }
  OrderBuilder& clear_status() {
    ClearField(Order::Status);
    return *this;
  }
  OrderBuilder& set_customer(::gendb::tests::CustomerBuilder& value);
  OrderBuilder& set_customer(::gendb::tests::CustomerBuilder&& value);
  OrderBuilder& set_customer(const ::gendb::tests::Customer& value);
  OrderBuilder& clear_customer() {
    ClearField(Order::Customer);
    return *this;
  }
  OrderBuilder& add_lines(::gendb::tests::OrderLineBuilder& value);
  OrderBuilder& add_lines(::gendb::tests::OrderLineBuilder&& value);
  OrderBuilder& add_lines(const ::gendb::tests::OrderLine& value);
  OrderBuilder& set_lines(const gendb::ListBuilder& value) {
    CopyBytesField(Order::Lines, value.Build());
    return *this;
  }
  OrderBuilder& clear_lines() {
    ClearField(Order::Lines);
    return *this;
  }
  OrderBuilder& add_tags(std::string_view value) {
    AddRepeatedString(Order::Tags, value);
    return *this;
  }
  OrderBuilder& set_tags(std::span<const std::string_view> value) {
    SetEmptyList(Order::Tags);
    for (const std::string_view element : value) AddRepeatedString(Order::Tags, element);
    return *this;
  }
  OrderBuilder& set_tags(std::initializer_list<std::string_view> value) {
    return set_tags(std::span<const std::string_view>(value.begin(), value.size()));
  }
  OrderBuilder& clear_tags() {
    ClearField(Order::Tags);
    return *this;
  }
  OrderBuilder& add_prices(double value) {
    AddRepeated<double>(Order::Prices, value);
    return *this;
  }
  OrderBuilder& set_prices(std::span<const double> value) {
    SetRepeated<double>(Order::Prices, value);
    return *this;
  }
  OrderBuilder& set_prices(std::initializer_list<double> value) {
    return set_prices(std::span<const double>(value.begin(), value.size()));
  }
  OrderBuilder& clear_prices() {
    ClearField(Order::Prices);
    return *this;
  }
  OrderBuilder& add_quantities(int16_t value) {
    AddRepeated<int16_t>(Order::Quantities, value);
    return *this;
  }
  OrderBuilder& set_quantities(std::span<const int16_t> value) {
    SetRepeated<int16_t>(Order::Quantities, value);
    return *this;
  }
  OrderBuilder& set_quantities(std::initializer_list<int16_t> value) {
    return set_quantities(std::span<const int16_t>(value.begin(), value.size()));
  }
  OrderBuilder& clear_quantities() {
    ClearField(Order::Quantities);
    return *this;
  }
  OrderBuilder& add_flags(bool value) {
    AddRepeated<bool>(Order::Flags, value);
    return *this;
  }
  OrderBuilder& set_flags(std::span<const bool> value) {
    SetRepeated<bool>(Order::Flags, value);
    return *this;
  }
  OrderBuilder& set_flags(std::initializer_list<bool> value) {
    return set_flags(std::span<const bool>(value.begin(), value.size()));
  }
  OrderBuilder& clear_flags() {
    ClearField(Order::Flags);
    return *this;
  }
  OrderBuilder& add_history(gendb::tests::OrderStatus value) {
    AddRepeated<gendb::tests::OrderStatus>(Order::History, value);
    return *this;
  }
  OrderBuilder& set_history(std::span<const gendb::tests::OrderStatus> value) {
    SetRepeated<gendb::tests::OrderStatus>(Order::History, value);
    return *this;
  }
  OrderBuilder& set_history(std::initializer_list<gendb::tests::OrderStatus> value) {
    return set_history(std::span<const gendb::tests::OrderStatus>(value.begin(), value.size()));
  }
  OrderBuilder& clear_history() {
    ClearField(Order::History);
    return *this;
  }
};

// Builds a partial update of Order. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class OrderPatchBuilder {
 public:
  OrderPatchBuilder() = default;

  OrderPatchBuilder&& set_order_id(uint64_t value) && {
    _patch.Set<uint64_t>(Order::OrderId, value);
    return std::move(*this);
  }
  OrderPatchBuilder&& clear_order_id() && {
    _patch.Remove(Order::OrderId);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_note(std::string_view value) && {
    _patch.CopyString(Order::Note, value);
    return std::move(*this);
  }
  OrderPatchBuilder&& clear_note() && {
    _patch.Remove(Order::Note);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_status(gendb::tests::OrderStatus value) && {
    _patch.Set<uint16_t>(Order::Status, static_cast<uint16_t>(value));
    return std::move(*this);
  }
  OrderPatchBuilder&& clear_status() && {
    _patch.Remove(Order::Status);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_customer(::gendb::tests::CustomerBuilder& value) &&;
  OrderPatchBuilder&& set_customer(::gendb::tests::CustomerBuilder&& value) &&;
  OrderPatchBuilder&& set_customer(const ::gendb::tests::Customer& value) &&;
  OrderPatchBuilder&& clear_customer() && {
    _patch.Remove(Order::Customer);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_lines(const gendb::ListBuilder& value) && {
    _patch.CopyBytes(Order::Lines, value.Build());
    return std::move(*this);
  }
  OrderPatchBuilder&& clear_lines() && {
    _patch.Remove(Order::Lines);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_tags(std::span<const std::string_view> value) && {
    _patch.CopyBytes(Order::Tags, gendb::PackList(value));
    return std::move(*this);
  }
  OrderPatchBuilder&& set_tags(std::initializer_list<std::string_view> value) && {
    return std::move(*this).set_tags(
        std::span<const std::string_view>(value.begin(), value.size()));
  }
  OrderPatchBuilder&& clear_tags() && {
    _patch.Remove(Order::Tags);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_prices(std::span<const double> value) && {
    _patch.CopyBytes(Order::Prices, gendb::PackList<double>(value));
    return std::move(*this);
  }
  OrderPatchBuilder&& set_prices(std::initializer_list<double> value) && {
    return std::move(*this).set_prices(std::span<const double>(value.begin(), value.size()));
  }
  OrderPatchBuilder&& clear_prices() && {
    _patch.Remove(Order::Prices);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_quantities(std::span<const int16_t> value) && {
    _patch.CopyBytes(Order::Quantities, gendb::PackList<int16_t>(value));
    return std::move(*this);
  }
  OrderPatchBuilder&& set_quantities(std::initializer_list<int16_t> value) && {
    return std::move(*this).set_quantities(std::span<const int16_t>(value.begin(), value.size()));
  }
  OrderPatchBuilder&& clear_quantities() && {
    _patch.Remove(Order::Quantities);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_flags(std::span<const bool> value) && {
    _patch.CopyBytes(Order::Flags, gendb::PackList<bool>(value));
    return std::move(*this);
  }
  OrderPatchBuilder&& set_flags(std::initializer_list<bool> value) && {
    return std::move(*this).set_flags(std::span<const bool>(value.begin(), value.size()));
  }
  OrderPatchBuilder&& clear_flags() && {
    _patch.Remove(Order::Flags);
    return std::move(*this);
  }
  OrderPatchBuilder&& set_history(std::span<const gendb::tests::OrderStatus> value) && {
    _patch.CopyBytes(Order::History, gendb::PackList<gendb::tests::OrderStatus>(value));
    return std::move(*this);
  }
  OrderPatchBuilder&& set_history(std::initializer_list<gendb::tests::OrderStatus> value) && {
    return std::move(*this).set_history(
        std::span<const gendb::tests::OrderStatus>(value.begin(), value.size()));
  }
  OrderPatchBuilder&& clear_history() && {
    _patch.Remove(Order::History);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
namespace gendb::tests {
// Zero-copy view of a serialized Customer. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class Customer {
 public:
  enum Field : gendb::FieldId {
    Name = 1,
    Address = 2,
    Phones = 3,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 3> kFieldsInfo = {
      gendb::FieldInfo{"name", Name, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"address", Address, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR,
                       "", std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"phones", Phones, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), true},
  };

  Customer() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Customer(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const Customer& other) const { return _view == other._view; }

  bool has_name() const { return _view.Has<Name, gendb::Kind::Var>(); }
  std::string_view name() const { return _view.GetString<Name>(); }
  // Empty if not set, which has_address() tells apart from being set to an empty message.
  bool has_address() const { return _view.Has<Address, gendb::Kind::Var>(); }
  ::gendb::tests::Address address() const;
  // Empty if not set, which has_phones() tells apart from being set to an empty list.
  bool has_phones() const { return _view.Has<Phones, gendb::Kind::Var>(); }
  gendb::StringList phones() const {
    return gendb::StringList(_view.GetBytes<Phones>().value_or(std::span<const uint8_t>{}));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Customer. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class CustomerBuilder : public gendb::MessageBuilder {
 public:
  CustomerBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit CustomerBuilder(const Customer& source) : MessageBuilder(source.View()) {}

  CustomerBuilder& set_name(std::string_view value) {
    AddStringField(Customer::Name, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  CustomerBuilder& set_name(S&&) = delete;
  CustomerBuilder& clear_name() {
    ClearField(Customer::Name);
    return *this;
  }
  CustomerBuilder& set_address(::gendb::tests::AddressBuilder& value);
  CustomerBuilder& set_address(::gendb::tests::AddressBuilder&& value);
  CustomerBuilder& set_address(const ::gendb::tests::Address& value);
  CustomerBuilder& clear_address() {
    ClearField(Customer::Address);
    return *this;
  }
  CustomerBuilder& add_phones(std::string_view value) {
    AddRepeatedString(Customer::Phones, value);
    return *this;
  }
  CustomerBuilder& set_phones(std::span<const std::string_view> value) {
    SetEmptyList(Customer::Phones);
    for (const std::string_view element : value) AddRepeatedString(Customer::Phones, element);
    return *this;
  }
  CustomerBuilder& set_phones(std::initializer_list<std::string_view> value) {
    return set_phones(std::span<const std::string_view>(value.begin(), value.size()));
  }
  CustomerBuilder& clear_phones() {
    ClearField(Customer::Phones);
    return *this;
  }
};

// Builds a partial update of Customer. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class CustomerPatchBuilder {
 public:
  CustomerPatchBuilder() = default;

  CustomerPatchBuilder&& set_name(std::string_view value) && {
    _patch.CopyString(Customer::Name, value);
    return std::move(*this);
  }
  CustomerPatchBuilder&& clear_name() && {
    _patch.Remove(Customer::Name);
    return std::move(*this);
  }
  CustomerPatchBuilder&& set_address(::gendb::tests::AddressBuilder& value) &&;
  CustomerPatchBuilder&& set_address(::gendb::tests::AddressBuilder&& value) &&;
  CustomerPatchBuilder&& set_address(const ::gendb::tests::Address& value) &&;
  CustomerPatchBuilder&& clear_address() && {
    _patch.Remove(Customer::Address);
    return std::move(*this);
  }
  CustomerPatchBuilder&& set_phones(std::span<const std::string_view> value) && {
    _patch.CopyBytes(Customer::Phones, gendb::PackList(value));
    return std::move(*this);
  }
  CustomerPatchBuilder&& set_phones(std::initializer_list<std::string_view> value) && {
    return std::move(*this).set_phones(
        std::span<const std::string_view>(value.begin(), value.size()));
  }
  CustomerPatchBuilder&& clear_phones() && {
    _patch.Remove(Customer::Phones);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
namespace gendb::tests {
// Zero-copy view of a serialized Address. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class Address {
 public:
  enum Field : gendb::FieldId {
    City = 1,
    Zip = 2,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 2> kFieldsInfo = {
      gendb::FieldInfo{"city", City, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"zip", Zip, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  Address() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  Address(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const Address& other) const { return _view == other._view; }

  bool has_city() const { return _view.Has<City, gendb::Kind::Var>(); }
  std::string_view city() const { return _view.GetString<City>(); }
  bool has_zip() const { return _view.Has<Zip, gendb::KindOf<int32_t>>(); }
  int32_t zip() const { return _view.Get<Zip, int32_t>(0); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized Address. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class AddressBuilder : public gendb::MessageBuilder {
 public:
  AddressBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit AddressBuilder(const Address& source) : MessageBuilder(source.View()) {}

  AddressBuilder& set_city(std::string_view value) {
    AddStringField(Address::City, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  AddressBuilder& set_city(S&&) = delete;
  AddressBuilder& clear_city() {
    ClearField(Address::City);
    return *this;
  }
  AddressBuilder& set_zip(int32_t value) {
    AddField<int32_t>(Address::Zip, value);
    return *this;
  }
  AddressBuilder& clear_zip() {
    ClearField(Address::Zip);
    return *this;
  }
};

// Builds a partial update of Address. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class AddressPatchBuilder {
 public:
  AddressPatchBuilder() = default;

  AddressPatchBuilder&& set_city(std::string_view value) && {
    _patch.CopyString(Address::City, value);
    return std::move(*this);
  }
  AddressPatchBuilder&& clear_city() && {
    _patch.Remove(Address::City);
    return std::move(*this);
  }
  AddressPatchBuilder&& set_zip(int32_t value) && {
    _patch.Set<int32_t>(Address::Zip, value);
    return std::move(*this);
  }
  AddressPatchBuilder&& clear_zip() && {
    _patch.Remove(Address::Zip);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
namespace gendb::tests {
// Zero-copy view of a serialized OrderLine. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class OrderLine {
 public:
  enum Field : gendb::FieldId {
    Sku = 1,
    Quantity = 2,
    Price = 3,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 3> kFieldsInfo = {
      gendb::FieldInfo{"sku", Sku, gendb::FieldInfo::STRING, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"quantity", Quantity, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"price", Price, gendb::FieldInfo::SCALAR, gendb::FieldInfo::DOUBLE, "",
                       std::span<const gendb::EnumValueInfo>(), false},
  };

  OrderLine() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  OrderLine(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const OrderLine& other) const { return _view == other._view; }

  bool has_sku() const { return _view.Has<Sku, gendb::Kind::Var>(); }
  std::string_view sku() const { return _view.GetString<Sku>(); }
  bool has_quantity() const { return _view.Has<Quantity, gendb::KindOf<int32_t>>(); }
  int32_t quantity() const { return _view.Get<Quantity, int32_t>(0); }
  bool has_price() const { return _view.Has<Price, gendb::KindOf<double>>(); }
  double price() const { return _view.Get<Price, double>(0.0); }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized OrderLine. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class OrderLineBuilder : public gendb::MessageBuilder {
 public:
  OrderLineBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit OrderLineBuilder(const OrderLine& source) : MessageBuilder(source.View()) {}

  OrderLineBuilder& set_sku(std::string_view value) {
    AddStringField(OrderLine::Sku, value);
    return *this;
  }
  template <gendb::TemporaryString S>
  OrderLineBuilder& set_sku(S&&) = delete;
  OrderLineBuilder& clear_sku() {
    ClearField(OrderLine::Sku);
    return *this;
  }
  OrderLineBuilder& set_quantity(int32_t value) {
    AddField<int32_t>(OrderLine::Quantity, value);
    return *this;
  }
  OrderLineBuilder& clear_quantity() {
    ClearField(OrderLine::Quantity);
    return *this;
  }
  OrderLineBuilder& set_price(double value) {
    AddField<double>(OrderLine::Price, value);
    return *this;
  }
  OrderLineBuilder& clear_price() {
    ClearField(OrderLine::Price);
    return *this;
  }
};

// Builds a partial update of OrderLine. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class OrderLinePatchBuilder {
 public:
  OrderLinePatchBuilder() = default;

  OrderLinePatchBuilder&& set_sku(std::string_view value) && {
    _patch.CopyString(OrderLine::Sku, value);
    return std::move(*this);
  }
  OrderLinePatchBuilder&& clear_sku() && {
    _patch.Remove(OrderLine::Sku);
    return std::move(*this);
  }
  OrderLinePatchBuilder&& set_quantity(int32_t value) && {
    _patch.Set<int32_t>(OrderLine::Quantity, value);
    return std::move(*this);
  }
  OrderLinePatchBuilder&& clear_quantity() && {
    _patch.Remove(OrderLine::Quantity);
    return std::move(*this);
  }
  OrderLinePatchBuilder&& set_price(double value) && {
    _patch.Set<double>(OrderLine::Price, value);
    return std::move(*this);
  }
  OrderLinePatchBuilder&& clear_price() && {
    _patch.Remove(OrderLine::Price);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests
namespace gendb::tests {
// Zero-copy view of a serialized TreeNode. Does not own the bytes. A getter returns the
// field's default if it is not set, which has_x() tells apart from being set to that value.
class TreeNode {
 public:
  enum Field : gendb::FieldId {
    Value = 1,
    Next = 2,
    Children = 3,
  };

  // Field reflection metadata using common FieldInfo struct
  static constexpr std::array<gendb::FieldInfo, 3> kFieldsInfo = {
      gendb::FieldInfo{"value", Value, gendb::FieldInfo::SCALAR, gendb::FieldInfo::INT32, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"next", Next, gendb::FieldInfo::MESSAGE, gendb::FieldInfo::NOT_SCALAR, "",
                       std::span<const gendb::EnumValueInfo>(), false},
      gendb::FieldInfo{"children", Children, gendb::FieldInfo::MESSAGE,
                       gendb::FieldInfo::NOT_SCALAR, "", std::span<const gendb::EnumValueInfo>(),
                       true},
  };

  TreeNode() = default;
  // The bytes must outlive the view. Malformed bytes read as an empty message; check untrusted
  // input with gendb::MessageView::Verify first.
  TreeNode(std::span<const uint8_t> bytes) : _view(gendb::MessageView::OpenOrEmpty(bytes)) {}

  // Whether these are the canonical bytes of such a message, down through its nested messages and
  // lists. Linear in the size, and no deeper than `depth`. Fields this program does not know are
  // checked only from the outside. Call it first on bytes from outside the process.
  static bool Verify(std::span<const uint8_t> bytes, int depth = gendb::kMaxMessageDepth);
  // Byte for byte, which the canonical encoding makes the same as field by field.
  bool operator==(const TreeNode& other) const { return _view == other._view; }

  bool has_value() const { return _view.Has<Value, gendb::KindOf<int32_t>>(); }
  int32_t value() const { return _view.Get<Value, int32_t>(0); }
  // Empty if not set, which has_next() tells apart from being set to an empty message.
  bool has_next() const { return _view.Has<Next, gendb::Kind::Var>(); }
  ::gendb::tests::TreeNode next() const;
  // Empty if not set, which has_children() tells apart from being set to an empty list.
  bool has_children() const { return _view.Has<Children, gendb::Kind::Var>(); }
  gendb::MessageList<::gendb::tests::TreeNode> children() const {
    return gendb::MessageList<::gendb::tests::TreeNode>(
        _view.GetBytes<Children>().value_or(std::span<const uint8_t>{}));
  }

  // Any field by ID, including unknown ones: this schema's future or deprecated fields.
  bool HasField(gendb::FieldId id) const { return _view.Has(id); }
  uint32_t FieldCount() const { return _view.FieldCount(); }
  const gendb::MessageView& View() const { return _view; }

 private:
  gendb::MessageView _view;
};

// Builds a serialized TreeNode. Strings are borrowed: they must stay alive until Build().
// Nested messages and repeated fields are serialized or copied when set, so they can go right away.
class TreeNodeBuilder : public gendb::MessageBuilder {
 public:
  TreeNodeBuilder() = default;
  // Starts from all fields of `source`, known or unknown; its bytes must outlive the builder.
  explicit TreeNodeBuilder(const TreeNode& source) : MessageBuilder(source.View()) {}

  TreeNodeBuilder& set_value(int32_t value) {
    AddField<int32_t>(TreeNode::Value, value);
    return *this;
  }
  TreeNodeBuilder& clear_value() {
    ClearField(TreeNode::Value);
    return *this;
  }
  TreeNodeBuilder& set_next(::gendb::tests::TreeNodeBuilder& value);
  TreeNodeBuilder& set_next(::gendb::tests::TreeNodeBuilder&& value);
  TreeNodeBuilder& set_next(const ::gendb::tests::TreeNode& value);
  TreeNodeBuilder& clear_next() {
    ClearField(TreeNode::Next);
    return *this;
  }
  TreeNodeBuilder& add_children(::gendb::tests::TreeNodeBuilder& value);
  TreeNodeBuilder& add_children(::gendb::tests::TreeNodeBuilder&& value);
  TreeNodeBuilder& add_children(const ::gendb::tests::TreeNode& value);
  TreeNodeBuilder& set_children(const gendb::ListBuilder& value) {
    CopyBytesField(TreeNode::Children, value.Build());
    return *this;
  }
  TreeNodeBuilder& clear_children() {
    ClearField(TreeNode::Children);
    return *this;
  }
};

// Builds a partial update of TreeNode. Owns its string values. A nested message or a list is
// replaced as a whole, which keeps what the log holds a flat list of fields.
class TreeNodePatchBuilder {
 public:
  TreeNodePatchBuilder() = default;

  TreeNodePatchBuilder&& set_value(int32_t value) && {
    _patch.Set<int32_t>(TreeNode::Value, value);
    return std::move(*this);
  }
  TreeNodePatchBuilder&& clear_value() && {
    _patch.Remove(TreeNode::Value);
    return std::move(*this);
  }
  TreeNodePatchBuilder&& set_next(::gendb::tests::TreeNodeBuilder& value) &&;
  TreeNodePatchBuilder&& set_next(::gendb::tests::TreeNodeBuilder&& value) &&;
  TreeNodePatchBuilder&& set_next(const ::gendb::tests::TreeNode& value) &&;
  TreeNodePatchBuilder&& clear_next() && {
    _patch.Remove(TreeNode::Next);
    return std::move(*this);
  }
  TreeNodePatchBuilder&& set_children(const gendb::ListBuilder& value) && {
    _patch.CopyBytes(TreeNode::Children, value.Build());
    return std::move(*this);
  }
  TreeNodePatchBuilder&& clear_children() && {
    _patch.Remove(TreeNode::Children);
    return std::move(*this);
  }

  gendb::MessagePatch Build() && { return std::move(_patch); }

 private:
  gendb::MessagePatch _patch;
};
}  // namespace gendb::tests

// What needs every message class to be complete.
namespace gendb::tests {
inline bool Order::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  const gendb::MessageView message = gendb::MessageView::OpenOrEmpty(bytes);
  if (const auto f = message.GetBytes<Customer>();
      f && !::gendb::tests::Customer::Verify(*f, depth - 1))
    return false;
  if (const auto f = message.GetBytes<Lines>();
      f && !gendb::MessageList<::gendb::tests::OrderLine>::Verify(*f, depth))
    return false;
  if (const auto f = message.GetBytes<Tags>(); f && !gendb::VarList::Verify(*f)) return false;
  if (const auto f = message.GetBytes<Prices>(); f && !gendb::ScalarList<double>::Verify(*f))
    return false;
  if (const auto f = message.GetBytes<Quantities>(); f && !gendb::ScalarList<int16_t>::Verify(*f))
    return false;
  if (const auto f = message.GetBytes<Flags>(); f && !gendb::ScalarList<bool>::Verify(*f))
    return false;
  if (const auto f = message.GetBytes<History>();
      f && !gendb::ScalarList<gendb::tests::OrderStatus>::Verify(*f))
    return false;
  return true;
}

inline ::gendb::tests::Customer Order::customer() const {
  return ::gendb::tests::Customer(_view.GetBytes<Customer>().value_or(std::span<const uint8_t>{}));
}
inline OrderBuilder& OrderBuilder::set_customer(::gendb::tests::CustomerBuilder& value) {
  AddMessageField(Order::Customer, value);
  return *this;
}
inline OrderBuilder& OrderBuilder::set_customer(::gendb::tests::CustomerBuilder&& value) {
  AddMessageField(Order::Customer, value);
  return *this;
}
inline OrderBuilder& OrderBuilder::set_customer(const ::gendb::tests::Customer& value) {
  AddMessageField(Order::Customer, value.View());
  return *this;
}
inline OrderPatchBuilder&& OrderPatchBuilder::set_customer(
    ::gendb::tests::CustomerBuilder& value) && {
  _patch.CopyMessage(Order::Customer, value);
  return std::move(*this);
}
inline OrderPatchBuilder&& OrderPatchBuilder::set_customer(
    ::gendb::tests::CustomerBuilder&& value) && {
  _patch.CopyMessage(Order::Customer, value);
  return std::move(*this);
}
inline OrderPatchBuilder&& OrderPatchBuilder::set_customer(
    const ::gendb::tests::Customer& value) && {
  _patch.CopyMessage(Order::Customer, value.View());
  return std::move(*this);
}

inline OrderBuilder& OrderBuilder::add_lines(::gendb::tests::OrderLineBuilder& value) {
  AddRepeatedMessage(Order::Lines, value);
  return *this;
}
inline OrderBuilder& OrderBuilder::add_lines(::gendb::tests::OrderLineBuilder&& value) {
  AddRepeatedMessage(Order::Lines, value);
  return *this;
}
inline OrderBuilder& OrderBuilder::add_lines(const ::gendb::tests::OrderLine& value) {
  AddRepeatedMessage(Order::Lines, value.View());
  return *this;
}
}  // namespace gendb::tests
namespace gendb::tests {
inline bool Customer::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  const gendb::MessageView message = gendb::MessageView::OpenOrEmpty(bytes);
  if (const auto f = message.GetBytes<Address>();
      f && !::gendb::tests::Address::Verify(*f, depth - 1))
    return false;
  if (const auto f = message.GetBytes<Phones>(); f && !gendb::VarList::Verify(*f)) return false;
  return true;
}

inline ::gendb::tests::Address Customer::address() const {
  return ::gendb::tests::Address(_view.GetBytes<Address>().value_or(std::span<const uint8_t>{}));
}
inline CustomerBuilder& CustomerBuilder::set_address(::gendb::tests::AddressBuilder& value) {
  AddMessageField(Customer::Address, value);
  return *this;
}
inline CustomerBuilder& CustomerBuilder::set_address(::gendb::tests::AddressBuilder&& value) {
  AddMessageField(Customer::Address, value);
  return *this;
}
inline CustomerBuilder& CustomerBuilder::set_address(const ::gendb::tests::Address& value) {
  AddMessageField(Customer::Address, value.View());
  return *this;
}
inline CustomerPatchBuilder&& CustomerPatchBuilder::set_address(
    ::gendb::tests::AddressBuilder& value) && {
  _patch.CopyMessage(Customer::Address, value);
  return std::move(*this);
}
inline CustomerPatchBuilder&& CustomerPatchBuilder::set_address(
    ::gendb::tests::AddressBuilder&& value) && {
  _patch.CopyMessage(Customer::Address, value);
  return std::move(*this);
}
inline CustomerPatchBuilder&& CustomerPatchBuilder::set_address(
    const ::gendb::tests::Address& value) && {
  _patch.CopyMessage(Customer::Address, value.View());
  return std::move(*this);
}
}  // namespace gendb::tests
namespace gendb::tests {
inline bool Address::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests
namespace gendb::tests {
inline bool OrderLine::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  return true;
}
}  // namespace gendb::tests
namespace gendb::tests {
inline bool TreeNode::Verify(std::span<const uint8_t> bytes, int depth) {
  if (depth <= 0 || !gendb::MessageView::Verify(bytes)) return false;
  const gendb::MessageView message = gendb::MessageView::OpenOrEmpty(bytes);
  if (const auto f = message.GetBytes<Next>();
      f && !::gendb::tests::TreeNode::Verify(*f, depth - 1))
    return false;
  if (const auto f = message.GetBytes<Children>();
      f && !gendb::MessageList<::gendb::tests::TreeNode>::Verify(*f, depth))
    return false;
  return true;
}

inline ::gendb::tests::TreeNode TreeNode::next() const {
  return ::gendb::tests::TreeNode(_view.GetBytes<Next>().value_or(std::span<const uint8_t>{}));
}
inline TreeNodeBuilder& TreeNodeBuilder::set_next(::gendb::tests::TreeNodeBuilder& value) {
  AddMessageField(TreeNode::Next, value);
  return *this;
}
inline TreeNodeBuilder& TreeNodeBuilder::set_next(::gendb::tests::TreeNodeBuilder&& value) {
  AddMessageField(TreeNode::Next, value);
  return *this;
}
inline TreeNodeBuilder& TreeNodeBuilder::set_next(const ::gendb::tests::TreeNode& value) {
  AddMessageField(TreeNode::Next, value.View());
  return *this;
}
inline TreeNodePatchBuilder&& TreeNodePatchBuilder::set_next(
    ::gendb::tests::TreeNodeBuilder& value) && {
  _patch.CopyMessage(TreeNode::Next, value);
  return std::move(*this);
}
inline TreeNodePatchBuilder&& TreeNodePatchBuilder::set_next(
    ::gendb::tests::TreeNodeBuilder&& value) && {
  _patch.CopyMessage(TreeNode::Next, value);
  return std::move(*this);
}
inline TreeNodePatchBuilder&& TreeNodePatchBuilder::set_next(
    const ::gendb::tests::TreeNode& value) && {
  _patch.CopyMessage(TreeNode::Next, value.View());
  return std::move(*this);
}

inline TreeNodeBuilder& TreeNodeBuilder::add_children(::gendb::tests::TreeNodeBuilder& value) {
  AddRepeatedMessage(TreeNode::Children, value);
  return *this;
}
inline TreeNodeBuilder& TreeNodeBuilder::add_children(::gendb::tests::TreeNodeBuilder&& value) {
  AddRepeatedMessage(TreeNode::Children, value);
  return *this;
}
inline TreeNodeBuilder& TreeNodeBuilder::add_children(const ::gendb::tests::TreeNode& value) {
  AddRepeatedMessage(TreeNode::Children, value.View());
  return *this;
}
}  // namespace gendb::tests
