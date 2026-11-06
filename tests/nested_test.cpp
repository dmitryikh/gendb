// Nested messages and repeated fields, through generated code.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "evolution.gendb.h"
#include "gendb/list.h"
#include "order.gendb.h"
#include "position.gendb.h"

using namespace gendb::tests;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {
gendb::Bytes FullOrder() {
  OrderBuilder order;
  order.set_order_id(7).set_note("rush").set_status(OrderStatus::kPaid);
  order.set_customer(CustomerBuilder().set_name("Ann").set_address(
      AddressBuilder().set_city("Oslo").set_zip(150)));
  order.add_lines(OrderLineBuilder().set_sku("a").set_quantity(1).set_price(1.5));
  order.add_lines(OrderLineBuilder().set_sku("b").set_quantity(2).set_price(2.5));
  order.add_tags("x").add_tags("").add_tags("zzz");
  order.set_prices({1.5, -2.5});
  order.set_quantities({-1, 300});
  order.set_flags({true, false, true});
  order.set_history({OrderStatus::kNew, OrderStatus::kPaid});
  return order.Build();
}

TEST(NestedTest, ReadsWhatWasBuilt) {
  const gendb::Bytes bytes = FullOrder();
  ASSERT_TRUE(Order::Verify(bytes));
  const Order order{bytes};

  EXPECT_EQ(order.order_id(), 7u);
  EXPECT_EQ(order.note(), "rush");
  EXPECT_EQ(order.status(), OrderStatus::kPaid);

  ASSERT_TRUE(order.has_customer());
  EXPECT_EQ(order.customer().name(), "Ann");
  EXPECT_EQ(order.customer().address().city(), "Oslo");
  EXPECT_EQ(order.customer().address().zip(), 150);

  const auto lines = order.lines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].sku(), "a");
  EXPECT_EQ(lines[1].quantity(), 2);
  EXPECT_EQ(lines[1].price(), 2.5);
  std::vector<std::string> skus;
  for (const OrderLine& line : lines) skus.emplace_back(line.sku());
  EXPECT_THAT(skus, ElementsAre("a", "b"));

  EXPECT_THAT(order.tags(), ElementsAre("x", "", "zzz"));
  EXPECT_THAT(order.prices(), ElementsAre(1.5, -2.5));
  EXPECT_THAT(order.quantities(), ElementsAre(-1, 300));
  EXPECT_THAT(order.flags(), ElementsAre(true, false, true));
  EXPECT_THAT(order.history(), ElementsAre(OrderStatus::kNew, OrderStatus::kPaid));
  EXPECT_EQ(order.prices().ToVector(), (std::vector<double>{1.5, -2.5}));
}

// These read from the bytes, wherever they are: nothing is aligned.
TEST(NestedTest, ReadsFromAnyAlignment) {
  const gendb::Bytes bytes = FullOrder();
  for (size_t shift = 0; shift < 9; ++shift) {
    gendb::Bytes moved(shift, 0);
    moved.insert(moved.end(), bytes.begin(), bytes.end());
    const Order order{std::span<const uint8_t>(moved).subspan(shift)};
    EXPECT_THAT(order.prices(), ElementsAre(1.5, -2.5));
    EXPECT_THAT(order.quantities(), ElementsAre(-1, 300));
    EXPECT_EQ(order.lines()[1].price(), 2.5);
  }
}

// What the format promises for scalars holds for these too: not set differs from set to nothing.
TEST(NestedTest, PresenceIsSeparateFromBeingEmpty) {
  const gendb::Bytes unset = OrderBuilder().set_order_id(1).Build();
  const Order none{unset};
  EXPECT_FALSE(none.has_customer());
  EXPECT_FALSE(none.has_lines());
  EXPECT_FALSE(none.has_tags());
  EXPECT_FALSE(none.has_prices());
  EXPECT_THAT(none.tags(), IsEmpty());
  EXPECT_THAT(none.prices(), IsEmpty());
  EXPECT_THAT(none.lines(), IsEmpty());
  EXPECT_FALSE(none.customer().has_name());
  EXPECT_EQ(none.customer().name(), "");

  OrderBuilder builder;
  builder.set_order_id(1).set_customer(CustomerBuilder()).set_lines({}).set_tags({}).set_prices({});
  const gendb::Bytes empty = builder.Build();
  ASSERT_TRUE(Order::Verify(empty));
  const Order set{empty};
  EXPECT_TRUE(set.has_customer());
  EXPECT_TRUE(set.has_lines());
  EXPECT_TRUE(set.has_tags());
  EXPECT_TRUE(set.has_prices());
  EXPECT_THAT(set.tags(), IsEmpty());
  EXPECT_THAT(set.lines(), IsEmpty());
  EXPECT_THAT(set.prices(), IsEmpty());
  EXPECT_NE(none, set);
  EXPECT_EQ(set.customer(), Customer{CustomerBuilder().Build()});
}

// Scalars are exactly where they were, and getting to them does not look at the rest.
TEST(NestedTest, ScalarsKeepTheirBytes) {
  // docs/message_format.md's worked example, from before there were these fields.
  const gendb::Bytes position = PositionBuilder()
                                    .set_position_id(1001)
                                    .set_account_id(42)
                                    .set_volume(250)
                                    .set_instrument("EURUSD")
                                    .set_open_price(1.2345f)
                                    .set_direction(Direction::kBuy)
                                    .Build();
  EXPECT_THAT(position,
              ElementsAre(0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x05, 0x01,  // header
                          0x37, 0x08,                                            // bitmaps
                          0xe9, 0x03, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,        // S4 slots
                          0xfa, 0x00, 0x00, 0x00, 0x19, 0x04, 0x9e, 0x3f, 0x01, 0x00, 0x00, 0x00,
                          0x06,  // end offset
                          'E', 'U', 'R', 'U', 'S', 'D'));

  // With lists next to them, scalar slots are as they would be without.
  const gendb::Bytes plain =
      OrderBuilder().set_order_id(7).set_note("n").set_status(OrderStatus::kPaid).Build();
  const gendb::Bytes with_list = OrderBuilder()
                                     .set_order_id(7)
                                     .set_note("n")
                                     .set_status(OrderStatus::kPaid)
                                     .set_prices({1.0})
                                     .Build();
  const gendb::MessageView a = gendb::MessageView::OpenOrEmpty(plain);
  const gendb::MessageView b = gendb::MessageView::OpenOrEmpty(with_list);
  EXPECT_EQ(a.GetField(Order::OrderId)->value, b.GetField(Order::OrderId)->value);
  EXPECT_EQ(a.GetField(Order::Status)->value, b.GetField(Order::Status)->value);
  EXPECT_EQ(a.GetField(Order::OrderId)->kind, gendb::Kind::S8);
  EXPECT_EQ(a.GetField(Order::Status)->kind, gendb::Kind::S4);
  EXPECT_EQ(b.GetField(Order::Prices)->kind, gendb::Kind::Var);
}

// docs/message_format.md's worked example.
TEST(NestedTest, DocumentedOrderExample) {
  const gendb::Bytes bytes = OrderBuilder()
                                 .set_order_id(7)
                                 .set_customer(CustomerBuilder().set_name("Ann"))
                                 .set_tags({"x", "yz"})
                                 .set_quantities({-1, 300})
                                 .Build();
  EXPECT_THAT(bytes,
              ElementsAre(0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x03,  // header
                          0x01,                                                  // S8 presence
                          0xa8,                                                  // Var presence
                          0x07, 0, 0, 0, 0, 0, 0, 0,                             // S8 slot
                          0x0e, 0x14, 0x18,                                      // end offsets
                          0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01,  // Customer header
                          0x01,                                                  // Var presence
                          0x03, 'A', 'n', 'n',                                   // end, payload
                          0x08, 0x01, 0x03, 'x', 'y', 'z',                       // tags
                          0xff, 0xff, 0x2c, 0x01));                              // quantities
  EXPECT_EQ(bytes.size(), 46u);
  EXPECT_TRUE(Order::Verify(bytes));
}

TEST(NestedTest, ChildBuildersCanBeReused) {
  OrderBuilder order;
  OrderLineBuilder line;
  for (int i = 0; i < 100; ++i) {
    line.Reset();
    line.set_sku("sku").set_quantity(i);
    order.add_lines(line);
  }
  const gendb::Bytes bytes = order.Build();
  ASSERT_TRUE(Order::Verify(bytes));
  const auto lines = Order{bytes}.lines();
  ASSERT_EQ(lines.size(), 100u);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(lines[i].quantity(), i);
}

TEST(NestedTest, NothingIsBorrowedForRepeatedFields) {
  OrderBuilder order;
  {
    std::string tag = "a string long enough not to fit in place";
    order.add_tags(tag);
    order.add_tags(std::string("temporary, also long enough not to fit in place"));
    tag.assign(tag.size(), 'x');
  }
  const gendb::Bytes bytes = order.Build();
  EXPECT_THAT(Order{bytes}.tags(), ElementsAre("a string long enough not to fit in place",
                                               "temporary, also long enough not to fit in place"));
}

TEST(NestedTest, BuildingAgainAfterAddingMore) {
  OrderBuilder order;
  order.add_tags("a").add_prices(1);
  const gendb::Bytes first = order.Build();
  order.add_tags("b").add_prices(2);
  const gendb::Bytes second = order.Build();
  EXPECT_THAT(Order{first}.tags(), ElementsAre("a"));
  EXPECT_THAT(Order{second}.tags(), ElementsAre("a", "b"));
  EXPECT_THAT(Order{second}.prices(), ElementsAre(1, 2));
  EXPECT_TRUE(Order::Verify(second));
}

TEST(NestedTest, SettingAndClearingReplaceAList) {
  OrderBuilder order;
  order.add_prices(1).set_prices({2, 3}).add_prices(4);
  order.add_tags("a").set_tags({"b"}).add_tags("c");
  order.add_quantities(1).clear_quantities();
  order.add_lines(OrderLineBuilder().set_sku("a")).clear_lines();
  const gendb::Bytes bytes = order.Build();
  const Order result{bytes};
  EXPECT_THAT(result.prices(), ElementsAre(2, 3, 4));
  EXPECT_THAT(result.tags(), ElementsAre("b", "c"));
  EXPECT_FALSE(result.has_quantities());
  EXPECT_FALSE(result.has_lines());
}

TEST(NestedTest, BuilderFromAMessageContinuesItsLists) {
  const gendb::Bytes bytes = FullOrder();
  OrderBuilder builder{Order{bytes}};
  builder.add_tags("more").add_prices(9).add_lines(OrderLineBuilder().set_sku("c"));
  const gendb::Bytes rebuilt = builder.Build();
  ASSERT_TRUE(Order::Verify(rebuilt));
  const Order order{rebuilt};
  EXPECT_THAT(order.tags(), ElementsAre("x", "", "zzz", "more"));
  EXPECT_THAT(order.prices(), ElementsAre(1.5, -2.5, 9));
  ASSERT_EQ(order.lines().size(), 3u);
  EXPECT_EQ(order.lines()[0].sku(), "a");
  EXPECT_EQ(order.lines()[2].sku(), "c");
  // The others were not touched.
  EXPECT_THAT(order.quantities(), ElementsAre(-1, 300));
  EXPECT_EQ(order.customer().name(), "Ann");
}

TEST(NestedTest, ViewsCanBeCopiedIntoAnotherMessage) {
  const gendb::Bytes source = FullOrder();
  const Order order{source};
  OrderBuilder copy;
  copy.set_customer(order.customer());
  for (const OrderLine& line : order.lines()) copy.add_lines(line);
  const gendb::Bytes bytes = copy.Build();
  ASSERT_TRUE(Order::Verify(bytes));
  EXPECT_EQ(Order{bytes}.customer(), order.customer());
  EXPECT_EQ(Order{bytes}.lines(), order.lines());
}

TEST(NestedTest, AViewOfNothingIsStoredAsAnEmptyMessage) {
  OrderBuilder order;
  order.set_customer(Customer{}).add_lines(OrderLine{});
  const gendb::Bytes bytes = order.Build();
  ASSERT_TRUE(Order::Verify(bytes));
  EXPECT_TRUE(Order{bytes}.has_customer());
  EXPECT_EQ(Order{bytes}.lines().size(), 1u);
}

TEST(NestedTest, MisusingAListThrows) {
  // Three bytes, which are neither two-byte elements nor a list of strings.
  const gendb::Bytes bytes = OrderBuilder().set_note("abc").Build();
  gendb::MessageBuilder builder{Order{bytes}.View()};
  EXPECT_THROW(builder.AddRepeated<int16_t>(Order::Note, 1), std::invalid_argument);
  EXPECT_THROW(builder.AddRepeatedString(Order::Note, "x"), std::invalid_argument);

  gendb::MessageBuilder mixed;
  mixed.AddRepeated<int16_t>(9, 1);
  EXPECT_THROW(mixed.AddRepeated<int32_t>(9, 1), std::invalid_argument);
  EXPECT_THROW(mixed.AddRepeatedString(9, "x"), std::invalid_argument);
}

TEST(NestedTest, PatchesReplaceAFieldAsAWhole) {
  gendb::Bytes bytes = FullOrder();
  gendb::ListBuilder lines;
  lines.AddMessage(OrderLineBuilder().set_sku("new"));
  gendb::ApplyPatch(OrderPatchBuilder()
                        .set_customer(CustomerBuilder().set_name("Bob"))
                        .set_lines(lines)
                        .set_tags({"t"})
                        .set_prices({3.5})
                        .set_history({OrderStatus::kShipped})
                        .clear_flags()
                        .Build(),
                    bytes);
  ASSERT_TRUE(Order::Verify(bytes));
  const Order order{bytes};
  EXPECT_EQ(order.customer().name(), "Bob");
  EXPECT_FALSE(order.customer().has_address());
  ASSERT_EQ(order.lines().size(), 1u);
  EXPECT_EQ(order.lines()[0].sku(), "new");
  EXPECT_THAT(order.tags(), ElementsAre("t"));
  EXPECT_THAT(order.prices(), ElementsAre(3.5));
  EXPECT_THAT(order.history(), ElementsAre(OrderStatus::kShipped));
  EXPECT_FALSE(order.has_flags());
  EXPECT_THAT(order.quantities(), ElementsAre(-1, 300));

  // A patch owns what it holds, including through the form in the log.
  std::string encoded;
  OrderPatchBuilder().set_lines({}).set_tags({}).set_prices({}).Build().AppendEncoded(encoded);
  const auto decoded = gendb::MessagePatch::Decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  gendb::ApplyPatch(*decoded, bytes);
  const Order emptied{bytes};
  EXPECT_TRUE(emptied.has_lines());
  EXPECT_THAT(emptied.lines(), IsEmpty());
  EXPECT_TRUE(emptied.has_tags());
  EXPECT_THAT(emptied.tags(), IsEmpty());
  EXPECT_TRUE(Order::Verify(bytes));
}

TEST(NestedTest, ATreeOfItsOwnType) {
  TreeNodeBuilder root;
  root.set_value(1);
  root.set_next(TreeNodeBuilder().set_value(2).set_next(TreeNodeBuilder().set_value(3)));
  root.add_children(TreeNodeBuilder().set_value(4).add_children(TreeNodeBuilder().set_value(5)));
  const gendb::Bytes bytes = root.Build();
  ASSERT_TRUE(TreeNode::Verify(bytes));
  const TreeNode tree{bytes};
  EXPECT_EQ(tree.next().next().value(), 3);
  EXPECT_EQ(tree.children()[0].children()[0].value(), 5);
  EXPECT_FALSE(tree.next().next().has_next());
}

// A hostile message cannot overflow the stack.
TEST(NestedTest, VerifyStopsAtTheDepthLimit) {
  const auto nested = [](int depth) {
    gendb::Bytes bytes = TreeNodeBuilder().set_value(0).Build();
    for (int i = 1; i < depth; ++i) {
      const TreeNode inner{bytes};
      bytes = TreeNodeBuilder().set_next(inner).Build();
    }
    return bytes;
  };
  EXPECT_TRUE(TreeNode::Verify(nested(gendb::kMaxMessageDepth)));
  EXPECT_FALSE(TreeNode::Verify(nested(gendb::kMaxMessageDepth + 1)));
  EXPECT_TRUE(TreeNode::Verify(nested(gendb::kMaxMessageDepth + 1), gendb::kMaxMessageDepth + 1));

  const auto through_lists = [](int depth) {
    gendb::Bytes bytes = TreeNodeBuilder().set_value(0).Build();
    for (int i = 1; i < depth; ++i) {
      const TreeNode inner{bytes};
      bytes = TreeNodeBuilder().add_children(inner).Build();
    }
    return bytes;
  };
  EXPECT_TRUE(TreeNode::Verify(through_lists(gendb::kMaxMessageDepth)));
  EXPECT_FALSE(TreeNode::Verify(through_lists(gendb::kMaxMessageDepth + 1)));
}

// Damage anywhere inside is found, however deep, whereas MessageView::Verify stops at the outside.
TEST(NestedTest, VerifyDescends) {
  const auto with = [](auto set) {
    gendb::MessageBuilder builder;
    set(builder);
    return builder.Build();
  };
  const std::array<uint8_t, 3> garbage = {0xff, 0xff, 0xff};
  const gendb::Bytes bad_customer =
      with([&](auto& b) { b.AddBytesField(Order::Customer, garbage); });
  EXPECT_TRUE(gendb::MessageView::Verify(bad_customer));
  EXPECT_FALSE(Order::Verify(bad_customer));
  // Reading it stays in bounds.
  EXPECT_EQ(Order{bad_customer}.customer().name(), "");

  EXPECT_FALSE(Order::Verify(with([&](auto& b) { b.AddBytesField(Order::Lines, garbage); })));
  EXPECT_THAT(Order{with([&](auto& b) { b.AddBytesField(Order::Lines, garbage); })}.lines(),
              IsEmpty());
  EXPECT_FALSE(Order::Verify(with([&](auto& b) { b.AddBytesField(Order::Tags, garbage); })));
  // Not a whole number of elements.
  EXPECT_FALSE(Order::Verify(with([&](auto& b) { b.AddBytesField(Order::Prices, garbage); })));
  EXPECT_FALSE(Order::Verify(with([&](auto& b) { b.AddBytesField(Order::Quantities, garbage); })));
  // A bool is 0 or 1.
  EXPECT_FALSE(Order::Verify(with([&](auto& b) { b.AddBytesField(Order::Flags, garbage); })));

  // An element of a list.
  const gendb::Bytes bad_line = with([&](auto& b) {
    gendb::ListBuilder lines;
    lines.AddMessage(OrderLineBuilder().set_sku("fine"));
    lines.AddBytes(garbage);
    const gendb::Bytes bytes = lines.Build();
    b.CopyBytesField(Order::Lines, bytes);
  });
  EXPECT_FALSE(Order::Verify(bad_line));
  EXPECT_EQ(Order{bad_line}.lines().size(), 2u);
  EXPECT_EQ(Order{bad_line}.lines()[0].sku(), "fine");
  EXPECT_FALSE(Order{bad_line}.lines()[1].has_sku());

  // A level down.
  const gendb::Bytes inner = with([&](auto& b) { b.AddBytesField(Customer::Address, garbage); });
  const gendb::Bytes deep = with([&](auto& b) { b.AddBytesField(Order::Customer, inner); });
  EXPECT_FALSE(Order::Verify(deep));

  // A deprecated field is opaque, like any this program does not know.
  EXPECT_TRUE(Order::Verify(with([&](auto& b) { b.AddBytesField(11, garbage); })));
}

// The same list has one form, as equal fields are equal bytes.
TEST(ListTest, CanonicalForm) {
  gendb::ListBuilder list;
  list.AddString("ab").AddString("").AddString("c");
  const gendb::Bytes bytes = list.Build();
  // The count, 3, times 4, plus width code 0: 0x0c. Then the ends and the payload.
  EXPECT_THAT(bytes, ElementsAre(0x0c, 2, 2, 3, 'a', 'b', 'c'));
  EXPECT_TRUE(gendb::VarList::Verify(bytes));

  // An empty list is no bytes at all.
  EXPECT_TRUE(gendb::ListBuilder().Build().empty());
  EXPECT_TRUE(gendb::VarList::Verify({}));
  const std::array<uint8_t, 1> counted_zero = {0};
  EXPECT_FALSE(gendb::VarList::Verify(counted_zero));

  // A longer header than needed.
  const std::array<uint8_t, 5> long_header = {0x84, 0x00, 1, 'a', 0};
  EXPECT_FALSE(gendb::VarList::Verify(std::span(long_header).first(4)));
  // Wider offsets than needed.
  const std::array<uint8_t, 4> wide = {0x05, 1, 0, 'a'};
  EXPECT_EQ(gendb::VarList(wide).size(), 1u);
  EXPECT_FALSE(gendb::VarList::Verify(wide));
  // A last offset that does not frame the payload.
  const std::array<uint8_t, 5> unframed = {0x08, 2, 1, 'a', 'b'};
  EXPECT_TRUE(gendb::VarList(unframed).empty());
  EXPECT_FALSE(gendb::VarList::Verify(unframed));
  // Offsets going back, which read as an empty element.
  const std::array<uint8_t, 7> back_framed = {0x0c, 3, 1, 3, 'a', 'b', 'c'};
  EXPECT_EQ(gendb::VarList(back_framed).size(), 3u);
  EXPECT_TRUE(gendb::VarList(back_framed)[1].empty());
  EXPECT_FALSE(gendb::VarList::Verify(back_framed));
}

TEST(ListTest, OffsetWidthGrowsWithThePayload) {
  for (const size_t size : {0uz, 1uz, 255uz, 256uz, 65535uz, 65536uz, 70000uz}) {
    const std::string element(size, 'x');
    gendb::ListBuilder list;
    list.AddString(element).AddString("end");
    const gendb::Bytes bytes = list.Build();
    EXPECT_EQ(bytes.size(), list.SerializedSize());
    EXPECT_TRUE(gendb::VarList::Verify(bytes)) << size;
    const gendb::StringList read(bytes);
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0].size(), size);
    EXPECT_EQ(read[1], "end");
  }
}

TEST(ListTest, CountsThatNeedALongerHeader) {
  gendb::ListBuilder list;
  for (int i = 0; i < 100000; ++i) list.AddString("x");
  const gendb::Bytes bytes = list.Build();
  EXPECT_TRUE(gendb::VarList::Verify(bytes));
  const gendb::StringList read(bytes);
  ASSERT_EQ(read.size(), 100000u);
  EXPECT_EQ(read[99999], "x");
}

// Whatever the bytes, reads stay inside them. ASan runs this too.
TEST(ListTest, AnyDamageStaysInBounds) {
  gendb::ListBuilder list;
  for (int i = 0; i < 20; ++i) list.AddString(std::string(i, 'x'));
  const gendb::Bytes good = list.Build();
  for (size_t at = 0; at < good.size(); ++at) {
    for (const uint8_t value : {0x00, 0x01, 0x7f, 0x80, 0xff}) {
      gendb::Bytes bad = good;
      bad[at] = value;
      const gendb::VarList read(bad);
      size_t total = 0;
      for (size_t i = 0; i < read.size(); ++i) {
        const auto element = read[i];
        total += element.size();
        if (!element.empty()) {
          EXPECT_GE(element.data(), bad.data());
          EXPECT_LE(element.data() + element.size(), bad.data() + bad.size());
        }
      }
      (void)total;
      (void)gendb::VarList::Verify(bad);
    }
  }
  for (size_t size = 0; size < good.size(); ++size) {
    const std::span<const uint8_t> cut = std::span(good).first(size);
    EXPECT_TRUE(gendb::VarList(cut).empty()) << size;
    if (size > 0) EXPECT_FALSE(gendb::VarList::Verify(cut)) << size;
  }

  const std::array<uint8_t, 3> odd = {1, 2, 3};
  const gendb::ScalarList<double> doubles(odd);
  EXPECT_TRUE(doubles.empty());
  EXPECT_EQ(doubles[5], 0.0);
  EXPECT_EQ(gendb::ScalarList<int16_t>(odd).size(), 1u);
  EXPECT_EQ(gendb::ScalarList<int16_t>(odd)[1], 0);
}

// Each side reads what it knows, and what it does not know survives a trip through it.
namespace evolution = gendb::tests::evolution;

TEST(NestedEvolutionTest, ANestedMessageCanGrow) {
  const gendb::Bytes contact_v2 =
      evolution::ContactV2Builder().set_email("a@b").set_phone("555").add_labels("home").Build();
  evolution::MemberV2Builder v2;
  v2.set_id(1)
      .set_contact(evolution::ContactV2{contact_v2})
      .add_others(evolution::ContactV2{contact_v2});
  gendb::Bytes bytes = v2.Build();

  // An old reader.
  ASSERT_TRUE(evolution::MemberV1::Verify(bytes));
  const evolution::MemberV1 old{bytes};
  EXPECT_EQ(old.contact().email(), "a@b");
  EXPECT_EQ(old.others()[0].email(), "a@b");

  // An old writer, in both ways it can change the record.
  gendb::ApplyPatch(evolution::MemberV1PatchBuilder().set_id(2).Build(), bytes);
  evolution::MemberV1Builder rewritten{evolution::MemberV1{bytes}};
  rewritten.add_others(evolution::ContactV1Builder().set_email("c@d"));
  const gendb::Bytes after = rewritten.Build();

  const evolution::MemberV2 member{after};
  EXPECT_EQ(member.id(), 2u);
  EXPECT_EQ(member.contact().phone(), "555");
  EXPECT_THAT(member.contact().labels(), ElementsAre("home"));
  ASSERT_EQ(member.others().size(), 2u);
  EXPECT_EQ(member.others()[0].phone(), "555");
  EXPECT_EQ(member.others()[1].email(), "c@d");
  EXPECT_FALSE(member.others()[1].has_phone());

  // And a new reader of old data.
  const gendb::Bytes old_bytes = evolution::MemberV1Builder()
                                     .set_contact(evolution::ContactV1Builder().set_email("e"))
                                     .Build();
  const evolution::MemberV2 upgraded{old_bytes};
  EXPECT_EQ(upgraded.contact().email(), "e");
  EXPECT_FALSE(upgraded.contact().has_phone());
  EXPECT_THAT(upgraded.contact().labels(), IsEmpty());
}
}  // namespace
