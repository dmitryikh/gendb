// Stored messages and patches: from a log, a replica's stream or a file.
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common.h"
#include "gendb/list.h"
#include "gendb/message.h"
#include "generated/order.gendb.h"

using namespace gendb;

namespace {
// Reads all of it the way a program would. This walks a damaged record safely, and only as deep as
// it goes, as each level is inside the bytes of the one before.
// Unsigned, as whatever the bytes say gets added up.
uint32_t WalkTree(const tests::TreeNode& node, int depth) {
  if (depth == 0) return 0;
  uint32_t sum = static_cast<uint32_t>(node.value());
  if (node.has_next()) sum += WalkTree(node.next(), depth - 1);
  for (const tests::TreeNode& child : node.children()) sum += WalkTree(child, depth - 1);
  return sum;
}

uint32_t WalkOrder(const tests::Order& order) {
  uint32_t sum =
      static_cast<uint32_t>(order.order_id()) + static_cast<uint32_t>(order.note().size());
  const tests::Customer customer = order.customer();
  sum += static_cast<uint32_t>(customer.name().size()) +
         static_cast<uint32_t>(customer.address().zip());
  for (const std::string_view phone : customer.phones()) sum += static_cast<uint32_t>(phone.size());
  for (const tests::OrderLine& line : order.lines()) sum += static_cast<uint32_t>(line.quantity());
  for (const std::string_view tag : order.tags()) sum += static_cast<uint32_t>(tag.size());
  for (const double price : order.prices()) sum += price > 0 ? 1 : 0;
  for (const int16_t quantity : order.quantities()) sum += static_cast<uint32_t>(quantity);
  for (const bool flag : order.flags()) sum += flag ? 1 : 0;
  for (const tests::OrderStatus status : order.history()) sum += static_cast<uint32_t>(status);
  return sum;
}

void FuzzLists(std::span<const uint8_t> bytes) {
  // Anything that passes as a list is the one form of that list.
  const VarList list(bytes);
  size_t total = 0;
  for (size_t i = 0; i < list.size(); ++i) total += list[i].size();
  if (VarList::Verify(bytes)) {
    // Elements do not overlap.
    if (total > bytes.size()) __builtin_trap();
    ListBuilder again;
    for (size_t i = 0; i < list.size(); ++i) again.AddBytes(list[i]);
    const Bytes rebuilt = again.Build();
    if (!std::equal(rebuilt.begin(), rebuilt.end(), bytes.begin(), bytes.end())) __builtin_trap();
  }
  const StringList strings(bytes);
  for (const std::string_view element : strings) total += element.size();
  const ScalarList<double> doubles(bytes);
  for (const double element : doubles) total += element > 0 ? 1 : 0;
  const ScalarList<bool> bools(bytes);
  (void)ScalarList<bool>::Verify(bytes);
  (void)bools[bools.size()];
  volatile size_t sink = total;
  (void)sink;
}

void FuzzGenerated(std::span<const uint8_t> bytes) {
  volatile uint32_t sink = WalkOrder(tests::Order(bytes));
  sink = WalkTree(tests::TreeNode(bytes), kMaxMessageDepth);
  (void)sink;
  // Verified bytes survive being taken apart and put together again.
  if (tests::Order::Verify(bytes)) {
    tests::OrderBuilder again{tests::Order(bytes)};
    const Bytes rebuilt = again.Build();
    if (!std::equal(rebuilt.begin(), rebuilt.end(), bytes.begin(), bytes.end())) __builtin_trap();
  }
  (void)tests::TreeNode::Verify(bytes);
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzz::Pieces input(data, size);
  const std::string_view message = input.Next();
  const std::string_view encoded_patch = input.Next();
  const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(message.data()),
                                       message.size());

  FuzzLists(bytes);
  FuzzGenerated(bytes);

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
