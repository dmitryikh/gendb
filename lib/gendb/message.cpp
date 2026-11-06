#include "gendb/message.h"

#include "gendb/list.h"
#include "gendb/log_format.h"

namespace gendb {

bool MessageView::InitGeneric(std::span<const uint8_t> bytes) noexcept {
  _base = nullptr;
  const size_t size = bytes.size();
  if (size < kHeaderSize || size > std::numeric_limits<uint32_t>::max()) return false;
  const uint8_t* p = bytes.data();
  if (p[0] != kMessageVersion || (p[1] & ~uint8_t{kWideCountsFlag | 3}) != 0 || (p[1] & 3) == 3)
    return false;
  const bool wide = (p[1] & kWideCountsFlag) != 0;
  const size_t header = wide ? kWideHeaderSize : kHeaderSize;
  if (size < header) return false;
  if (wide) {
    _n8 = detail::Load<uint16_t>(p + 6);
    _n4 = detail::Load<uint16_t>(p + 8);
    _nvar = detail::Load<uint16_t>(p + 10);
  } else {
    _n8 = p[6];
    _n4 = p[7];
    _nvar = p[8];
  }
  const uint32_t l8 = p[2], l4 = p[3], lb = p[4], lv = p[5];
  const uint8_t* bm8 = p + header;
  const uint8_t* bm4 = bm8 + l8;
  const uint8_t* bits = bm4 + l4;
  const uint8_t* bmvar = bits + 2 * size_t(lb);
  const uint8_t* slots = bmvar + lv;
  const uint8_t* end = p + size;
  if (slots > end) return false;
  _width = static_cast<uint8_t>(1u << (p[1] & 3));
  _header = static_cast<uint8_t>(header);
  _len = {p[2], p[3], p[4], p[5]};
  _w0 = {detail::FirstWord(bm8, l8), detail::FirstWord(bm4, l4), 0, detail::FirstWord(bmvar, lv)};
  const uint64_t fixed = uint64_t(_n8) * 8 + uint64_t(_n4) * 4 + uint64_t(_nvar) * _width;
  if (fixed > uint64_t(end - slots)) return false;
  _bits = bits;
  _s8 = slots;
  _s4 = slots + size_t(_n8) * 8;
  _ends = _s4 + size_t(_n4) * 4;
  _payload = slots + fixed;
  _payload_size = static_cast<uint32_t>(end - _payload);
  if (_nvar ? EndAt(_nvar - 1) != _payload_size : _payload_size != 0) return false;
  _base = p;
  _size = static_cast<uint32_t>(size);
  return true;
}

std::optional<Kind> MessageView::KindOfField(FieldId id) const noexcept {
  if (id == 0 || id > kMaxFieldId) return std::nullopt;
  const uint32_t bit = detail::BitOf(id);
  if (TestRuntime(kS8, bit)) return Kind::S8;
  if (TestRuntime(kS4, bit)) return Kind::S4;
  if (TestBool(bit, false)) return Kind::Bit;
  if (TestRuntime(kVar, bit)) return Kind::Var;
  return std::nullopt;
}

std::optional<Field> MessageView::GetField(FieldId id) const noexcept {
  auto kind = KindOfField(id);
  if (!kind) return std::nullopt;
  const uint32_t bit = detail::BitOf(id);
  Field f{id, *kind, 0, {}};
  switch (*kind) {
    case Kind::S8:
    case Kind::S4: {
      const size_t cls = static_cast<size_t>(*kind);
      const uint32_t index = RankRuntime(cls, bit);
      if (index >= Count(cls)) return std::nullopt;
      f.value = SlotBits(*kind, index);
      break;
    }
    case Kind::Bit:
      f.value = TestBool(bit, true) ? 1 : 0;
      break;
    case Kind::Var: {
      auto bytes = VarAt(RankRuntime(kVar, bit));
      if (!bytes) return std::nullopt;
      f.bytes = *bytes;
      break;
    }
  }
  return f;
}

void MessageBuilder::ThrowBadId() {
  throw std::invalid_argument("message field ID out of range");
}

bool MessageView::Validate() const noexcept {
  if (_base == nullptr) return false;
  // Canonical header: two-byte counts only when a count needs them.
  const bool wide = _header == kWideHeaderSize;
  if (wide != (std::max({_n8, _n4, _nvar}) > 0xff)) return false;
  // Canonical offset width.
  if ((1u << detail::WidthCode(_payload_size)) != _width) return false;
  // Canonical bitmap lengths: a non-empty bitmap ends in a non-zero byte.
  for (size_t cls : {kS8, kS4, kVar}) {
    if (_len[cls] != 0 && BitmapPtr(cls)[_len[cls] - 1] == 0) return false;
  }
  if (_len[kBit] != 0 && _bits[_len[kBit] - 1] == 0) return false;
  if (_len[kS8] <= 8 && _len[kS4] <= 8 && _len[kBit] <= 8 && _len[kVar] <= 8) {
    // Fast path (field IDs <= 64): whole bitmaps fit in one word each.
    const uint64_t s8 = _w0[kS8], s4 = _w0[kS4], var = _w0[kVar];
    const uint64_t bit = detail::FirstWord(_bits, _len[kBit]);
    const uint64_t val = detail::FirstWord(_bits + _len[kBit], _len[kBit]);
    if ((s8 & s4) | (s8 & bit) | (s8 & var) | (s4 & bit) | (s4 & var) | (bit & var) | (val & ~bit))
      return false;
    // The header counts must match the bitmaps.
    if (uint32_t(std::popcount(s8)) != _n8 || uint32_t(std::popcount(s4)) != _n4 ||
        uint32_t(std::popcount(var)) != _nvar)
      return false;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < _nvar; ++i) {
      const uint32_t end = EndAt(i);
      if (end < previous) return false;
      previous = end;
    }
    return true;  // the last end equals the payload size (checked by Open)
  }
  // Disjoint classes, Bit values within presence, counts, monotonic in-bounds offsets.
  return ForEach([](const Field&) {});
}

bool MutableView::SetBits(FieldId id, Kind kind, uint64_t bits) noexcept {
  if (id == 0 || id > kMaxFieldId || kind == Kind::Var) return false;
  const uint32_t bit = detail::BitOf(id);
  if (kind == Kind::Bit) {
    if (!_view.TestBool(bit, false)) return false;
    uint8_t* target = Mutable(_view._bits + _view._len[MessageView::kBit] + bit / 8);
    const uint8_t mask = uint8_t(1u << (bit % 8));
    *target = bits ? uint8_t(*target | mask) : uint8_t(*target & ~mask);
    return true;
  }
  const size_t cls = static_cast<size_t>(kind);
  if (!_view.TestRuntime(cls, bit)) return false;
  const uint32_t index = _view.RankRuntime(cls, bit);
  if (index >= _view.Count(cls)) return false;
  if (kind == Kind::S8)
    detail::Store<uint64_t>(Mutable(_view._s8 + size_t(index) * 8), bits);
  else
    detail::Store<uint32_t>(Mutable(_view._s4 + size_t(index) * 4), static_cast<uint32_t>(bits));
  return true;
}

bool MutableView::SetBytes(FieldId id, std::span<const uint8_t> value) noexcept {
  auto current = _view.GetBytes(id);
  if (!current || current->size() != value.size()) return false;
  if (!value.empty()) std::memmove(Mutable(current->data()), value.data(), value.size());
  return true;
}

MessageBuilder::MessageBuilder(const MessageView& source) {
  if (!source.ForEach([this](const Field& f) { AddRaw(f); }))
    throw std::invalid_argument("invalid source message");
}

void MessageBuilder::AddRaw(const Field& field) {
  if (field.kind == Kind::Var) {
    AddBytesField(field.id, field.bytes);
    return;
  }
  detail::Entry e{CheckId(field.id), field.kind, false, 0, {}};
  e.value = field.kind == Kind::Bit  ? (field.value != 0)
            : field.kind == Kind::S4 ? (field.value & 0xffffffffu)
                                     : field.value;
  Append(e);
}

// One repeated field as it is being added to.
struct MessageBuilder::OwnedList {
  FieldId id;
  // Of fixed-size elements, in `packed`, whose bytes are what the field holds. Zero for strings or
  // messages, in `var`, for which `encoded` is what the field holds, made once it is needed.
  uint32_t element_size;
  Bytes packed;
  ListBuilder var;
  Bytes encoded;
};

struct MessageBuilder::Owned {
  OwnedList* Find(FieldId id) noexcept {
    for (OwnedList& list : lists) {
      if (list.id == id) return &list;
    }
    return nullptr;
  }

  std::vector<OwnedList> lists;
  // Each stays where it is when more are added, as entries point into it.
  std::vector<Bytes> copies;
};

void MessageBuilder::OwnedDeleter::operator()(Owned* owned) const noexcept {
  delete owned;
}

MessageBuilder::Owned& MessageBuilder::EnsureOwned() {
  if (_owned == nullptr) _owned.reset(new Owned());
  return *_owned;
}

void MessageBuilder::ClearField(FieldId id) {
  for (size_t i = _entries.size(); i-- > 0;) {
    if (_entries[i].id == id) _entries.erase_at(i);
  }
  if (_owned != nullptr) {
    std::erase_if(_owned->lists, [id](const OwnedList& list) { return list.id == id; });
  }
  _finalized = false;
}

namespace {
// What a view holds as a message of its own. A view of nothing, as of bytes that were malformed,
// reads as an empty message, so that is what it is stored as.
std::span<const uint8_t> MessageBytes(const MessageView& message) noexcept {
  static constexpr std::array<uint8_t, kHeaderSize> kEmpty = {kMessageVersion};
  return message.Buffer().empty() ? std::span<const uint8_t>(kEmpty) : message.Buffer();
}
}  // namespace

void MessageBuilder::CopyBytesField(FieldId id, std::span<const uint8_t> value) {
  CheckId(id);
  // First, as `value` may be from what this clears.
  Bytes& copy = EnsureOwned().copies.emplace_back(value.begin(), value.end());
  ClearField(id);
  AddBytesField(id, copy);
}

void MessageBuilder::AddMessageField(FieldId id, const MessageView& message) {
  CopyBytesField(id, MessageBytes(message));
}

void MessageBuilder::AddMessageField(FieldId id, MessageBuilder& message) {
  CheckId(id);
  Bytes& copy = EnsureOwned().copies.emplace_back(message.SerializedSize());
  message.SerializeTo(copy);
  ClearField(id);
  AddBytesField(id, copy);
}

namespace {
// The last value given for a field, which a list of it goes on from.
std::optional<std::span<const uint8_t>> PresentBytes(const detail::EntryVector<64>& entries,
                                                     FieldId id) {
  for (size_t i = entries.size(); i-- > 0;) {
    const detail::Entry& e = entries[i];
    if (e.id != id) continue;
    if (e.kind != Kind::Var) throw std::invalid_argument("not a repeated field");
    return std::span<const uint8_t>(e.len ? e.data : nullptr, e.len);
  }
  return std::nullopt;
}
}  // namespace

void MessageBuilder::AppendPacked(FieldId id, const void* data, uint32_t element_size,
                                  size_t size) {
  CheckId(id);
  Owned& owned = EnsureOwned();
  OwnedList* list = owned.Find(id);
  if (list == nullptr) {
    OwnedList created{id, element_size, {}, {}, {}};
    if (const auto present = PresentBytes(_entries, id)) {
      if (present->size() % element_size != 0) {
        throw std::invalid_argument("not a list of this type");
      }
      created.packed.assign(present->begin(), present->end());
    }
    list = &owned.lists.emplace_back(std::move(created));
  } else if (list->element_size != element_size) {
    throw std::invalid_argument("not a list of this type");
  }
  if (list->packed.size() + size > std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("message field too large");
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (size != 0) list->packed.insert(list->packed.end(), bytes, bytes + size);
  _finalized = false;
}

MessageBuilder::OwnedList& MessageBuilder::VarListOf(FieldId id) {
  CheckId(id);
  Owned& owned = EnsureOwned();
  _finalized = false;
  if (OwnedList* list = owned.Find(id)) {
    if (list->element_size != 0) throw std::invalid_argument("not a list of this type");
    return *list;
  }
  OwnedList created{id, 0, {}, {}, {}};
  if (const auto present = PresentBytes(_entries, id)) {
    if (!VarList::Verify(*present)) throw std::invalid_argument("not a list of this type");
    const VarList existing(*present);
    for (size_t i = 0; i < existing.size(); ++i) created.var.AddBytes(existing[i]);
  }
  return owned.lists.emplace_back(std::move(created));
}

void MessageBuilder::SetEmptyList(FieldId id) {
  CheckId(id);
  ClearField(id);
  VarListOf(id);
}

void MessageBuilder::AddRepeatedBytes(FieldId id, std::span<const uint8_t> value) {
  VarListOf(id).var.AddBytes(value);
}

void MessageBuilder::AddRepeatedMessage(FieldId id, MessageBuilder& message) {
  VarListOf(id).var.AddMessage(message);
}

void MessageBuilder::AddRepeatedMessage(FieldId id, const MessageView& message) {
  AddRepeatedBytes(id, MessageBytes(message));
}

void MessageBuilder::Finalize() {
  if (_owned != nullptr) {
    // Each list's current bytes. Entries from an earlier Finalize may point at what has moved
    // since, but later entries win below, without those being read.
    for (OwnedList& list : _owned->lists) {
      if (list.element_size == 0) list.encoded = list.var.Build();
      AddBytesField(list.id, list.element_size == 0 ? list.encoded : list.packed);
    }
  }
  if (!_sorted) {
    // Stable insertion sort: entries are few and usually almost sorted.
    detail::Entry* e = _entries.data();
    const size_t n = _entries.size();
    for (size_t i = 1; i < n; ++i) {
      detail::Entry x = e[i];
      size_t j = i;
      for (; j > 0 && e[j - 1].id > x.id; --j) e[j] = e[j - 1];
      e[j] = x;
    }
    // A later AddField for the same ID replaces the earlier one.
    size_t out = 0;
    for (size_t i = 0; i < n; ++i) {
      if (i + 1 < n && e[i + 1].id == e[i].id) continue;
      e[out++] = e[i];
    }
    while (_entries.size() > out) _entries.erase_at(_entries.size() - 1);
    _sorted = true;
  }
  std::array<uint32_t, 4> max_id{};
  _n8 = _n4 = _nvar = 0;
  uint64_t payload = 0;
  for (const auto& e : _entries) {
    max_id[static_cast<size_t>(e.kind)] = e.id;
    switch (e.kind) {
      case Kind::S8:
        ++_n8;
        break;
      case Kind::S4:
        ++_n4;
        break;
      case Kind::Bit:
        break;
      case Kind::Var:
        ++_nvar;
        payload += e.len;
        break;
    }
  }
  if (payload > std::numeric_limits<uint32_t>::max()) throw std::length_error("message too large");
  for (size_t i = 0; i < 4; ++i) _len[i] = static_cast<uint8_t>((max_id[i] + 7) / 8);
  _payload = static_cast<uint32_t>(payload);
  _width_code = detail::WidthCode(payload);
  _header =
      static_cast<uint8_t>(std::max({_n8, _n4, _nvar}) > 0xff ? kWideHeaderSize : kHeaderSize);
  const uint64_t total = _header + size_t(_len[0]) + _len[1] + 2 * size_t(_len[2]) + _len[3] +
                         uint64_t(_n8) * 8 + uint64_t(_n4) * 4 +
                         uint64_t(_nvar) * (1u << _width_code) + payload;
  if (total > std::numeric_limits<uint32_t>::max()) throw std::length_error("message too large");
  _total = static_cast<uint32_t>(total);
  _finalized = true;
}

size_t MessageBuilder::SerializedSize() {
  if (!_finalized) Finalize();
  return _total;
}

size_t MessageBuilder::SerializeTo(std::span<uint8_t> out) {
  if (!_finalized) Finalize();
  if (out.size() < _total) throw std::length_error("message output buffer too small");
  uint8_t* p = out.data();
  p[0] = kMessageVersion;
  p[1] = static_cast<uint8_t>(_width_code | (_header == kWideHeaderSize ? kWideCountsFlag : 0));
  p[2] = _len[0];
  p[3] = _len[1];
  p[4] = _len[2];
  p[5] = _len[3];
  if (_header == kWideHeaderSize) {
    detail::Store<uint16_t>(p + 6, static_cast<uint16_t>(_n8));
    detail::Store<uint16_t>(p + 8, static_cast<uint16_t>(_n4));
    detail::Store<uint16_t>(p + 10, static_cast<uint16_t>(_nvar));
  } else {
    p[6] = static_cast<uint8_t>(_n8);
    p[7] = static_cast<uint8_t>(_n4);
    p[8] = static_cast<uint8_t>(_nvar);
  }
  uint8_t* bm8 = p + _header;
  uint8_t* bm4 = bm8 + _len[0];
  uint8_t* bmb = bm4 + _len[1];
  uint8_t* bmv = bmb + _len[2];
  uint8_t* bmvar = bmv + _len[2];
  uint8_t* s8 = bmvar + _len[3];
  std::memset(bm8, 0, static_cast<size_t>(s8 - bm8));
  uint8_t* s4 = s8 + size_t(_n8) * 8;
  const uint32_t width = 1u << _width_code;
  uint8_t* ends = s4 + size_t(_n4) * 4;
  uint8_t* payload = ends + size_t(_nvar) * width;
  uint32_t offset = 0;
  for (const auto& e : _entries) {
    const uint32_t bit = detail::BitOf(e.id);
    const uint32_t byte = bit / 8;
    const uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
    switch (e.kind) {
      case Kind::S8:
        bm8[byte] |= mask;
        detail::Store<uint64_t>(s8, e.value);
        s8 += 8;
        break;
      case Kind::S4:
        bm4[byte] |= mask;
        detail::Store<uint32_t>(s4, static_cast<uint32_t>(e.value));
        s4 += 4;
        break;
      case Kind::Bit:
        bmb[byte] |= mask;
        if (e.value) bmv[byte] |= mask;
        break;
      case Kind::Var:
        bmvar[byte] |= mask;
        if (e.len) std::memcpy(payload + offset, e.data, e.len);
        offset += e.len;
        detail::StoreWidth(ends, width, offset);
        ends += width;
        break;
    }
  }
  return _total;
}

namespace {
// A list's header: its count, and the width of offsets into a payload of this size.
uint64_t ListHeader(size_t count, size_t payload_size) {
  return uint64_t(count) * 4 + detail::WidthCode(payload_size);
}

size_t VarintSize(uint64_t value) {
  size_t size = 1;
  for (value >>= 7; value != 0; value >>= 7) ++size;
  return size;
}
}  // namespace

VarList::VarList(std::span<const uint8_t> bytes) noexcept {
  if (bytes.empty()) return;
  uint64_t header = 0;
  size_t at = 0;
  for (uint32_t shift = 0;; shift += 7) {
    if (at >= bytes.size() || shift > 35) return;
    const uint8_t byte = bytes[at++];
    header |= uint64_t(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) break;
  }
  const uint64_t count = header >> 2;
  const uint32_t code = header & 3;
  if (code == 3 || count == 0 || count > std::numeric_limits<uint32_t>::max()) return;
  const uint32_t width = 1u << code;
  if (count * width > bytes.size() - at) return;
  const size_t fixed = at + size_t(count) * width;
  const size_t payload_size = bytes.size() - fixed;
  // As with a message, the last offset frames it, so truncation and trailing bytes show.
  if (detail::LoadWidth(bytes.data() + fixed - width, width) != payload_size) return;
  _bytes = bytes;
  _ends = bytes.data() + at;
  _payload = bytes.data() + fixed;
  _count = static_cast<uint32_t>(count);
  _payload_size = static_cast<uint32_t>(payload_size);
  _width = static_cast<uint8_t>(width);
}

bool VarList::Verify(std::span<const uint8_t> bytes) noexcept {
  if (bytes.empty()) return true;
  const VarList list(bytes);
  if (list.empty()) return false;
  // One list has one form: the shortest header, and the smallest width that holds the offsets.
  const uint64_t header = ListHeader(list._count, list._payload_size);
  if (list._width != (1u << (header & 3))) return false;
  if (static_cast<size_t>(list._ends - bytes.data()) != VarintSize(header)) return false;
  uint32_t previous = 0;
  for (size_t i = 0; i < list.size(); ++i) {
    const uint32_t end = list.End(i);
    if (end < previous) return false;
    previous = end;
  }
  return true;
}

ListBuilder& ListBuilder::AddBytes(std::span<const uint8_t> element) {
  if (_payload.size() + element.size() > std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("message field too large");
  }
  _payload.insert(_payload.end(), element.begin(), element.end());
  _ends.push_back(static_cast<uint32_t>(_payload.size()));
  return *this;
}

ListBuilder& ListBuilder::AddMessage(MessageBuilder& element) {
  const size_t size = element.SerializedSize();
  const size_t at = _payload.size();
  if (at + size > std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("message field too large");
  }
  _payload.resize(at + size);
  element.SerializeTo(std::span<uint8_t>(_payload).subspan(at));
  _ends.push_back(static_cast<uint32_t>(_payload.size()));
  return *this;
}

ListBuilder& ListBuilder::AddMessage(const MessageView& element) {
  return AddBytes(MessageBytes(element));
}

size_t ListBuilder::SerializedSize() const {
  if (_ends.empty()) return 0;
  const uint64_t header = ListHeader(_ends.size(), _payload.size());
  const uint64_t size =
      VarintSize(header) + uint64_t(_ends.size()) * (1u << (header & 3)) + _payload.size();
  if (size > std::numeric_limits<uint32_t>::max()) throw std::length_error("message too large");
  return static_cast<size_t>(size);
}

void ListBuilder::SerializeTo(std::span<uint8_t> out) const {
  if (out.size() < SerializedSize()) throw std::length_error("list output buffer too small");
  if (_ends.empty()) return;
  uint64_t header = ListHeader(_ends.size(), _payload.size());
  const uint32_t width = 1u << (header & 3);
  uint8_t* p = out.data();
  while (header >= 0x80) {
    *p++ = static_cast<uint8_t>(header | 0x80);
    header >>= 7;
  }
  *p++ = static_cast<uint8_t>(header);
  for (const uint32_t end : _ends) {
    detail::StoreWidth(p, width, end);
    p += width;
  }
  if (!_payload.empty()) std::memcpy(p, _payload.data(), _payload.size());
}

Bytes PackList(std::span<const std::string_view> values) {
  ListBuilder list;
  for (const std::string_view value : values) list.AddString(value);
  return list.Build();
}

void MessagePatch::CopyMessage(FieldId id, MessageBuilder& message) {
  CopyBytes(id, message.Build());
}

void MessagePatch::CopyMessage(FieldId id, const MessageView& message) {
  CopyBytes(id, MessageBytes(message));
}

void MessagePatch::AppendEncoded(std::string& out) const {
  internal::PutVarint(out, _changes.size());
  for (const auto& e : _changes) {
    internal::PutVarint(out, e.id);
    out.push_back(static_cast<char>(static_cast<uint8_t>(e.kind) | (e.remove ? 4 : 0)));
    if (e.remove) continue;
    switch (e.kind) {
      case Kind::S8:
        internal::PutFixed64(out, e.value);
        break;
      case Kind::S4:
        internal::PutFixed32(out, static_cast<uint32_t>(e.value));
        break;
      case Kind::Bit:
        out.push_back(e.value != 0 ? 1 : 0);
        break;
      case Kind::Var:
        internal::PutString(out, {reinterpret_cast<const char*>(e.data), e.len});
        break;
    }
  }
}

std::optional<MessagePatch> MessagePatch::Decode(std::string_view encoded) {
  internal::Decoder in(encoded);
  MessagePatch patch;
  const uint64_t count = in.Varint();
  for (uint64_t i = 0; i < count; ++i) {
    const uint64_t id = in.Varint();
    const uint8_t tag = in.Byte();
    if (!in.ok() || id == 0 || id > kMaxFieldId || tag > 7) return std::nullopt;
    detail::Entry e{static_cast<FieldId>(id), static_cast<Kind>(tag & 3), (tag & 4) != 0, 0, {}};
    e.value = 0;
    if (!e.remove) {
      switch (e.kind) {
        case Kind::S8:
          e.value = in.Fixed64();
          break;
        case Kind::S4:
          e.value = in.Fixed32();
          break;
        case Kind::Bit:
          e.value = in.Byte() != 0 ? 1 : 0;
          break;
        case Kind::Var: {
          const std::string_view bytes = in.String();
          if (bytes.size() > std::numeric_limits<uint32_t>::max()) return std::nullopt;
          e.len = static_cast<uint32_t>(bytes.size());
          e.data = reinterpret_cast<const uint8_t*>(bytes.data());
          break;
        }
      }
    }
    if (!in.ok()) return std::nullopt;
    patch.Put(e);
  }
  if (!in.ok() || !in.empty()) return std::nullopt;
  return patch;
}

bool CanApplyPatchInplace(const MessagePatch& patch, const MessageView& view) noexcept {
  for (const auto& e : patch.Changes()) {
    if (e.remove) return false;
    auto kind = view.KindOfField(e.id);
    if (!kind || *kind != e.kind) return false;
    if (e.kind == Kind::Var) {
      auto current = view.GetBytes(e.id);
      if (!current || current->size() != e.len) return false;
    }
  }
  return true;
}

bool ApplyPatchInplace(const MessagePatch& patch, std::span<uint8_t> bytes) noexcept {
  auto message = MutableView::Open(bytes);
  if (!message || !CanApplyPatchInplace(patch, message->View())) return false;
  for (const auto& e : patch.Changes()) {
    if (e.kind == Kind::Var)
      message->SetBytes(e.id, {e.data, e.len});
    else
      message->SetBits(e.id, e.kind, e.value);
  }
  return true;
}

Bytes Rebuild(const MessageView& source, const MessagePatch& patch) {
  if (source._base == nullptr) throw std::invalid_argument("invalid source message");
  const auto& changes = patch.Changes();
  // Fast path: replace one present Var field. Header, bitmaps and slots are copied as one
  // block, later end offsets shift by the length delta, and the payload is spliced.
  if (changes.size() == 1 && !changes[0].remove && changes[0].kind == Kind::Var) {
    const auto& e = changes[0];
    const uint32_t bit = detail::BitOf(e.id);
    if (source.TestRuntime(MessageView::kVar, bit)) {
      const uint32_t index = source.RankRuntime(MessageView::kVar, bit);
      // Open checks counts, not that they match the bitmaps.
      if (index >= source._nvar) throw std::invalid_argument("invalid source message");
      const uint32_t start = index ? source.EndAt(index - 1) : 0;
      const uint32_t end = source.EndAt(index);
      if (start > end || end > source._payload_size)
        throw std::invalid_argument("invalid source message");
      const uint64_t new_payload = uint64_t(source._payload_size) - (end - start) + e.len;
      if ((1u << detail::WidthCode(new_payload)) == source._width) {
        const size_t ends_offset = static_cast<size_t>(source._ends - source._base);
        const size_t payload_offset = static_cast<size_t>(source._payload - source._base);
        Bytes out(payload_offset + new_payload);
        uint8_t* p = out.data();
        std::memcpy(p, source._base, ends_offset);
        const uint32_t width = source._width;
        uint32_t previous = 0;
        for (uint32_t i = 0; i < source._nvar; ++i) {
          const uint32_t value = source.EndAt(i);
          if (value < previous || value > source._payload_size)
            throw std::invalid_argument("invalid source message");
          previous = value;
          const uint32_t shifted =
              i < index ? value : static_cast<uint32_t>(uint64_t(value) - (end - start) + e.len);
          detail::StoreWidth(p + ends_offset + size_t(i) * width, width, shifted);
        }
        uint8_t* payload = p + payload_offset;
        std::memcpy(payload, source._payload, start);
        if (e.len) std::memcpy(payload + start, e.data, e.len);
        std::memcpy(payload + start + e.len, source._payload + end, source._payload_size - end);
        return out;
      }
    }
  }
  // General path: re-encode every field; untouched fields (known or unknown) keep their
  // class and bytes. Var payloads are borrowed from the source and copied once.
  MessageBuilder builder;
  auto patched = [&](FieldId id) {
    for (const auto& e : changes)
      if (e.id == id) return true;
    return false;
  };
  const bool ok = source.ForEach([&](const Field& f) {
    if (!patched(f.id)) builder.AddRaw(f);
  });
  if (!ok) throw std::invalid_argument("invalid source message");
  for (const auto& e : changes) {
    if (e.remove) continue;
    Field f{e.id, e.kind, 0, {}};
    if (e.kind == Kind::Var)
      f.bytes = {e.data, e.len};
    else
      f.value = e.value;
    builder.AddRaw(f);
  }
  return builder.Build();
}

void ApplyPatch(const MessagePatch& patch, Bytes& bytes) {
  if (ApplyPatchInplace(patch, bytes)) return;
  auto view = MessageView::Open(bytes);
  if (!view) throw std::invalid_argument("invalid message");
  auto rebuilt = Rebuild(*view, patch);
  bytes.swap(rebuilt);
}

}  // namespace gendb
