#pragma once

#include "absl/status/status.h"
#include "gtest/gtest.h"

// Macros for testing absl::Status in tests
// These provide better error messages than raw ASSERT_TRUE(status.ok())

#define ASSERT_OK(expression)                                                               \
  do {                                                                                      \
    const absl::Status _status = (expression);                                              \
    if (!_status.ok()) {                                                                    \
      GTEST_FATAL_FAILURE_(("Expected OK status, but got: " + _status.ToString()).c_str()); \
    }                                                                                       \
  } while (0)

#define EXPECT_OK(expression)                                                                  \
  do {                                                                                         \
    const absl::Status _status = (expression);                                                 \
    if (!_status.ok()) {                                                                       \
      GTEST_NONFATAL_FAILURE_(("Expected OK status, but got: " + _status.ToString()).c_str()); \
    }                                                                                          \
  } while (0)

// For testing specific status codes
#define ASSERT_STATUS_EQ(expected_code, expression)                                             \
  do {                                                                                          \
    const absl::Status _status = (expression);                                                  \
    if (_status.code() != (expected_code)) {                                                    \
      GTEST_FATAL_FAILURE_(("Expected status code " + absl::StatusCodeToString(expected_code) + \
                            ", but got: " + _status.ToString())                                 \
                               .c_str());                                                       \
    }                                                                                           \
  } while (0)

#define EXPECT_STATUS_EQ(expected_code, expression)                                                \
  do {                                                                                             \
    const absl::Status _status = (expression);                                                     \
    if (_status.code() != (expected_code)) {                                                       \
      GTEST_NONFATAL_FAILURE_(("Expected status code " + absl::StatusCodeToString(expected_code) + \
                               ", but got: " + _status.ToString())                                 \
                                  .c_str());                                                       \
    }                                                                                              \
  } while (0)

// Convenience aliases for common status codes
#define ASSERT_NOT_FOUND(expression) ASSERT_STATUS_EQ(absl::StatusCode::kNotFound, expression)
#define EXPECT_NOT_FOUND(expression) EXPECT_STATUS_EQ(absl::StatusCode::kNotFound, expression)