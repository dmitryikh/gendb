#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

// The macros below are inspired by code form <google/protobuf/stubs/status_macros.h>

#define RETURN_IF_ERROR(expr)                                                \
  do {                                                                       \
    /* Using _status below to avoid capture problems if expr is "status". */ \
    absl::Status return_if_error_status = (expr);                            \
    if (!return_if_error_status.ok()) [[unlikely]]                           \
      return return_if_error_status;                                         \
  } while (0)