#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gendb/engine.h"
#include "gendb/log_format.h"

// Fuzzed data would almost never get past a checksum, so the targets take frame bodies, and add
// valid headers and checksums themselves, to reach the code behind them.
namespace gendb::fuzz {

// Splits input into the pieces a target works on: [size: u8] [bytes]*, the last one taking the
// rest.
class Pieces {
 public:
  Pieces(const uint8_t* data, size_t size) : _rest(reinterpret_cast<const char*>(data), size) {}
  bool empty() const noexcept { return _rest.empty(); }
  uint8_t Byte() {
    if (_rest.empty()) return 0;
    const auto byte = static_cast<uint8_t>(_rest.front());
    _rest.remove_prefix(1);
    return byte;
  }
  std::string_view Next() {
    const size_t size = Byte();
    const std::string_view piece = _rest.substr(0, size);
    _rest.remove_prefix(piece.size());
    return piece;
  }

 private:
  std::string_view _rest;
};

inline std::string MakeFrame(internal::FrameType type, std::string_view body) {
  std::string frame;
  const size_t begin = internal::BeginFrame(frame, type);
  frame.append(body);
  internal::EndFrame(frame, begin);
  return frame;
}

inline EngineSchema MakeSchema() {
  EngineSchema schema;
  schema.collections.push_back({"users"});
  schema.collections.push_back({"orders"});
  schema.collections.push_back({"sessions", /*ephemeral=*/true});
  schema.indices.push_back({"orders_by_value", 1, [](BytesConstView value, std::string& out) {
                              if (value.empty()) return false;
                              out.append(reinterpret_cast<const char*>(value.data()), 1);
                              return true;
                            }});
  return schema;
}

// A commit whose entries say what the input says, for a known collection.
inline std::string CommitBody(uint64_t lsn, Pieces& input) {
  std::string body;
  internal::PutFixed64(body, lsn);
  const uint8_t count = input.Byte() % 4;
  internal::PutFixed32(body, count);
  for (uint8_t i = 0; i < count; ++i) {
    internal::PutFixed32(body,
                         internal::CollectionIdOf(input.Byte() % 2 == 0 ? "users" : "orders"));
    body.push_back(static_cast<char>(input.Byte() % 4));
    const std::string_view key = input.Next();
    const std::string_view value = input.Next();
    internal::PutVarint(body, key.size());
    internal::PutVarint(body, value.size());
    body.append(key);
    body.append(value);
  }
  return body;
}

}  // namespace gendb::fuzz
