#pragma once

#include <cctype>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gendb/message_builder.h"

namespace gendb::tests {

// ParseText function that parses a simple text format into message objects
// Format:
//   field_name: value
//   another_field: "string value"
//   bool_field: true
//
// Usage:
//   auto buffer = ParseText<Account>(R"m(
//     account_id: 42
//     name: "John"
//     is_active: true
//   )m");
//   Account account(buffer);

// Helper functions for parsing values
std::string ParseStringValue(const std::string& value);
int64_t ParseIntValue(const std::string& value);
uint64_t ParseUIntValue(const std::string& value);
float ParseFloatValue(const std::string& value);
double ParseDoubleValue(const std::string& value);
bool ParseBoolValue(const std::string& value);

// Helper struct for parsed fields (internal use)
struct ParsedField {
  std::string name;
  std::string value;
  bool valid;
};
ParsedField ParseLine(const std::string& line);

// Helper to parse enum values from string based on MessageType::FieldInfo
template <typename MessageType>
int ParseEnumValueGeneric(const typename MessageType::FieldInfo& field_info,
                          std::string_view value) {
  if (field_info.enum_values == nullptr || field_info.enum_values_count == 0) {
    throw std::runtime_error("No enum values available for field: " + std::string(field_info.name));
  }

  std::string value_str(value);
  for (size_t i = 0; i < field_info.enum_values_count; ++i) {
    if (value_str == field_info.enum_values[i].name) {
      return field_info.enum_values[i].value;
    }
  }

  throw std::runtime_error("Invalid " + std::string(field_info.enum_name) + " value: " + value_str);
}

// Fully generic ParseText implementation using MessageBuilder and reflection
template <typename MessageType>
std::vector<uint8_t> ParseText(std::string_view text_data) {
  gendb::MessageBuilder builder;

  std::string text_str{text_data};
  std::istringstream stream{text_str};
  std::string line;

  while (std::getline(stream, line)) {
    ParsedField field = ParseLine(line);
    if (!field.valid) continue;

    // Find the field in the reflection info
    bool field_found = false;
    for (const auto& field_info : MessageType::kFieldsInfo) {
      if (field.name == field_info.name) {
        field_found = true;

        // Set the field based on its type using generic MessageBuilder
        switch (field_info.type) {
          case MessageType::FieldInfo::STRING: {
            std::string str_value = ParseStringValue(field.value);
            builder.AddStringField(field_info.field_id, str_value);
            break;
          }
          case MessageType::FieldInfo::SCALAR: {
            switch (field_info.scalar_type) {
              case MessageType::FieldInfo::UINT64:
                builder.AddField<uint64_t>(field_info.field_id, ParseUIntValue(field.value));
                break;
              case MessageType::FieldInfo::INT32:
                builder.AddField<int32_t>(field_info.field_id,
                                          static_cast<int32_t>(ParseIntValue(field.value)));
                break;
              case MessageType::FieldInfo::BOOL:
                builder.AddField<bool>(field_info.field_id, ParseBoolValue(field.value));
                break;
              case MessageType::FieldInfo::FLOAT:
                builder.AddField<float>(field_info.field_id, ParseFloatValue(field.value));
                break;
              default:
                throw std::runtime_error("Unsupported scalar type for field: " + field.name);
            }
            break;
          }
          case MessageType::FieldInfo::ENUM: {
            int enum_value =
                ParseEnumValueGeneric<MessageType>(field_info, ParseStringValue(field.value));
            // Enums are typically stored as uint32_t
            builder.AddField<uint32_t>(field_info.field_id, static_cast<uint32_t>(enum_value));
            break;
          }
        }
        break;
      }
    }

    if (!field_found) {
      throw std::runtime_error("Unknown field: " + field.name);
    }
  }

  return builder.Build();
}

}  // namespace gendb::tests