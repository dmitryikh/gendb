#pragma once

#include <cmath>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "parse_text.h"

namespace gendb::tests {

// Matcher for comparing FlatBuffers messages with text representations
template <typename MessageType>
class MessageEqualMatcher : public ::testing::MatcherInterface<const MessageType&> {
 public:
  explicit MessageEqualMatcher(std::string_view expected_text, bool partial = false)
      : expected_text_(expected_text), partial_(partial) {}

  bool MatchAndExplain(const MessageType& actual,
                       ::testing::MatchResultListener* listener) const override {
    try {
      // Parse expected text into a message
      auto expected_buffer = ParseText<MessageType>(expected_text_);
      MessageType expected(expected_buffer);

      // Compare each field defined in the expected text
      std::vector<std::string> mismatches;
      std::vector<std::string> extra_fields;

      // Parse expected text to identify which fields should be compared
      std::set<std::string> expected_fields = ParseFieldNames(expected_text_);

      for (const auto& field_info : MessageType::kFieldsInfo) {
        std::string field_name{field_info.name};
        bool field_in_expected = expected_fields.count(field_name) > 0;

        if (!field_in_expected && !partial_) {
          // For exact matching, check if actual has fields not in expected
          if (actual.HasField(field_info.field_id)) {
            extra_fields.push_back(field_name);
          }
          continue;
        }

        if (!field_in_expected) {
          continue;  // Skip fields not mentioned in expected for partial matching
        }

        // Compare field values using type-specific comparisons
        bool fields_match = CompareFieldValue(actual, expected, field_info, listener);
        if (!fields_match) {
          mismatches.push_back(field_name);
        }
      }

      bool success = mismatches.empty() && (partial_ || extra_fields.empty());

      if (!success && listener->IsInterested()) {
        *listener << "Message comparison failed:";
        if (!mismatches.empty()) {
          *listener << "\nField mismatches: ";
          for (size_t i = 0; i < mismatches.size(); ++i) {
            if (i > 0) *listener << ", ";
            *listener << mismatches[i];
          }
        }
        if (!extra_fields.empty() && !partial_) {
          *listener << "\nUnexpected fields in actual: ";
          for (size_t i = 0; i < extra_fields.size(); ++i) {
            if (i > 0) *listener << ", ";
            *listener << extra_fields[i];
          }
        }
      }

      return success;
    } catch (const std::exception& e) {
      if (listener->IsInterested()) {
        *listener << "Failed to parse expected text: " << e.what();
      }
      return false;
    }
  }

  void DescribeTo(std::ostream* os) const override {
    *os << (partial_ ? "partially matches" : "exactly matches") << " message with fields:\n"
        << expected_text_;
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << (partial_ ? "does not partially match" : "does not exactly match")
        << " message with fields:\n"
        << expected_text_;
  }

 private:
  std::string expected_text_;
  bool partial_;

  std::set<std::string> ParseFieldNames(std::string_view text) const {
    std::set<std::string> field_names;
    std::string text_str{text};
    std::istringstream stream{text_str};
    std::string line;

    while (std::getline(stream, line)) {
      ParsedField field = ParseLine(line);
      if (field.valid) {
        field_names.insert(field.name);
      }
    }
    return field_names;
  }

  // Compare two field values using serialization to binary representation
  bool CompareFieldValue(const MessageType& actual, const MessageType& expected,
                         const gendb::FieldInfo& field_info,
                         ::testing::MatchResultListener* listener) const {
    // Check if both messages have the field
    bool actual_has = actual.HasField(field_info.field_id);
    bool expected_has = expected.HasField(field_info.field_id);

    if (actual_has != expected_has) {
      if (listener->IsInterested()) {
        std::string field_name{field_info.name};
        *listener << "\n  " << field_name << ": field presence mismatch - " << "actual "
                  << (actual_has ? "has" : "missing") << ", " << "expected "
                  << (expected_has ? "has" : "missing");
      }
      return false;
    }

    if (!actual_has) {
      return true;  // Both don't have the field
    }

    // Get raw field data for comparison
    auto actual_raw = actual.FieldRaw(field_info.field_id);
    auto expected_raw = expected.FieldRaw(field_info.field_id);

    bool match = (actual_raw.size() == expected_raw.size()) &&
                 std::equal(actual_raw.begin(), actual_raw.end(), expected_raw.begin());

    if (!match && listener->IsInterested()) {
      std::string field_name{field_info.name};
      *listener << "\n  " << field_name << ": field values differ";
      // For better debugging, we could add specific type-based value printing here
    }

    return match;
  }
};

// Factory functions for creating matchers
template <typename MessageType>
::testing::Matcher<const MessageType&> MessageEqual(std::string_view expected_text) {
  return ::testing::MakeMatcher(new MessageEqualMatcher<MessageType>(expected_text, false));
}

template <typename MessageType>
::testing::Matcher<const MessageType&> MessageEqualPartially(std::string_view expected_text) {
  return ::testing::MakeMatcher(new MessageEqualMatcher<MessageType>(expected_text, true));
}

}  // namespace gendb::tests