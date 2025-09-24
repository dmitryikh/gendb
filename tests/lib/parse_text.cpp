#include "parse_text.h"

#include <algorithm>

namespace gendb::tests {

std::string ParseStringValue(const std::string& value) {
  std::string result = value;
  // Remove quotes if present
  if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
    result = result.substr(1, result.size() - 2);
  }
  return result;
}

int64_t ParseIntValue(const std::string& value) {
  try {
    return std::stoll(value);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid integer value: " + value);
  }
}

uint64_t ParseUIntValue(const std::string& value) {
  try {
    return std::stoull(value);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid unsigned integer value: " + value);
  }
}

float ParseFloatValue(const std::string& value) {
  try {
    return std::stof(value);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid float value: " + value);
  }
}

double ParseDoubleValue(const std::string& value) {
  try {
    return std::stod(value);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid double value: " + value);
  }
}

bool ParseBoolValue(const std::string& value) {
  std::string lower_value = value;
  std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);

  if (lower_value == "true" || lower_value == "1") {
    return true;
  } else if (lower_value == "false" || lower_value == "0") {
    return false;
  } else {
    throw std::runtime_error("Invalid bool value: " + value);
  }
}

ParsedField ParseLine(const std::string& line) {
  // Skip empty lines and lines that are just whitespace
  if (line.empty() ||
      std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); })) {
    return {"", "", false};
  }

  // Find the colon separator
  size_t colon_pos = line.find(':');
  if (colon_pos == std::string::npos) {
    return {"", "", false};  // Skip malformed lines
  }

  // Extract field name and value
  std::string field_name = line.substr(0, colon_pos);
  std::string value = line.substr(colon_pos + 1);

  // Trim whitespace
  field_name.erase(0, field_name.find_first_not_of(" \t"));
  field_name.erase(field_name.find_last_not_of(" \t") + 1);
  value.erase(0, value.find_first_not_of(" \t"));
  value.erase(value.find_last_not_of(" \t") + 1);

  if (field_name.empty()) {
    return {"", "", false};
  }

  return {field_name, value, true};
}

}  // namespace gendb::tests