#pragma once

// GenDB message format: per-storage-class presence bitmaps indexed by field ID, inline
// fixed-width scalar slots, and end offsets only for variable-length fields.
// docs/message_format.md is the wire specification.

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "gendb/bytes.h"

namespace gendb {

using FieldId = uint32_t;

class MessagePatch;

inline constexpr uint8_t kMessageVersion = 1;
// Header with one-byte per-class counts; kWideHeaderSize when any count exceeds 255.
inline constexpr size_t kHeaderSize = 9;
inline constexpr size_t kWideHeaderSize = 12;
// Header byte 1: bits 0-1 end-offset width code, bit 2 two-byte counts, others zero.
inline constexpr uint8_t kWideCountsFlag = 4;
// Bitmap lengths are stored as one byte per class, so at most 255 * 8 field IDs.
inline constexpr FieldId kMaxFieldId = 255 * 8;

// Rejects `std::string` temporaries in borrowing setters at compile time: the bytes of a
// temporary would be gone by the time the builder serializes them.
template <typename S>
concept TemporaryString = std::same_as<std::remove_cv_t<S>, std::string>;

// Storage class. It is part of a field's wire encoding and never changes for a field ID.
enum class Kind : uint8_t {
  S8 = 0,   // 8-byte inline slot: int64, uint64, double
  S4 = 1,   // 4-byte inline slot: int8..int32, uint8..uint32, float
  Bit = 2,  // value stored in a bitmap: bool
  Var = 3,  // end offset + payload bytes: strings, bytes, nested messages
};

template <typename T>
concept Scalar = (std::is_integral_v<T> &&
                  (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)) ||
                 std::same_as<T, float> || std::same_as<T, double>;

template <Scalar T>
inline constexpr Kind KindOf = std::same_as<T, bool> ? Kind::Bit
                               : sizeof(T) == 8      ? Kind::S8
                                                     : Kind::S4;

namespace detail {
template <size_t Size>
using Bits = std::conditional_t<
    Size == 1, uint8_t,
    std::conditional_t<Size == 2, uint16_t, std::conditional_t<Size == 4, uint32_t, uint64_t>>>;

template <Scalar T>
inline T Load(const uint8_t* ptr) noexcept {
  if constexpr (std::same_as<T, bool>) {
    return *ptr != 0;
  } else {
    Bits<sizeof(T)> bits;
    std::memcpy(&bits, ptr, sizeof(bits));
    if constexpr (std::endian::native == std::endian::big) bits = std::byteswap(bits);
    return std::bit_cast<T>(bits);
  }
}

template <Scalar T>
inline void Store(uint8_t* ptr, T value) noexcept {
  if constexpr (std::same_as<T, bool>) {
    *ptr = value ? 1 : 0;
  } else {
    auto bits = std::bit_cast<Bits<sizeof(T)>>(value);
    if constexpr (std::endian::native == std::endian::big) bits = std::byteswap(bits);
    std::memcpy(ptr, &bits, sizeof(bits));
  }
}

template <Scalar T>
inline uint64_t ToBits(T value) noexcept {
  if constexpr (std::same_as<T, bool>) {
    return value ? 1 : 0;
  } else {
    return static_cast<uint64_t>(std::bit_cast<Bits<sizeof(T)>>(value));
  }
}

template <Scalar T>
inline T FromBits(uint64_t bits) noexcept {
  if constexpr (std::same_as<T, bool>) {
    return bits != 0;
  } else {
    return std::bit_cast<T>(static_cast<Bits<sizeof(T)>>(bits));
  }
}

inline uint32_t LoadWidth(const uint8_t* ptr, uint32_t width) noexcept {
  switch (width) {
    case 1:
      return *ptr;
    case 2:
      return Load<uint16_t>(ptr);
    default:
      return Load<uint32_t>(ptr);
  }
}

inline void StoreWidth(uint8_t* ptr, uint32_t width, uint32_t value) noexcept {
  switch (width) {
    case 1:
      *ptr = static_cast<uint8_t>(value);
      break;
    case 2:
      Store<uint16_t>(ptr, static_cast<uint16_t>(value));
      break;
    default:
      Store<uint32_t>(ptr, value);
      break;
  }
}

constexpr uint8_t WidthCode(uint64_t max_value) noexcept {
  return max_value <= 0xff ? 0 : max_value <= 0xffff ? 1 : 2;
}

// Bitmap index of a field ID inside every class bitmap.
constexpr uint32_t BitOf(FieldId id) noexcept {
  return id - 1;
}

// Popcount of all bytes in a bitmap.
inline uint32_t PopcountBytes(const uint8_t* ptr, uint32_t len) noexcept {
  uint32_t result = 0;
  uint32_t i = 0;
  for (; i + 8 <= len; i += 8) result += std::popcount(Load<uint64_t>(ptr + i));
  for (; i < len; ++i) result += std::popcount(ptr[i]);
  return result;
}

// Number of set bits strictly below `bit`. Requires bit / 8 < len.
inline uint32_t RankBytes(const uint8_t* ptr, uint32_t bit) noexcept {
  const uint32_t byte = bit / 8;
  uint32_t result = PopcountBytes(ptr, byte);
  return result + std::popcount(static_cast<uint8_t>(ptr[byte] & ((1u << (bit % 8)) - 1)));
}
}  // namespace detail

// One enumerated field. `value` holds the slot bits (S4/S8, zero-extended) or 0/1 (Bit);
// `bytes` holds a borrowed payload slice (Var).
struct Field {
  FieldId id = 0;
  Kind kind = Kind::Var;
  uint64_t value = 0;
  std::span<const uint8_t> bytes;
};

class MessageView {
 public:
  // A default-constructed view is empty: every getter returns its default.
  MessageView() noexcept
      : _base(nullptr),
        _bits(nullptr),
        _s8(nullptr),
        _s4(nullptr),
        _ends(nullptr),
        _payload(nullptr),
        _w0{},
        _size(0),
        _payload_size(0),
        _n8(0),
        _n4(0),
        _nvar(0),
        _len{},
        _width(1),
        _header(kHeaderSize) {}
  // Leaves members uninitialized; only for Open/Init, which assigns every member on success.
  struct Uninitialized {};
  explicit MessageView(Uninitialized) noexcept {}

  // O(1) structural checks from the header alone: no bitmap popcounts. After a
  // successful Open every read is in bounds: slot and offset indices are checked against
  // the header's per-class counts. Call Validate() for full verification (including
  // that the counts match the bitmaps) of untrusted input.
  static std::optional<MessageView> Open(std::span<const uint8_t> bytes) noexcept {
    std::optional<MessageView> result(std::in_place, Uninitialized{});
    if (!result->Init(bytes)) result.reset();
    return result;
  }
  // Open for trusted storage: malformed bytes read as an empty message.
  static MessageView OpenOrEmpty(std::span<const uint8_t> bytes) noexcept {
    MessageView view{Uninitialized{}};
    return view.Init(bytes) ? view : MessageView{};
  }
  // In-place variant of Open; on failure the view is left unusable.
  bool Init(std::span<const uint8_t> bytes) noexcept;
  static bool Verify(std::span<const uint8_t> bytes) noexcept {
    auto view = Open(bytes);
    return view && view->Validate();
  }
  bool Validate() const noexcept;

  std::span<const uint8_t> Buffer() const noexcept { return {_base, _size}; }
  uint32_t FieldCount() const noexcept {
    return _n8 + _n4 + _nvar + detail::PopcountBytes(_bits, _len[kBit]);
  }
  uint32_t PayloadSize() const noexcept { return _payload_size; }

  // Presence with compile-time ID and storage class: one bit test, no class search.
  template <FieldId Id, Kind K>
  bool Has() const noexcept {
    static_assert(Id >= 1 && Id <= kMaxFieldId);
    constexpr uint32_t bit = detail::BitOf(Id);
    if constexpr (K == Kind::Bit) {
      return TestBool(bit, false);
    } else {
      return Test<bit>(static_cast<size_t>(K));
    }
  }

  // Compile-time field ID and type: bit test + popcount + one load.
  template <FieldId Id, Scalar T>
  T Get(T default_value = {}) const noexcept {
    static_assert(Id >= 1 && Id <= kMaxFieldId);
    constexpr uint32_t bit = detail::BitOf(Id);
    constexpr Kind kind = KindOf<T>;
    if constexpr (kind == Kind::Bit) {
      constexpr uint32_t byte = bit / 8;
      constexpr uint8_t mask = uint8_t(1u << (bit % 8));
      if (byte >= _len[kBit] || !(_bits[byte] & mask)) return default_value;
      return (_bits[_len[kBit] + byte] & mask) != 0;
    } else {
      constexpr size_t cls = static_cast<size_t>(kind);
      if (!Test<bit>(cls)) return default_value;
      const uint32_t index = Rank<bit>(cls);
      if (index >= Count(cls)) [[unlikely]]
        return default_value;
      return detail::Load<T>(kind == Kind::S8 ? _s8 + size_t(index) * 8 : _s4 + size_t(index) * 4);
    }
  }

  template <FieldId Id>
  std::optional<std::span<const uint8_t>> GetBytes() const noexcept {
    static_assert(Id >= 1 && Id <= kMaxFieldId);
    constexpr uint32_t bit = detail::BitOf(Id);
    if (!Test<bit>(kVar)) return std::nullopt;
    return VarAt(Rank<bit>(kVar));
  }

  template <FieldId Id>
  std::string_view GetString(std::string_view default_value = {}) const noexcept {
    return AsString(GetBytes<Id>(), default_value);
  }

  // Runtime field IDs.
  std::optional<Kind> KindOfField(FieldId id) const noexcept;
  bool Has(FieldId id) const noexcept { return KindOfField(id).has_value(); }

  template <Scalar T>
  T Get(FieldId id, T default_value = {}) const noexcept {
    if (id == 0 || id > kMaxFieldId) return default_value;
    const uint32_t bit = detail::BitOf(id);
    constexpr Kind kind = KindOf<T>;
    if constexpr (kind == Kind::Bit) {
      if (!TestBool(bit, false)) return default_value;
      return TestBool(bit, true);
    } else {
      constexpr size_t cls = static_cast<size_t>(kind);
      if (!TestRuntime(cls, bit)) return default_value;
      const uint32_t index = RankRuntime(cls, bit);
      if (index >= Count(cls)) return default_value;
      return detail::Load<T>(kind == Kind::S8 ? _s8 + size_t(index) * 8 : _s4 + size_t(index) * 4);
    }
  }

  std::optional<std::span<const uint8_t>> GetBytes(FieldId id) const noexcept {
    if (id == 0 || id > kMaxFieldId) return std::nullopt;
    const uint32_t bit = detail::BitOf(id);
    if (!TestRuntime(kVar, bit)) return std::nullopt;
    return VarAt(RankRuntime(kVar, bit));
  }

  std::string_view GetString(FieldId id, std::string_view default_value = {}) const noexcept {
    return AsString(GetBytes(id), default_value);
  }

  std::optional<Field> GetField(FieldId id) const noexcept;

  // Enumerates every present field, including unknown ones, in ascending ID order.
  // Returns false (after a partial enumeration) if the message is malformed.
  template <typename F>
  bool ForEach(F&& fn) const;

 private:
  friend class MutableView;
  friend Bytes Rebuild(const MessageView&, const MessagePatch&);
  // Wide counts, long bitmaps and tiny buffers; Init falls back to it.
  bool InitGeneric(std::span<const uint8_t> bytes) noexcept;

  static constexpr size_t kS8 = static_cast<size_t>(Kind::S8);
  static constexpr size_t kS4 = static_cast<size_t>(Kind::S4);
  static constexpr size_t kBit = static_cast<size_t>(Kind::Bit);
  static constexpr size_t kVar = static_cast<size_t>(Kind::Var);

  static std::string_view AsString(std::optional<std::span<const uint8_t>> raw,
                                   std::string_view default_value) noexcept {
    return raw ? std::string_view(reinterpret_cast<const char*>(raw->data()), raw->size())
               : default_value;
  }

  uint32_t Count(size_t cls) const noexcept { return cls == kS8 ? _n8 : cls == kS4 ? _n4 : _nvar; }
  // Presence bitmaps of the slot/offset classes (S8, S4, Var).
  const uint8_t* BitmapPtr(size_t cls) const noexcept {
    return cls == kS8   ? _base + _header
           : cls == kS4 ? _base + _header + _len[kS8]
                        : _bits + 2 * size_t(_len[kBit]);
  }

  template <uint32_t Bit>
  bool Test(size_t cls) const noexcept {
    if constexpr (Bit < 64) {
      return (_w0[cls] >> Bit) & 1;
    } else {
      return TestRuntime(cls, Bit);
    }
  }
  template <uint32_t Bit>
  uint32_t Rank(size_t cls) const noexcept {
    if constexpr (Bit < 64) {
      return static_cast<uint32_t>(std::popcount(_w0[cls] & ((uint64_t{1} << Bit) - 1)));
    } else {
      return detail::RankBytes(BitmapPtr(cls), Bit);
    }
  }
  bool TestRuntime(size_t cls, uint32_t bit) const noexcept {
    if (bit < 64) return (_w0[cls] >> bit) & 1;
    if (bit / 8 >= _len[cls]) return false;
    return (BitmapPtr(cls)[bit / 8] >> (bit % 8)) & 1;
  }
  uint32_t RankRuntime(size_t cls, uint32_t bit) const noexcept {
    if (bit < 64)
      return static_cast<uint32_t>(std::popcount(_w0[cls] & ((uint64_t{1} << bit) - 1)));
    return detail::RankBytes(BitmapPtr(cls), bit);
  }
  // Bit class: presence (values == false) or value (values == true) bitmap.
  bool TestBool(uint32_t bit, bool values) const noexcept {
    if (bit / 8 >= _len[kBit]) return false;
    return (_bits[(values ? _len[kBit] : 0) + bit / 8] >> (bit % 8)) & 1;
  }

  uint32_t EndAt(uint32_t index) const noexcept {
    return detail::LoadWidth(_ends + size_t(index) * _width, _width);
  }
  std::optional<std::span<const uint8_t>> VarAt(uint32_t index) const noexcept {
    if (index >= _nvar) [[unlikely]]
      return std::nullopt;
    const uint32_t start = index ? EndAt(index - 1) : 0;
    const uint32_t end = EndAt(index);
    if (start > end || end > _payload_size) return std::nullopt;
    return std::span<const uint8_t>(_payload + start, end - start);
  }
  uint64_t SlotBits(Kind kind, uint32_t index) const noexcept {
    return kind == Kind::S8 ? detail::Load<uint64_t>(_s8 + size_t(index) * 8)
                            : detail::Load<uint32_t>(_s4 + size_t(index) * 4);
  }

  const uint8_t* _base;
  const uint8_t* _bits;  // Bit presence bitmap, followed by the Bit value bitmap
  const uint8_t* _s8;
  const uint8_t* _s4;
  const uint8_t* _ends;
  const uint8_t* _payload;
  // First 64 bits of the S8, S4 and Var bitmaps (index = Kind; the Bit entry is unused).
  std::array<uint64_t, 4> _w0;
  uint32_t _size;
  uint32_t _payload_size;
  uint32_t _n8, _n4, _nvar;  // from the header
  std::array<uint8_t, 4> _len;
  uint8_t _width;
  uint8_t _header;
};

// A view over mutable bytes: same-size in-place updates without a rebuild.
class MutableView {
 public:
  static std::optional<MutableView> Open(std::span<uint8_t> bytes) noexcept {
    std::optional<MutableView> result(std::in_place, MessageView::Uninitialized{}, bytes.data());
    if (!result->_view.Init(bytes)) result.reset();
    return result;
  }
  const MessageView& View() const noexcept { return _view; }

  // Overwrites an existing field of the same storage class. Returns false if absent.
  template <FieldId Id, Scalar T>
  bool Set(T value) noexcept {
    static_assert(Id >= 1 && Id <= kMaxFieldId);
    constexpr uint32_t bit = detail::BitOf(Id);
    constexpr Kind kind = KindOf<T>;
    if constexpr (kind == Kind::Bit) {
      constexpr uint32_t byte = bit / 8;
      constexpr uint8_t mask = uint8_t(1u << (bit % 8));
      const uint32_t len = _view._len[MessageView::kBit];
      if (byte >= len || !(_view._bits[byte] & mask)) return false;
      uint8_t* target = Mutable(_view._bits + len + byte);
      *target = value ? uint8_t(*target | mask) : uint8_t(*target & ~mask);
    } else {
      constexpr size_t cls = static_cast<size_t>(kind);
      if (!_view.Test<bit>(cls)) return false;
      const uint32_t index = _view.Rank<bit>(cls);
      if (index >= _view.Count(cls)) [[unlikely]]
        return false;
      const uint8_t* slot =
          kind == Kind::S8 ? _view._s8 + size_t(index) * 8 : _view._s4 + size_t(index) * 4;
      if constexpr (kind == Kind::S4 && sizeof(T) < 4) {
        detail::Store<uint32_t>(Mutable(slot), static_cast<uint32_t>(detail::ToBits(value)));
      } else {
        detail::Store<T>(Mutable(slot), value);
      }
    }
    return true;
  }

  template <Scalar T>
  bool Set(FieldId id, T value) noexcept {
    return SetBits(id, KindOf<T>, detail::ToBits(value));
  }
  // Overwrites an existing S4/S8/Bit field with raw slot bits (zero-extended for S4).
  bool SetBits(FieldId id, Kind kind, uint64_t bits) noexcept;
  // Replaces an existing Var field with a payload of exactly the same length.
  bool SetBytes(FieldId id, std::span<const uint8_t> value) noexcept;
  bool SetString(FieldId id, std::string_view value) noexcept {
    return SetBytes(id, {reinterpret_cast<const uint8_t*>(value.data()), value.size()});
  }

  // Use Open(); public only for in-place construction inside std::optional.
  MutableView(MessageView::Uninitialized tag, uint8_t* base) noexcept : _view(tag), _base(base) {}

 private:
  uint8_t* Mutable(const uint8_t* ptr) const noexcept { return _base + (ptr - _view._base); }

  MessageView _view;
  uint8_t* _base;
};

namespace detail {
// Builder/patch entry. Var payloads are borrowed, never copied, until serialization.
struct Entry {
  FieldId id;
  Kind kind;
  bool remove;
  uint32_t len;
  union {
    uint64_t value;
    const uint8_t* data;
  };
};

// Inline storage for N entries; spills to the heap only beyond N.
template <size_t N>
class EntryVector {
 public:
  EntryVector() = default;
  EntryVector(const EntryVector& other) { *this = other; }
  EntryVector& operator=(const EntryVector& other) {
    if (this != &other) {
      clear();
      for (const auto& e : other) push_back(e);
    }
    return *this;
  }
  Entry* data() noexcept { return _heap.empty() ? _inline.data() : _heap.data(); }
  const Entry* data() const noexcept { return _heap.empty() ? _inline.data() : _heap.data(); }
  size_t size() const noexcept { return _size; }
  bool empty() const noexcept { return _size == 0; }
  Entry* begin() noexcept { return data(); }
  Entry* end() noexcept { return data() + _size; }
  const Entry* begin() const noexcept { return data(); }
  const Entry* end() const noexcept { return data() + _size; }
  Entry& operator[](size_t i) noexcept { return data()[i]; }
  const Entry& operator[](size_t i) const noexcept { return data()[i]; }
  Entry& back() noexcept { return data()[_size - 1]; }
  void clear() noexcept {
    _size = 0;
    _heap.clear();
  }
  void push_back(const Entry& e) { emplace_back() = e; }
  // Returns an uninitialized slot; the caller writes every member in place.
  Entry& emplace_back() {
    if (_size < N && _heap.empty()) [[likely]]
      return _inline[_size++];
    return Spill();
  }
  Entry& Spill() {
    if (_heap.empty()) {
      _heap.reserve(N * 2);
      _heap.assign(_inline.begin(), _inline.begin() + _size);
    }
    ++_size;
    return _heap.emplace_back();
  }
  void erase_at(size_t i) noexcept {
    Entry* d = data();
    std::memmove(static_cast<void*>(d + i), d + i + 1, (_size - i - 1) * sizeof(Entry));
    --_size;
    if (!_heap.empty()) _heap.pop_back();
  }

 private:
  std::array<Entry, N> _inline;
  std::vector<Entry> _heap;
  size_t _size = 0;
};
}  // namespace detail

class MessageBuilder {
 public:
  MessageBuilder() = default;
  // Starts from every field of `source`, known or unknown. Var payloads stay borrowed from
  // the source bytes. Throws std::invalid_argument if the source is malformed.
  explicit MessageBuilder(const MessageView& source);

  template <Scalar T>
  void AddField(FieldId id, T value) {
    detail::Entry& e = Slot(id);
    e.kind = KindOf<T>;
    e.value = detail::ToBits(value);
  }
  // The bytes are borrowed: they must stay alive until Build/SerializeTo returns.
  void AddBytesField(FieldId id, std::span<const uint8_t> value) {
    const uint32_t len = CheckLen(value.size());
    detail::Entry& e = Slot(id);
    e.kind = Kind::Var;
    e.len = len;
    e.data = value.data();
  }
  void AddStringField(FieldId id, std::string_view value) {
    AddBytesField(id, {reinterpret_cast<const uint8_t*>(value.data()), value.size()});
  }
  // Adds an enumerated field (known or unknown), preserving its storage class and bytes.
  void AddRaw(const Field& field);
  void ClearField(FieldId id);
  void Reset() noexcept {
    _entries.clear();
    _last_id = 0;
    _sorted = true;
    _finalized = false;
  }

  size_t SerializedSize();
  // Writes exactly SerializedSize() bytes; `out` must be at least that large.
  size_t SerializeTo(std::span<uint8_t> out);
  Bytes Build() {
    Bytes result(SerializedSize());
    SerializeTo(result);
    return result;
  }
  void BuildTo(Bytes& result) {
    result.resize(SerializedSize());
    SerializeTo(result);
  }

 private:
  static FieldId CheckId(FieldId id) {
    if (id == 0 || id > kMaxFieldId) throw std::invalid_argument("message field ID out of range");
    return id;
  }
  static uint32_t CheckLen(size_t len) {
    if (len > std::numeric_limits<uint32_t>::max())
      throw std::length_error("message field too large");
    return static_cast<uint32_t>(len);
  }
  detail::Entry& Slot(FieldId id) {
    if (id == 0 || id > kMaxFieldId) [[unlikely]]
      ThrowBadId();
    if (id <= _last_id) _sorted = false;
    _last_id = id;
    _finalized = false;
    detail::Entry& e = _entries.emplace_back();
    e.id = id;
    e.remove = false;
    e.len = 0;
    return e;
  }
  [[noreturn]] static void ThrowBadId();
  void Append(const detail::Entry& e) { Slot(e.id) = e; }
  void Finalize();

  detail::EntryVector<64> _entries;
  FieldId _last_id = 0;
  bool _sorted = true;
  bool _finalized = false;
  // Layout computed by Finalize.
  std::array<uint8_t, 4> _len{};
  uint32_t _n8 = 0, _n4 = 0, _nvar = 0;
  uint32_t _payload = 0;
  uint8_t _width_code = 0;
  uint8_t _header = kHeaderSize;
  uint32_t _total = 0;
};

// A set of changes applied together. `SetBytes`/`SetString` borrow the value until the patch
// is applied; `CopyBytes`/`CopyString` own it. A patch is move-only.
class MessagePatch {
 public:
  template <Scalar T>
  void Set(FieldId id, T value) {
    detail::Entry e{CheckId(id), KindOf<T>, false, 0, {}};
    e.value = detail::ToBits(value);
    Put(e);
  }
  void SetBytes(FieldId id, std::span<const uint8_t> value) {
    if (value.size() > std::numeric_limits<uint32_t>::max())
      throw std::length_error("message field too large");
    detail::Entry e{CheckId(id), Kind::Var, false, static_cast<uint32_t>(value.size()), {}};
    e.data = value.data();
    Put(e);
  }
  void SetString(FieldId id, std::string_view value) {
    SetBytes(id, {reinterpret_cast<const uint8_t*>(value.data()), value.size()});
  }
  void CopyBytes(FieldId id, std::span<const uint8_t> value) {
    auto owned = std::make_unique_for_overwrite<uint8_t[]>(value.size());
    if (!value.empty()) std::memcpy(owned.get(), value.data(), value.size());
    SetBytes(id, {owned.get(), value.size()});
    _owned.push_back(std::move(owned));
  }
  void CopyString(FieldId id, std::string_view value) {
    CopyBytes(id, {reinterpret_cast<const uint8_t*>(value.data()), value.size()});
  }
  void Remove(FieldId id) {
    detail::Entry e{CheckId(id), Kind::Var, true, 0, {}};
    e.value = 0;
    Put(e);
  }

  // True if the patch sets or removes the field.
  bool Modifies(FieldId id) const noexcept { return Find(id) != nullptr; }
  // The scalar the patch writes to the field; nullopt if it does not set one of this class.
  template <Scalar T>
  std::optional<T> ScalarValue(FieldId id) const noexcept {
    const detail::Entry* e = Find(id);
    if (e == nullptr || e->remove || e->kind != KindOf<T>) return std::nullopt;
    return detail::FromBits<T>(e->value);
  }
  const detail::EntryVector<8>& Changes() const noexcept { return _changes; }

 private:
  static FieldId CheckId(FieldId id) {
    if (id == 0 || id > kMaxFieldId) throw std::invalid_argument("message field ID out of range");
    return id;
  }
  const detail::Entry* Find(FieldId id) const noexcept {
    for (const auto& e : _changes) {
      if (e.id == id) return &e;
    }
    return nullptr;
  }
  void Put(const detail::Entry& e) {
    for (auto& existing : _changes) {
      if (existing.id == e.id) {
        existing = e;
        return;
      }
    }
    _changes.push_back(e);
  }
  detail::EntryVector<8> _changes;
  std::vector<std::unique_ptr<uint8_t[]>> _owned;
};

// All-or-nothing: preflights every change, then overwrites payload/slot bytes only.
// False (bytes untouched) if a field is absent, changes storage class or length, or is removed.
bool ApplyPatchInplace(const MessagePatch& patch, std::span<uint8_t> bytes) noexcept;
// Returns a new message with the patch applied; unknown fields are preserved byte-for-byte.
// Throws std::invalid_argument if the source is malformed.
Bytes Rebuild(const MessageView& source, const MessagePatch& patch);
// In place when possible, otherwise rebuilds and swaps.
void ApplyPatch(const MessagePatch& patch, Bytes& bytes);

// ---------------------------------------------------------------------------------------
// Inline definitions.

namespace detail {
// Mask of the first min(len, 8) bytes of a bitmap word.
inline constexpr std::array<uint64_t, 9> kBitmapMask = {
    0,           0xff, 0xffff, 0xffffff, 0xffffffff, 0xffffffffff, 0xffffffffffff, 0xffffffffffffff,
    ~uint64_t{0}};
}  // namespace detail

inline bool MessageView::Init(std::span<const uint8_t> bytes) noexcept {
  const size_t size = bytes.size();
  const uint8_t* p = bytes.data();
  // Fast path: one-byte counts and at least 8 readable bytes after the Var bitmap start, so
  // the first word (field IDs 1..64) of each slot/offset bitmap is one masked load. Longer
  // bitmaps are fine: IDs above 64 are read byte-wise. Section extents come from the header
  // counts, so there is no popcount here.
  if (size >= kHeaderSize + 8 && size <= std::numeric_limits<uint32_t>::max() &&
      p[0] == kMessageVersion && p[1] <= 2) [[likely]] {
    const uint32_t l8 = p[2], l4 = p[3], lb = p[4], lv = p[5];
    const uint32_t ob = l8 + l4, ovar = ob + 2 * lb, oslots = ovar + lv;
    if (kHeaderSize + ovar + 8 <= size) [[likely]] {
      const uint8_t* bm = p + kHeaderSize;
      const uint32_t n8 = p[6], n4 = p[7], nvar = p[8];
      const uint32_t width = 1u << p[1];
      const size_t fixed =
          kHeaderSize + oslots + size_t(n8) * 8 + size_t(n4) * 4 + size_t(nvar) * width;
      if (fixed > size) [[unlikely]]
        return false;
      _w0[kS8] = detail::Load<uint64_t>(bm) & detail::kBitmapMask[std::min(l8, 8u)];
      _w0[kS4] = detail::Load<uint64_t>(bm + l8) & detail::kBitmapMask[std::min(l4, 8u)];
      _w0[kVar] = detail::Load<uint64_t>(bm + ovar) & detail::kBitmapMask[std::min(lv, 8u)];
      _w0[kBit] = 0;
      _n8 = n8;
      _n4 = n4;
      _nvar = nvar;
      _bits = bm + ob;
      _size = static_cast<uint32_t>(size);
      _width = static_cast<uint8_t>(width);
      _header = kHeaderSize;
      std::memcpy(_len.data(), p + 2, 4);
      _s8 = bm + oslots;
      _s4 = _s8 + size_t(n8) * 8;
      _ends = _s4 + size_t(n4) * 4;
      _payload = p + fixed;
      _payload_size = static_cast<uint32_t>(size - fixed);
      // The last end offset frames the message: detects truncation and trailing bytes.
      const bool ok = nvar ? EndAt(nvar - 1) == _payload_size : _payload_size == 0;
      _base = ok ? p : nullptr;
      return ok;
    }
  }
  return InitGeneric(bytes);
}

namespace detail {
// Zero-extended first (up to) 8 bytes of a bitmap, without reading past it.
inline uint64_t FirstWord(const uint8_t* ptr, uint32_t len) noexcept {
  uint64_t w = 0;
  for (uint32_t b = 0; b < len && b < 8; ++b) w |= uint64_t(ptr[b]) << (8 * b);
  return w;
}
}  // namespace detail

template <typename F>
bool MessageView::ForEach(F&& fn) const {
  uint32_t i8 = 0, i4 = 0, ivar = 0, previous_end = 0;
  // Emits the fields of one bitmap chunk; `base` is the bit index of the chunk's bit 0.
  auto emit = [&](uint64_t s8, uint64_t s4, uint64_t bit, uint64_t val, uint64_t var,
                  uint32_t base) {
    if ((s8 & s4) | (s8 & bit) | (s8 & var) | (s4 & bit) | (s4 & var) | (bit & var) | (val & ~bit))
      return false;
    uint64_t any = s8 | s4 | bit | var;
    while (any) {
      const uint32_t k = static_cast<uint32_t>(std::countr_zero(any));
      any &= any - 1;
      const uint64_t mask = uint64_t{1} << k;
      Field f;
      f.id = base + k + 1;
      if (s8 & mask) {
        if (i8 >= _n8) return false;
        f.kind = Kind::S8;
        f.value = SlotBits(Kind::S8, i8++);
      } else if (s4 & mask) {
        if (i4 >= _n4) return false;
        f.kind = Kind::S4;
        f.value = SlotBits(Kind::S4, i4++);
      } else if (bit & mask) {
        f.kind = Kind::Bit;
        f.value = (val & mask) ? 1 : 0;
      } else {
        if (ivar >= _nvar) return false;
        f.kind = Kind::Var;
        const uint32_t end_offset = EndAt(ivar++);
        if (end_offset < previous_end || end_offset > _payload_size) return false;
        f.bytes = {_payload + previous_end, end_offset - previous_end};
        previous_end = end_offset;
      }
      fn(static_cast<const Field&>(f));
    }
    return true;
  };
  const uint32_t lb = _len[kBit];
  bool ok;
  if (_len[kS8] <= 8 && _len[kS4] <= 8 && lb <= 8 && _len[kVar] <= 8) [[likely]] {
    ok = emit(_w0[kS8], _w0[kS4], detail::FirstWord(_bits, lb), detail::FirstWord(_bits + lb, lb),
              _w0[kVar], 0);
  } else {
    const uint8_t* s8 = BitmapPtr(kS8);
    const uint8_t* s4 = BitmapPtr(kS4);
    const uint8_t* var = BitmapPtr(kVar);
    const uint32_t max_len = std::max({_len[0], _len[1], _len[2], _len[3]});
    auto byte_of = [](const uint8_t* bm, uint32_t len, uint32_t b) -> uint64_t {
      return b < len ? bm[b] : 0;
    };
    ok = true;
    for (uint32_t b = 0; ok && b < max_len; ++b) {
      ok = emit(byte_of(s8, _len[kS8], b), byte_of(s4, _len[kS4], b), byte_of(_bits, lb, b),
                byte_of(_bits + lb, lb, b), byte_of(var, _len[kVar], b), b * 8);
    }
  }
  // Every slot and offset announced by the header must belong to a field.
  return ok && i8 == _n8 && i4 == _n4 && ivar == _nvar;
}

}  // namespace gendb
