// Stored messages and patches: from a log, a replica's stream or a file.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common.h"
#include "gendb/message.h"

using namespace gendb;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzz::Pieces input(data, size);
  const std::string_view message = input.Next();
  const std::string_view encoded_patch = input.Next();
  const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(message.data()),
                                       message.size());

  const auto view = MessageView::Open(bytes);
  if (view.has_value()) {
    (void)view->ForEach([](const Field&) {});
  }
  const std::optional<MessagePatch> patch = MessagePatch::Decode(encoded_patch);
  if (!patch.has_value()) return 0;

  // What decodes must encode to something that decodes the same.
  std::string again;
  patch->AppendEncoded(again);
  const std::optional<MessagePatch> second = MessagePatch::Decode(again);
  if (!second.has_value()) __builtin_trap();
  std::string third;
  second->AppendEncoded(third);
  if (third != again) __builtin_trap();

  if (!view.has_value()) return 0;
  try {
    Bytes rebuilt = Rebuild(*view, *patch);
    (void)MessageView::Open(rebuilt);
  } catch (const std::invalid_argument&) {
  } catch (const std::length_error&) {
  }
  Bytes copy(bytes.begin(), bytes.end());
  (void)ApplyPatchInplace(*patch, copy);
  return 0;
}
