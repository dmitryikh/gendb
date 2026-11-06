#include "gendb/message.h"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <string>

#include "absl/crc/crc32c.h"
#include "gendb/log_format.h"

namespace gendb {
namespace {

struct Value {
  Kind kind;
  uint64_t bits = 0;
  std::string bytes;
  bool operator==(const Value&) const = default;
};
using Model = std::map<FieldId, Value>;

Model Collect(const MessageView& view) {
  Model model;
  const bool ok = view.ForEach([&](const Field& f) {
    model[f.id] = {f.kind, f.value, std::string(f.bytes.begin(), f.bytes.end())};
  });
  EXPECT_TRUE(ok);
  return model;
}

Model Collect(const Bytes& bytes) {
  auto view = MessageView::Open(bytes);
  EXPECT_TRUE(view.has_value());
  return view ? Collect(*view) : Model{};
}

Bytes Build(const Model& model) {
  MessageBuilder builder;
  for (const auto& [id, v] : model) {
    builder.AddRaw(Field{
        id, v.kind, v.bits, {reinterpret_cast<const uint8_t*>(v.bytes.data()), v.bytes.size()}});
  }
  return builder.Build();
}

TEST(MessageFormat, EmptyMessageIsOnlyTheHeader) {
  MessageBuilder builder;
  auto bytes = builder.Build();
  EXPECT_EQ(bytes.size(), kHeaderSize);
  EXPECT_TRUE(MessageView::Verify(bytes));
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->FieldCount(), 0u);
  EXPECT_FALSE(view->Has(1));
  EXPECT_EQ((view->Get<1, int32_t>(-5)), -5);
  EXPECT_EQ(view->GetString<1>("absent"), "absent");
}

TEST(MessageFormat, ExactWireBytes) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 0x01020304);
  builder.AddField<bool>(2, true);
  builder.AddStringField(3, "abc");
  builder.AddField<uint64_t>(9, 0x1122334455667788ull);
  auto bytes = builder.Build();
  const Bytes expected = {
      1,    0,    2,    1,    1,    1,                 // version, width code, bitmap lengths
      1,    1,    1,                                   // counts: S8, S4, Var
      0x00, 0x01,                                      // S8 presence: id 9
      0x01,                                            // S4 presence: id 1
      0x02,                                            // Bit presence: id 2
      0x02,                                            // Bit values: id 2 = true
      0x04,                                            // Var presence: id 3
      0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,  // S8 slot
      0x04, 0x03, 0x02, 0x01,                          // S4 slot
      3,                                               // Var end offset
      'a',  'b',  'c',                                 // payload
  };
  EXPECT_EQ(bytes, expected);
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ(view->FieldCount(), 4u);
  EXPECT_EQ((view->Get<1, int32_t>()), 0x01020304);
  EXPECT_TRUE((view->Get<2, bool>()));
  EXPECT_EQ(view->GetString<3>(), "abc");
  EXPECT_EQ((view->Get<9, uint64_t>()), 0x1122334455667788ull);
  EXPECT_EQ(view->KindOfField(9), Kind::S8);
  EXPECT_EQ(view->KindOfField(4), std::nullopt);
}

// The worked example in docs/message_format.md. Changing these bytes changes the wire format.
TEST(MessageFormat, DocumentedPositionExample) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 1001);
  builder.AddField<int32_t>(2, 42);
  builder.AddField<int32_t>(3, 250);
  builder.AddStringField(4, "EURUSD");
  builder.AddField<float>(5, 1.2345f);
  builder.AddField<uint32_t>(6, 1);
  const Bytes expected = {
      0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x05, 0x01,  // header
      0x37,                                                  // S4 presence: 1, 2, 3, 5, 6
      0x08,                                                  // Var presence: 4
      0xe9, 0x03, 0x00, 0x00,                                // id 1
      0x2a, 0x00, 0x00, 0x00,                                // id 2
      0xfa, 0x00, 0x00, 0x00,                                // id 3
      0x19, 0x04, 0x9e, 0x3f,                                // id 5
      0x01, 0x00, 0x00, 0x00,                                // id 6
      0x06,                                                  // end offset of id 4
      'E',  'U',  'R',  'U',  'S',  'D',                     // payload
  };
  EXPECT_EQ(builder.Build(), expected);
}

TEST(MessageFormat, EveryScalarTypeCompileTimeAndRuntime) {
  MessageBuilder builder;
  builder.AddField<bool>(1, false);
  builder.AddField<int8_t>(2, -7);
  builder.AddField<uint8_t>(3, 255);
  builder.AddField<int16_t>(4, -32000);
  builder.AddField<uint16_t>(5, 65000);
  builder.AddField<int32_t>(6, -2000000000);
  builder.AddField<uint32_t>(7, 4000000000u);
  builder.AddField<int64_t>(8, -9000000000000000000ll);
  builder.AddField<uint64_t>(9, 18000000000000000000ull);
  builder.AddField<float>(10, -1.25f);
  builder.AddField<double>(11, 1.0 / 3.0);
  auto bytes = builder.Build();
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_FALSE((view->Get<1, bool>(true)));  // present false differs from absent
  EXPECT_TRUE(view->Has(1));
  EXPECT_EQ((view->Get<2, int8_t>()), -7);
  EXPECT_EQ((view->Get<3, uint8_t>()), 255);
  EXPECT_EQ((view->Get<4, int16_t>()), -32000);
  EXPECT_EQ((view->Get<5, uint16_t>()), 65000);
  EXPECT_EQ((view->Get<6, int32_t>()), -2000000000);
  EXPECT_EQ((view->Get<7, uint32_t>()), 4000000000u);
  EXPECT_EQ((view->Get<8, int64_t>()), -9000000000000000000ll);
  EXPECT_EQ((view->Get<9, uint64_t>()), 18000000000000000000ull);
  EXPECT_EQ((view->Get<10, float>()), -1.25f);
  EXPECT_EQ((view->Get<11, double>()), 1.0 / 3.0);
  EXPECT_EQ(view->Get<int8_t>(2), -7);
  EXPECT_EQ(view->Get<int16_t>(4), -32000);
  EXPECT_EQ(view->Get<double>(11), 1.0 / 3.0);
  EXPECT_FALSE(view->Get<bool>(1, true));
  // A different storage class reads as absent: never a reinterpretation.
  EXPECT_EQ((view->Get<9, int32_t>(77)), 77);
  EXPECT_EQ((view->Get<6, int64_t>(77)), 77);
  EXPECT_EQ(view->GetString(6, "x"), "x");
  EXPECT_EQ(view->Get<uint64_t>(0, 5), 5u);
  EXPECT_EQ(view->Get<uint64_t>(kMaxFieldId + 1, 5), 5u);
}

TEST(MessageFormat, EmptyVarFieldsAreDistinctFromAbsentFields) {
  MessageBuilder builder;
  builder.AddStringField(1, "");
  builder.AddStringField(2, "abc");
  builder.AddStringField(3, "");
  auto bytes = builder.Build();
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_TRUE(view->Has(1));
  EXPECT_TRUE(view->Has(3));
  EXPECT_FALSE(view->Has(4));
  EXPECT_EQ(view->GetString<1>("default"), "");
  EXPECT_EQ(view->GetString<2>(), "abc");
  EXPECT_EQ(view->GetBytes<3>()->size(), 0u);
  MessagePatch patch;
  patch.SetString(3, "");
  EXPECT_TRUE(ApplyPatchInplace(patch, bytes));
  patch.SetString(4, "");
  EXPECT_FALSE(ApplyPatchInplace(patch, bytes));
  ApplyPatch(patch, bytes);
  EXPECT_TRUE(MessageView::Open(bytes)->Has(4));
  EXPECT_TRUE(MessageView::Verify(bytes));
}

TEST(MessageFormat, HighFieldIdsUseMultiWordRank) {
  MessageBuilder builder;
  for (FieldId id : {1u, 63u, 64u, 65u, 130u, 200u, 2040u}) builder.AddField<uint32_t>(id, id * 3);
  for (FieldId id : {66u, 129u, 1000u}) builder.AddField<uint64_t>(id, id * 5);
  builder.AddStringField(70, "seventy");
  builder.AddStringField(1999, "late");
  builder.AddField<bool>(700, true);
  auto bytes = builder.Build();
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ((view->Get<64, uint32_t>()), 192u);
  EXPECT_EQ((view->Get<65, uint32_t>()), 195u);
  EXPECT_EQ((view->Get<130, uint32_t>()), 390u);
  EXPECT_EQ((view->Get<2040, uint32_t>()), 6120u);
  EXPECT_EQ((view->Get<1000, uint64_t>()), 5000u);
  EXPECT_EQ((view->Get<131, uint32_t>(9)), 9u);
  EXPECT_EQ(view->Get<uint32_t>(200), 600u);
  EXPECT_EQ(view->Get<uint64_t>(129), 645u);
  EXPECT_EQ(view->GetString<1999>(), "late");
  EXPECT_EQ(view->GetString(70), "seventy");
  EXPECT_TRUE((view->Get<700, bool>()));
  EXPECT_THROW(builder.AddField<int32_t>(2041, 1), std::invalid_argument);
  EXPECT_THROW(builder.AddField<int32_t>(0, 1), std::invalid_argument);
}

TEST(MessageFormat, BuilderOrderDuplicatesClearAndSerializeTo) {
  MessageBuilder builder;
  builder.AddField<int32_t>(5, 1);
  builder.AddStringField(2, "first");
  builder.AddField<int32_t>(5, 2);  // later value wins
  builder.AddStringField(2, "second");
  builder.AddField<bool>(9, true);
  builder.ClearField(9);
  const size_t size = builder.SerializedSize();
  std::vector<uint8_t> out(size + 16, 0xee);
  EXPECT_EQ(builder.SerializeTo(out), size);
  EXPECT_EQ(out[size], 0xee);
  out.resize(size);
  auto view = MessageView::Open(out);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ((view->Get<5, int32_t>()), 2);
  EXPECT_EQ(view->GetString<2>(), "second");
  EXPECT_FALSE(view->Has(9));
  EXPECT_EQ(view->FieldCount(), 2u);
  std::vector<uint8_t> small(size - 1);
  EXPECT_THROW(builder.SerializeTo(small), std::length_error);
  // A builder is reusable and gives identical output.
  EXPECT_EQ(builder.Build(), out);
}

TEST(MessageFormat, InplaceScalarBoolAndBytesUpdates) {
  MessageBuilder builder;
  builder.AddField<int16_t>(1, -1);
  builder.AddField<bool>(2, true);
  builder.AddField<double>(3, 2.5);
  builder.AddStringField(4, "EURUSD");
  auto bytes = builder.Build();
  auto mutable_view = MutableView::Open(bytes);
  ASSERT_TRUE(mutable_view.has_value());
  EXPECT_TRUE((mutable_view->Set<1, int16_t>(-300)));
  EXPECT_TRUE((mutable_view->Set<2, bool>(false)));
  EXPECT_TRUE((mutable_view->Set<3, double>(-7.5)));
  EXPECT_FALSE((mutable_view->Set<5, int32_t>(1)));   // absent
  EXPECT_FALSE((mutable_view->Set<3, float>(1.0f)));  // different storage class
  EXPECT_TRUE(mutable_view->SetString(4, "GBPUSD"));
  EXPECT_FALSE(mutable_view->SetString(4, "GBP"));
  EXPECT_FALSE((mutable_view->View().Get<2, bool>(true)));
  auto view = MessageView::Open(bytes);
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ((view->Get<1, int16_t>()), -300);
  EXPECT_FALSE((view->Get<2, bool>(true)));
  EXPECT_EQ((view->Get<3, double>()), -7.5);
  EXPECT_EQ(view->GetString<4>(), "GBPUSD");
  EXPECT_TRUE(mutable_view->Set<bool>(2, true));
  EXPECT_TRUE((MessageView::Open(bytes)->Get<2, bool>()));
}

TEST(MessageFormat, InplaceFailureLeavesEveryByteUnchanged) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 10);
  builder.AddStringField(2, "abc");
  builder.AddField<uint64_t>(3, 30);
  const auto original = builder.Build();
  auto try_patch = [&](auto&& configure) {
    auto bytes = original;
    MessagePatch patch;
    patch.Set<int32_t>(1, 99);  // valid change that must not be applied
    configure(patch);
    EXPECT_FALSE(ApplyPatchInplace(patch, bytes));
    EXPECT_EQ(bytes, original);
  };
  try_patch([](MessagePatch& p) { p.SetString(2, "abcd"); });  // length change
  try_patch([](MessagePatch& p) { p.Set<int32_t>(4, 1); });    // absent
  try_patch([](MessagePatch& p) { p.Set<uint32_t>(3, 1); });   // storage class change
  try_patch([](MessagePatch& p) { p.Remove(3); });             // removal
  try_patch([](MessagePatch& p) { p.Set<bool>(2, true); });    // Var -> Bit
}

TEST(MessageFormat, UnknownFieldsSurviveEveryPatchPath) {
  // A "newer" writer adds fields 10..13 of every class; an older reader knows 1..4 only.
  const std::string raw("\0raw\xff", 5);  // borrowed by the builder until Build()
  MessageBuilder newer;
  newer.AddField<int32_t>(1, 1);
  newer.AddStringField(2, "name");
  newer.AddField<uint64_t>(3, 3);
  newer.AddField<bool>(4, true);
  newer.AddField<float>(10, 10.5f);
  newer.AddField<int64_t>(11, -11);
  newer.AddField<bool>(12, false);
  newer.AddStringField(13, raw);
  const auto bytes = newer.Build();
  const Model before = Collect(bytes);
  auto check = [&](const Bytes& out, auto&& expected_changes) {
    EXPECT_TRUE(MessageView::Verify(out));
    Model expected = before;
    expected_changes(expected);
    EXPECT_EQ(Collect(out), expected);
  };
  auto view = *MessageView::Open(bytes);
  {
    MessagePatch p;  // splice path: replace a present Var field with a different length
    p.SetString(2, "a much longer name");
    check(Rebuild(view, p), [](Model& m) { m[2].bytes = "a much longer name"; });
  }
  {
    MessagePatch p;  // general path: insertion
    p.SetString(5, "inserted");
    check(Rebuild(view, p), [](Model& m) { m[5] = {Kind::Var, 0, "inserted"}; });
  }
  {
    MessagePatch p;  // general path: removal, scalar class change and several changes
    p.Remove(3);
    p.Set<double>(1, 2.0);
    p.SetString(2, "");
    check(Rebuild(view, p), [](Model& m) {
      m.erase(3);
      m[1] = {Kind::S8, std::bit_cast<uint64_t>(2.0), ""};
      m[2].bytes = "";
    });
  }
  {
    Bytes copy = bytes;  // in-place path through ApplyPatch
    MessagePatch p;
    p.Set<bool>(4, false);
    ApplyPatch(p, copy);
    EXPECT_EQ(copy.size(), bytes.size());
    check(copy, [](Model& m) { m[4].bits = 0; });
  }
}

TEST(MessageFormat, SpliceFallsBackWhenOffsetWidthChanges) {
  MessageBuilder builder;
  builder.AddStringField(1, "a");
  builder.AddStringField(2, "b");
  builder.AddField<int32_t>(3, 7);
  auto bytes = builder.Build();
  ASSERT_EQ(bytes[1], 0);  // one-byte end offsets
  const std::string big(300, 'x');
  MessagePatch grow;
  grow.SetString(1, big);
  ApplyPatch(grow, bytes);
  EXPECT_EQ(bytes[1], 1);  // two-byte end offsets
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ(view->GetString<1>(), big);
  EXPECT_EQ(view->GetString<2>(), "b");
  EXPECT_EQ((view->Get<3, int32_t>()), 7);
  MessagePatch shrink;
  shrink.SetString(1, "c");
  ApplyPatch(shrink, bytes);
  EXPECT_EQ(bytes[1], 0);
  EXPECT_TRUE(MessageView::Verify(bytes));
  EXPECT_EQ(MessageView::Open(bytes)->GetString<1>(), "c");
}

TEST(MessageFormat, MoreThan64KiBAndNestedMessages) {
  const std::string large(70000, 'z');
  MessageBuilder inner_builder;
  inner_builder.AddField<int32_t>(1, 5);
  inner_builder.AddStringField(2, "inner");
  const auto inner = inner_builder.Build();
  MessageBuilder builder;
  builder.AddStringField(1, large);
  builder.AddBytesField(2, inner);
  builder.AddField<uint32_t>(3, 3);
  auto bytes = builder.Build();
  EXPECT_EQ(bytes[1], 2);  // four-byte end offsets
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ(view->GetString<1>().size(), large.size());
  auto nested = MessageView::Open(*view->GetBytes<2>());
  ASSERT_TRUE(nested.has_value());
  EXPECT_TRUE(nested->Validate());
  EXPECT_EQ(nested->GetString<2>(), "inner");
  EXPECT_EQ((nested->Get<1, int32_t>()), 5);
}

TEST(MessageFormat, PatchCanBorrowFromTheMessageItself) {
  MessageBuilder builder;
  builder.AddStringField(1, "abcdef");
  builder.AddStringField(2, "uvwxyz");
  auto bytes = builder.Build();
  auto view = *MessageView::Open(bytes);
  MessagePatch patch;
  patch.SetBytes(2, *view.GetBytes<1>());  // aliases the destination buffer
  ApplyPatch(patch, bytes);
  EXPECT_EQ(MessageView::Open(bytes)->GetString<2>(), "abcdef");
  MessagePatch longer;
  longer.SetBytes(1, MessageView::Open(bytes)->GetBytes<2>()->subspan(1));
  ApplyPatch(longer, bytes);
  EXPECT_EQ(MessageView::Open(bytes)->GetString<1>(), "bcdef");
}

TEST(MessageFormat, RejectsMalformedInput) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 1);
  builder.AddStringField(2, "hello");
  builder.AddStringField(3, "world");
  builder.AddField<bool>(4, true);
  builder.AddField<uint64_t>(5, 5);
  const auto good = builder.Build();
  ASSERT_TRUE(MessageView::Verify(good));
  // Every truncation and any trailing byte are detected by Open itself.
  for (size_t n = 0; n < good.size(); ++n) {
    EXPECT_FALSE(MessageView::Open(std::span(good).first(n)).has_value()) << n;
  }
  auto trailing = good;
  trailing.push_back(0);
  EXPECT_FALSE(MessageView::Open(trailing).has_value());
  auto mutate = [&](size_t index, uint8_t value) {
    auto bad = good;
    bad[index] = value;
    return bad;
  };
  EXPECT_FALSE(MessageView::Open(mutate(0, 3)).has_value());  // version
  EXPECT_FALSE(MessageView::Open(mutate(1, 3)).has_value());  // width code
  EXPECT_FALSE(MessageView::Open(mutate(1, 8)).has_value());  // reserved flag
  // Layout: header(9) S8[1] S4[1] Bit[1] BitVal[1] Var[1] slots...
  const size_t s8 = 9, bit = 11, val = 12;
  // id 1 also in S8: Open trusts the header counts, Validate catches the overlap/mismatch.
  auto s8_overlap = mutate(s8, good[s8] | 0x01);
  ASSERT_TRUE(MessageView::Open(s8_overlap).has_value());
  EXPECT_FALSE(MessageView::Verify(s8_overlap));
  // Bit value for id 5, which has no Bit presence.
  auto value_without_presence = mutate(val, good[val] | 0x10);
  EXPECT_TRUE(MessageView::Open(value_without_presence).has_value());
  EXPECT_FALSE(MessageView::Verify(value_without_presence));
  auto bit_overlap = mutate(bit, good[bit] | 0x01);  // id 1 is S4 and Bit (Bit has no slot)
  ASSERT_TRUE(MessageView::Open(bit_overlap).has_value());
  EXPECT_FALSE(MessageView::Verify(bit_overlap));
  // Non-monotonic end offsets: Open succeeds (last offset is intact) but reads are bounded.
  const size_t ends = 14 + 8 + 4;
  auto bad_offsets = mutate(ends, 200);
  auto view = MessageView::Open(bad_offsets);
  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(view->Validate());
  EXPECT_EQ(view->GetString<2>("bounded"), "bounded");
  EXPECT_EQ(view->GetString<3>("bounded"), "bounded");
  MessagePatch patch;
  patch.SetString(9, "x");
  EXPECT_THROW(Rebuild(*view, patch), std::invalid_argument);
  // Non-canonical bitmap length: an extra trailing zero byte in the Var bitmap.
  Bytes padded(good.begin(), good.begin() + 14);
  padded[5] = 2;
  padded.push_back(0);
  padded.insert(padded.end(), good.begin() + 14, good.end());
  ASSERT_TRUE(MessageView::Open(padded).has_value());
  EXPECT_EQ(MessageView::Open(padded)->GetString<3>(), "world");
  EXPECT_FALSE(MessageView::Verify(padded));
}

TEST(MessageFormat, HeaderCountsThatContradictBitmapsStayInBounds) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 1);
  builder.AddStringField(2, "hello");
  builder.AddField<uint64_t>(5, 5);
  auto bytes = builder.Build();
  ASSERT_EQ(bytes[6], 1);  // n8
  ASSERT_EQ(bytes[7], 1);  // n4
  // Same total size (0 * 8 + 3 * 4 == 1 * 8 + 1 * 4), so Open accepts the message.
  bytes[6] = 0;
  bytes[7] = 3;
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(view->Validate());
  EXPECT_EQ((view->Get<5, uint64_t>(77)), 77u);  // slot index 0 >= n8 == 0: absent
  EXPECT_EQ(view->Get<uint64_t>(5, 77), 77u);
  EXPECT_FALSE(view->GetField(5).has_value());
  (void)view->Get<1, int32_t>();  // in bounds (reads another section), never past the end
  EXPECT_EQ(view->GetString<2>(), "hello");
  auto mutable_view = MutableView::Open(bytes);
  EXPECT_FALSE((mutable_view->Set<5, uint64_t>(1)));
  EXPECT_FALSE(mutable_view->Set<uint64_t>(5, 1));
  MessagePatch patch;
  patch.SetString(9, "x");
  EXPECT_THROW(Rebuild(*view, patch), std::invalid_argument);
  // More Var offsets announced than Var fields: also rejected by Validate.
  MessageBuilder two;
  two.AddStringField(1, "a");
  two.AddStringField(2, "b");
  auto var_bytes = two.Build();
  ASSERT_EQ(var_bytes[kHeaderSize], 0x03);  // Var bitmap: S8, S4 and Bit bitmaps are empty
  var_bytes[kHeaderSize] = 0x01;            // drop field 2; the header still says 2
  auto var_view = MessageView::Open(var_bytes);
  ASSERT_TRUE(var_view.has_value());
  EXPECT_FALSE(var_view->Validate());
  EXPECT_EQ(var_view->GetString<1>(), "a");
  EXPECT_FALSE(var_view->Has(2));
}

TEST(MessageFormat, TwoByteCountsAbove255FieldsPerClass) {
  MessageBuilder builder;
  for (FieldId id = 1; id <= 300; ++id) builder.AddField<uint32_t>(id, id * 7);
  builder.AddStringField(301, "tail");
  auto bytes = builder.Build();
  EXPECT_EQ(bytes[1] & kWideCountsFlag, kWideCountsFlag);
  EXPECT_EQ(bytes.size(), kWideHeaderSize + 0 + 38 + 0 + 38 + 300 * 4 + 1 + 4);
  auto view = MessageView::Open(bytes);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->Validate());
  EXPECT_EQ(view->FieldCount(), 301u);
  EXPECT_EQ((view->Get<1, uint32_t>()), 7u);
  EXPECT_EQ((view->Get<64, uint32_t>()), 448u);
  EXPECT_EQ((view->Get<300, uint32_t>()), 2100u);
  EXPECT_EQ(view->Get<uint32_t>(257), 1799u);
  EXPECT_EQ(view->GetString<301>(), "tail");
  MessagePatch patch;
  patch.Remove(300);
  patch.Remove(299);
  patch.Remove(298);
  patch.Remove(297);
  patch.Remove(296);
  ApplyPatch(patch, bytes);  // 295 S4 fields remain: still two-byte counts
  EXPECT_TRUE(MessageView::Verify(bytes));
  MessageBuilder small;
  for (FieldId id = 1; id <= 255; ++id) small.AddField<uint32_t>(id, id);
  auto small_bytes = small.Build();
  EXPECT_EQ(small_bytes[1] & kWideCountsFlag, 0);
  EXPECT_TRUE(MessageView::Verify(small_bytes));
  // A non-canonical wide header is rejected by Validate.
  Bytes widened(small_bytes.begin(), small_bytes.begin() + 6);
  widened[1] |= kWideCountsFlag;
  for (uint8_t c : {small_bytes[6], small_bytes[7], small_bytes[8]}) {
    widened.push_back(c);
    widened.push_back(0);
  }
  widened.insert(widened.end(), small_bytes.begin() + kHeaderSize, small_bytes.end());
  auto widened_view = MessageView::Open(widened);
  ASSERT_TRUE(widened_view.has_value());
  EXPECT_EQ((widened_view->Get<200, uint32_t>()), 200u);
  EXPECT_FALSE(widened_view->Validate());
}

TEST(MessageFormat, RandomizedRoundTripsAndPatchesMatchAModel) {
  std::mt19937_64 rng(1234);
  auto random_value = [&](Kind kind) {
    Value v{kind, 0, {}};
    switch (kind) {
      case Kind::S8:
        v.bits = rng();
        break;
      case Kind::S4:
        v.bits = rng() & 0xffffffffu;
        break;
      case Kind::Bit:
        v.bits = rng() & 1;
        break;
      case Kind::Var: {
        const size_t len = rng() % 4 == 0 ? rng() % 400 : rng() % 12;
        for (size_t i = 0; i < len; ++i) v.bytes.push_back(static_cast<char>(rng()));
        break;
      }
    }
    return v;
  };
  for (int round = 0; round < 300; ++round) {
    const FieldId max_id = round % 3 == 0 ? 300 : 40;
    Model model;
    const int count = static_cast<int>(rng() % 30);
    for (int i = 0; i < count; ++i) {
      model[1 + rng() % max_id] = random_value(static_cast<Kind>(rng() % 4));
    }
    auto bytes = Build(model);
    ASSERT_TRUE(MessageView::Verify(bytes)) << round;
    EXPECT_EQ(Collect(bytes), model);
    auto view = *MessageView::Open(bytes);
    EXPECT_EQ(view.FieldCount(), model.size());
    for (const auto& [id, v] : model) {
      auto field = view.GetField(id);
      ASSERT_TRUE(field.has_value());
      EXPECT_EQ(field->kind, v.kind);
    }
    // Apply a random patch and compare against the model.
    MessagePatch patch;
    std::vector<std::string> keep_alive;
    keep_alive.reserve(8);
    Model expected = model;
    const int changes = 1 + static_cast<int>(rng() % 4);
    for (int i = 0; i < changes; ++i) {
      const FieldId id = 1 + rng() % max_id;
      const int action = static_cast<int>(rng() % 3);
      if (action == 0) {
        patch.Remove(id);
        expected.erase(id);
        continue;
      }
      // Prefer same-kind/same-size changes half the time to exercise in-place and splice.
      Kind kind = static_cast<Kind>(rng() % 4);
      if (action == 1 && model.contains(id)) kind = model.at(id).kind;
      Value v = random_value(kind);
      switch (kind) {
        case Kind::S8:
          patch.Set<uint64_t>(id, v.bits);
          break;
        case Kind::S4:
          patch.Set<uint32_t>(id, static_cast<uint32_t>(v.bits));
          break;
        case Kind::Bit:
          patch.Set<bool>(id, v.bits != 0);
          break;
        case Kind::Var:
          keep_alive.push_back(v.bytes);
          patch.SetString(id, keep_alive.back());
          break;
      }
      expected[id] = v;
    }
    auto patched = bytes;
    ApplyPatch(patch, patched);
    ASSERT_TRUE(MessageView::Verify(patched)) << round;
    EXPECT_EQ(Collect(patched), expected) << round;
    EXPECT_EQ(patched, Build(expected)) << round;  // canonical encoding
  }
}

}  // namespace
TEST(MessagePatchEncodingTest, RoundTripsEveryKindAndUnknownFields) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 1);
  builder.AddField<int64_t>(2, 2);
  builder.AddField<bool>(3, false);
  builder.AddStringField(4, "old");
  builder.AddStringField(900, "unknown stays");
  builder.AddField<int32_t>(5, 5);
  const Bytes source = builder.Build();

  MessagePatch patch;
  patch.Set<int32_t>(1, -7);
  patch.Set<int64_t>(2, -8);
  patch.Set<bool>(3, true);
  patch.CopyString(4, "a longer string");
  patch.Remove(5);
  patch.Set<float>(1000, 1.5f);  // A field this reader's schema would not have.

  std::string encoded;
  patch.AppendEncoded(encoded);
  const std::optional<MessagePatch> decoded = MessagePatch::Decode(encoded);
  ASSERT_TRUE(decoded.has_value());

  const auto view = MessageView::Open(source);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(Rebuild(*view, *decoded), Rebuild(*view, patch));
}

TEST(MessagePatchEncodingTest, IsSmall) {
  MessagePatch patch;
  patch.Set<float>(6, 1.5f);
  std::string encoded;
  patch.AppendEncoded(encoded);
  EXPECT_EQ(encoded.size(), 7u);  // Count, ID, tag and 4 bytes.
}

TEST(MessagePatchEncodingTest, RejectsMalformedInput) {
  MessagePatch patch;
  patch.Set<int32_t>(1, 1);
  patch.CopyString(2, "value");
  patch.Remove(3);
  std::string encoded;
  patch.AppendEncoded(encoded);
  for (size_t size = 0; size < encoded.size(); ++size) {
    EXPECT_FALSE(MessagePatch::Decode(std::string_view(encoded).substr(0, size)).has_value())
        << "cut to " << size;
  }
  EXPECT_FALSE(MessagePatch::Decode(encoded + "x").has_value());  // Trailing bytes.

  std::string id_zero;
  internal::PutVarint(id_zero, 1);
  internal::PutVarint(id_zero, 0);
  id_zero.push_back(0);
  internal::PutFixed64(id_zero, 0);
  EXPECT_FALSE(MessagePatch::Decode(id_zero).has_value());

  std::string id_too_large;
  internal::PutVarint(id_too_large, 1);
  internal::PutVarint(id_too_large, kMaxFieldId + 1);
  id_too_large.push_back(4);
  EXPECT_FALSE(MessagePatch::Decode(id_too_large).has_value());

  std::string huge_count;
  internal::PutVarint(huge_count, uint64_t{1} << 40);
  EXPECT_FALSE(MessagePatch::Decode(huge_count).has_value());
}

// The log holds patches with a checksum of their result, so the bytes these produce are part of the
// on-disk format: if this fails, old logs no longer replay. Bump kFormatVersion and make recovery
// handle the old result, rather than updating these numbers.
TEST(MessagePatchEncodingTest, ResultBytesArePartOfTheLogFormat) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 1);
  builder.AddField<int64_t>(2, 2);
  builder.AddField<bool>(3, true);
  builder.AddStringField(4, "text");
  builder.AddStringField(5, "more");
  const Bytes source = builder.Build();
  const auto view = MessageView::Open(source);
  ASSERT_TRUE(view.has_value());
  const auto crc = [](const Bytes& bytes) {
    return static_cast<uint32_t>(
        absl::ComputeCrc32c({reinterpret_cast<const char*>(bytes.data()), bytes.size()}));
  };

  EXPECT_EQ(crc(source), 2303624702u);
  MessagePatch same_size_string;
  same_size_string.CopyString(4, "TEXT");
  Bytes in_place = source;
  ASSERT_TRUE(ApplyPatchInplace(same_size_string, in_place));
  EXPECT_EQ(crc(in_place), 659144704u);

  MessagePatch longer_string;
  longer_string.CopyString(4, "a longer text");
  EXPECT_EQ(crc(Rebuild(*view, longer_string)), 1202667440u);

  MessagePatch general;
  general.Set<int32_t>(6, 6);
  general.Remove(2);
  EXPECT_EQ(crc(Rebuild(*view, general)), 3776665425u);
}

}  // namespace gendb
