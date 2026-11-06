#pragma once

// Repeated fields. A list is the payload of one Var field, so the message around it does not
// change: a reader that does not know the field keeps its bytes, and a patch replaces it as a
// whole. docs/message_format.md is the wire specification.
//
//   Scalars and enums: the elements, little-endian, each of its type's natural size. No header, as
//   the size gives the count. A bool is one byte, 0 or 1.
//   Strings and messages: [varint: count * 4 + width code] [end offsets: width * count] [payload]
//   An empty list of any type is zero bytes.
//
// There is no alignment, so these read through memcpy and hand out elements by value.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "gendb/message.h"

namespace gendb {

// How deep generated Verify functions follow nested messages, which keeps a hostile message from
// overflowing the stack. Reads do not recurse by themselves.
inline constexpr int kMaxMessageDepth = 64;

namespace detail {
// Goes by index, as what a list hands out is made on the fly.
template <typename List, typename T>
class ListIterator {
 public:
  using iterator_concept = std::forward_iterator_tag;
  using iterator_category = std::input_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using reference = T;
  using pointer = void;

  ListIterator() = default;
  ListIterator(const List* list, size_t index) noexcept : _list(list), _index(index) {}

  T operator*() const { return (*_list)[_index]; }
  ListIterator& operator++() noexcept {
    ++_index;
    return *this;
  }
  ListIterator operator++(int) noexcept {
    ListIterator previous = *this;
    ++_index;
    return previous;
  }
  bool operator==(const ListIterator& other) const noexcept { return _index == other._index; }

 private:
  const List* _list = nullptr;
  size_t _index = 0;
};

inline bool SameBytes(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
  return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

}  // namespace detail

// A list of scalars or enums, over bytes it does not own.
template <ListScalar T>
class ScalarList {
  using Wire = detail::ElementWire<T>;

 public:
  using value_type = T;
  using const_iterator = detail::ListIterator<ScalarList, T>;
  using iterator = const_iterator;

  ScalarList() = default;
  // Whole elements only: anything after them cannot be read.
  explicit ScalarList(std::span<const uint8_t> bytes) noexcept : _bytes(bytes) {}

  // Whether these are the canonical bytes of such a list.
  static bool Verify(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() % sizeof(Wire) != 0) return false;
    if constexpr (std::same_as<Wire, bool>) {
      return std::ranges::all_of(bytes, [](uint8_t byte) { return byte <= 1; });
    } else {
      return true;
    }
  }

  size_t size() const noexcept { return _bytes.size() / sizeof(Wire); }
  bool empty() const noexcept { return size() == 0; }
  // Past the end reads as the type's zero.
  T operator[](size_t index) const noexcept {
    if (index >= size()) return T{};
    return static_cast<T>(detail::Load<Wire>(_bytes.data() + index * sizeof(Wire)));
  }
  const_iterator begin() const noexcept { return {this, 0}; }
  const_iterator end() const noexcept { return {this, size()}; }

  std::vector<T> ToVector() const {
    std::vector<T> result;
    result.reserve(size());
    for (size_t i = 0; i < size(); ++i) result.push_back((*this)[i]);
    return result;
  }
  std::span<const uint8_t> Buffer() const noexcept { return _bytes; }
  bool operator==(const ScalarList& other) const noexcept {
    return detail::SameBytes(_bytes, other._bytes);
  }

 private:
  std::span<const uint8_t> _bytes;
};

// A list of byte strings, over bytes it does not own. Malformed bytes read as an empty list, and an
// element whose offsets contradict each other as an empty element: reads stay inside the buffer.
class VarList {
 public:
  VarList() = default;
  explicit VarList(std::span<const uint8_t> bytes) noexcept;

  // Whether these are the canonical bytes of a list, down to the elements' offsets, but not inside
  // the elements. O(number of elements).
  static bool Verify(std::span<const uint8_t> bytes) noexcept;

  size_t size() const noexcept { return _count; }
  bool empty() const noexcept { return _count == 0; }
  std::span<const uint8_t> operator[](size_t index) const noexcept {
    if (index >= _count) return {};
    const uint32_t start = index ? End(index - 1) : 0;
    const uint32_t end = End(index);
    if (start > end || end > _payload_size) return {};
    return {_payload + start, end - start};
  }
  std::span<const uint8_t> Buffer() const noexcept { return _bytes; }
  bool operator==(const VarList& other) const noexcept {
    return detail::SameBytes(_bytes, other._bytes);
  }

 private:
  uint32_t End(size_t index) const noexcept {
    return detail::LoadWidth(_ends + index * _width, _width);
  }

  std::span<const uint8_t> _bytes;
  const uint8_t* _ends = nullptr;
  const uint8_t* _payload = nullptr;
  uint32_t _count = 0;
  uint32_t _payload_size = 0;
  uint8_t _width = 1;
};

class StringList {
 public:
  using value_type = std::string_view;
  using const_iterator = detail::ListIterator<StringList, std::string_view>;
  using iterator = const_iterator;

  StringList() = default;
  explicit StringList(std::span<const uint8_t> bytes) noexcept : _list(bytes) {}

  size_t size() const noexcept { return _list.size(); }
  bool empty() const noexcept { return _list.empty(); }
  std::string_view operator[](size_t index) const noexcept {
    const std::span<const uint8_t> bytes = _list[index];
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  }
  const_iterator begin() const noexcept { return {this, 0}; }
  const_iterator end() const noexcept { return {this, size()}; }
  std::span<const uint8_t> Buffer() const noexcept { return _list.Buffer(); }
  bool operator==(const StringList& other) const noexcept = default;

 private:
  VarList _list;
};

// A list of messages, each read through a zero-copy `T` as for a single nested one.
template <typename T>
class MessageList {
 public:
  using value_type = T;
  using const_iterator = detail::ListIterator<MessageList, T>;
  using iterator = const_iterator;

  MessageList() = default;
  explicit MessageList(std::span<const uint8_t> bytes) noexcept : _list(bytes) {}

  // Each element with T::Verify, which goes `depth` levels further down.
  static bool Verify(std::span<const uint8_t> bytes, int depth) {
    if (!VarList::Verify(bytes)) return false;
    const VarList list(bytes);
    for (size_t i = 0; i < list.size(); ++i) {
      if (!T::Verify(list[i], depth - 1)) return false;
    }
    return true;
  }

  size_t size() const noexcept { return _list.size(); }
  bool empty() const noexcept { return _list.empty(); }
  T operator[](size_t index) const { return T(_list[index]); }
  const_iterator begin() const noexcept { return {this, 0}; }
  const_iterator end() const noexcept { return {this, size()}; }
  std::span<const uint8_t> Buffer() const noexcept { return _list.Buffer(); }
  bool operator==(const MessageList& other) const noexcept = default;

 private:
  VarList _list;
};

// Makes a list of strings or messages. It owns what it holds, so each element is serialized or
// copied when added. For a builder's own fields, `add_x` is simpler: this is for patches, which
// replace a list as a whole.
class ListBuilder {
 public:
  ListBuilder& AddBytes(std::span<const uint8_t> element);
  ListBuilder& AddString(std::string_view element) {
    return AddBytes({reinterpret_cast<const uint8_t*>(element.data()), element.size()});
  }
  ListBuilder& AddMessage(MessageBuilder& element);
  ListBuilder& AddMessage(MessageBuilder&& element) { return AddMessage(element); }
  // Copies the bytes as they are, so fields this program does not know stay.
  ListBuilder& AddMessage(const MessageView& element);
  void Clear() noexcept {
    _payload.clear();
    _ends.clear();
  }

  size_t size() const noexcept { return _ends.size(); }
  bool empty() const noexcept { return _ends.empty(); }
  size_t SerializedSize() const;
  // Writes exactly SerializedSize() bytes.
  void SerializeTo(std::span<uint8_t> out) const;
  Bytes Build() const {
    Bytes result(SerializedSize());
    SerializeTo(result);
    return result;
  }

 private:
  Bytes _payload;
  std::vector<uint32_t> _ends;
};

// The bytes of a list of scalars or enums.
template <ListScalar T>
Bytes PackList(std::span<const T> values) {
  using Wire = detail::ElementWire<T>;
  Bytes result(values.size() * sizeof(Wire));
  uint8_t* out = result.data();
  for (const T value : values) {
    detail::Store<Wire>(out, static_cast<Wire>(value));
    out += sizeof(Wire);
  }
  return result;
}

// The bytes of a list of strings.
Bytes PackList(std::span<const std::string_view> values);

}  // namespace gendb
