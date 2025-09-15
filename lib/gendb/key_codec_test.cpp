#include "gendb/key_codec.h"

#include <gtest/gtest.h>

using namespace gendb::internal::key_codec;

TEST(KeyCodecTest, IntegerEncodeDecode) {
  Bytes out;
  int32_t value = -123456;
  WriteInteger(value, out);
  BytesConstView in(out.data(), out.size());
  auto decoded = ReadInteger<int32_t>(in);
  EXPECT_EQ(decoded, value);
}

TEST(KeyCodecTest, StringEncodeDecode) {
  Bytes out;
  std::string s = "hello world";
  WriteString(s, out);
  BytesConstView in(out.data(), out.size());
  auto sv = ReadStringView(in);
  EXPECT_EQ(sv, s);
}

TEST(KeyCodecTest, TupleEncodeDecode) {
  std::tuple<int32_t, std::string> tup{-42, "abc"};
  Bytes out = EncodeTuple(tup);
  BytesConstView in(out.data(), out.size());
  auto decoded = DecodeTuple<int32_t, std::string>(in);
  EXPECT_EQ(std::get<0>(decoded), -42);
  EXPECT_EQ(std::get<1>(decoded), "abc");
}

TEST(KeyCodecTest, EnumEncodeDecode) {
  enum class MyEnum : int8_t { A = 1, B = 2 };
  Bytes out;
  MyEnum e = MyEnum::B;
  EncodeField(e, out);
  BytesConstView in(out.data(), out.size());
  auto decoded = DecodeField<MyEnum>(in);
  EXPECT_EQ(decoded, MyEnum::B);
}

TEST(KeyCodecTest, IntegerEnumTupleEncodeDecode) {
  enum class MyEnum : int8_t { A = 1, B = 2 };
  std::tuple<int32_t, MyEnum> tup{123, MyEnum::A};
  Bytes out = EncodeTuple(tup);
  BytesConstView in(out.data(), out.size());
  auto decoded = DecodeTuple<int32_t, MyEnum>(in);
  EXPECT_EQ(std::get<0>(decoded), 123);
  EXPECT_EQ(std::get<1>(decoded), MyEnum::A);
}

TEST(KeyCodecOrderTest, IntegerOrderPreserved) {
  std::vector<int32_t> values = {-100, -1, 0, 1, 42, 100, 100000};
  std::vector<Bytes> encoded;
  for (auto v : values) {
    Bytes out;
    WriteInteger(v, out);
    encoded.push_back(out);
  }
  for (size_t i = 1; i < encoded.size(); ++i) {
    EXPECT_LT(encoded[i - 1], encoded[i]) << "Failed for " << values[i - 1] << " < " << values[i];
  }
}

TEST(KeyCodecOrderTest, StringOrderPreserved) {
  std::vector<std::string> values = {"", "a", "abc", "b", "ba", "zzz"};
  std::vector<Bytes> encoded;
  for (const auto& s : values) {
    Bytes out;
    WriteString(s, out);
    encoded.push_back(out);
  }
  for (size_t i = 1; i < encoded.size(); ++i) {
    EXPECT_LT(encoded[i - 1], encoded[i])
        << "Failed for \"" << values[i - 1] << "\" < \"" << values[i] << "\"";
  }
}

TEST(KeyCodecOrderTest, EnumOrderPreserved) {
  enum class MyEnum : int8_t { A = 1, B = 2, C = 3 };
  std::vector<MyEnum> values = {MyEnum::A, MyEnum::B, MyEnum::C};
  std::vector<Bytes> encoded;
  for (auto e : values) {
    Bytes out;
    EncodeField(e, out);
    encoded.push_back(out);
  }
  for (size_t i = 1; i < encoded.size(); ++i) {
    EXPECT_LT(encoded[i - 1], encoded[i]) << "Failed for enum order";
  }
}

TEST(KeyCodecOrderTest, TupleOrderPreserved) {
  enum class MyEnum : int8_t { A = 1, B = 2 };
  std::vector<std::tuple<int32_t, MyEnum>> values = {
      {1, MyEnum::A}, {1, MyEnum::B}, {2, MyEnum::A}, {2, MyEnum::B}};
  std::vector<Bytes> encoded;
  for (const auto& tup : values) {
    encoded.push_back(EncodeTuple(tup));
  }
  for (size_t i = 1; i < encoded.size(); ++i) {
    EXPECT_LT(encoded[i - 1], encoded[i]) << "Failed for tuple order";
  }
}

TEST(KeyCodecOrderTest, NegativeIntegerOrderPreserved) {
  std::vector<int32_t> values = {-100000, -100, -42, -2, -1, 1, 100};
  std::vector<Bytes> encoded;
  for (auto v : values) {
    Bytes out;
    WriteInteger(v, out);
    encoded.push_back(out);
  }
  for (size_t i = 1; i < encoded.size(); ++i) {
    EXPECT_LT(encoded[i - 1], encoded[i]) << "Failed for " << values[i - 1] << " < " << values[i];
  }
}
