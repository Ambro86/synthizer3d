// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "commandlineflags.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <utility>

#include "../src/string_util.h"

namespace benchmark {
namespace {

// Parses 'str' for a 32-bit signed integer.  If successful, writes
// the result to *value and returns true; otherwise leaves *value
// unchanged and returns false.
bool ParseInt32(const std::string& src_text, const char* str, int32_t* value) {
  // Parses the environment variable as a decimal integer.
  char* end = nullptr;
  const long long_value = strtol(str, &end, 10);  // NOLINT

  // Has strtol() consumed all characters in the string?
  if (*end != '\0') {
    // No - an invalid character was encountered.
    std::cerr << src_text << " is expected to be a 32-bit integer, "
              << "but actually has value \"" << str << "\".\n";
    return false;
  }

  // Is the parsed value in the range of an Int32?
  const int32_t result = static_cast<int32_t>(long_value);
  if (long_value == std::numeric_limits<long>::max() ||
      long_value == std::numeric_limits<long>::min() ||
      // The parsed value overflows as a long.  (strtol() returns
      // LONG_MAX or LONG_MIN when the input overflows.)
      result != long_value
      // The parsed value overflows as an Int32.
  ) {
    std::cerr << src_text << " is expected to be a 32-bit integer, "
              << "but actually has value \"" << str << "\", "
              << "which overflows.\n";
    return false;
  }

  *value = result;
  return true;
}

// Parses 'str' for a double.  If successful, writes the result to *value and
// returns true; otherwise leaves *value unchanged and returns false.
bool ParseDouble(const std::string& src_text, const char* str, double* value) {
  // Parses the environment variable as a decimal integer.
  char* end = nullptr;
  const double double_value = strtod(str, &end);  // NOLINT

  // Has strtol() consumed all characters in the string?
  if (*end != '\0') {
    // No - an invalid character was encountered.
    std::cerr << src_text << " is expected to be a double, "
              << "but actually has value \"" << str << "\".\n";
    return false;
  }

  *value = double_value;
  return true;
}

// Parses 'str' into KV pairs. If successful, writes the result to *value and
// returns true; otherwise leaves *value unchanged and returns false.
bool ParseKvPairs(const std::string& src_text, const char* str,
                  std::map<std::string, std::string>* value) {
  std::map<std::string, std::string> kvs;
  for (const auto& kvpair : StrSplit(str, ',')) {
    const auto kv = StrSplit(kvpair, '=');
    if (kv.size() != 2) {
      std::cerr << src_text << " is expected to be a comma-separated list of "
                << "<key>=<value> strings, but actually has value \"" << str
                << "\".\n";
      return false;
    }
    if (!kvs.emplace(kv[0], kv[1]).second) {
      std::cerr << src_text << " is expected to contain unique keys but key \""
                << kv[0] << "\" was repeated.\n";
      return false;
    }
  }

  *value = kvs;
  return true;
}

// Returns the name of the environment variable corresponding to the
// given flag.  For example, FlagToEnvVar("foo") will return
// "BENCHMARK_FOO" in the open-source version.
static std::string FlagToEnvVar(const char* flag) {
  const std::string flag_str(flag);

  std::string env_var;
  for (size_t i = 0; i != flag_str.length(); ++i)
    env_var += static_cast<char>(::toupper(flag_str.c_str()[i]));

  return env_var;
}

}  // namespace

bool BoolFromEnv(const char* flag, bool default_val) {
  const std::string env_var = FlagToEnvVar(flag);
  const char* const value_str = getenv(env_var.c_str());
  return value_str == nullptr ? default_val : IsTruthyFlagValue(value_str);
}

int32_t Int32FromEnv(const char* flag, int32_t default_val) {
  const std::string env_var = FlagToEnvVar(flag);
  const char* const value_str = getenv(env_var.c_str());
  int32_t value = default_val;
  if (value_str == nullptr ||
      !ParseInt32(std::string("Environment variable ") + env_var, value_str,
                  &value)) {
    return default_val;
  }
  return value;
}

double DoubleFromEnv(const char* flag, double default_val) {
  const std::string env_var = FlagToEnvVar(flag);
  const char* const value_str = getenv(env_var.c_str());
  double value = default_val;
  if (value_str == nullptr ||
      !ParseDouble(std::string("Environment variable ") + env_var, value_str,
                   &value)) {
    return default_val;
  }
  return value;
}

const char* StringFromEnv(const char* flag, const char* default_val) {
  const std::string env_var = FlagToEnvVar(flag);
  const char* const value = getenv(env_var.c_str());
  return value == nullptr ? default_val : value;
}

std::map<std::string, std::string> KvPairsFromEnv(
    const char* flag, std::map<std::string, std::string> default_val) {
  const std::string env_var = FlagToEnvVar(flag);
  const char* const value_str = getenv(env_var.c_str());

  if (value_str == nullptr) return default_val;

  std::map<std::string, std::string> value;
  if (!ParseKvPairs("Environment variable " + env_var, value_str, &value)) {
    return default_val;
  }
  return value;
}

// Parses a string as a command line flag.  The string should have
// the format "--flag=value".  When def_optional is true, the "=value"
// part can be omitted.
//
// Returns the value of the flag, or nullptr if the parsing failed.
const char* ParseFlagValue(const char* str, const char* flag,
                           bool def_optional) {
  // str and flag must not be nullptr.
  if (str == nullptr || flag == nullptr) return nullptr;

  // The flag must start with "--".
  const std::string flag_str = std::string("--") + std::string(flag);
  const size_t flag_len = flag_str.length();
  if (strncmp(str, flag_str.c_str(), flag_len) != 0) return nullptr;

  // Skips the flag name.
  const char* flag_end = str + flag_len;

  // When def_optional is true, it's OK to not have a "=value" part.
  if (def_optional && (flag_end[0] == '\0')) return flag_end;

  // If def_optional is true and there are more characters after the
  // flag name, or if def_optional is false, there must be a '=' after
  // the flag name.
  if (flag_end[0] != '=') return nullptr;

  // Returns the string after "=".
  return flag_end + 1;
}

bool ParseBoolFlag(const char* str, const char* flag, bool* value) {
  // Gets the value of the flag as a string.
  const char* const value_str = ParseFlagValue(str, flag, true);

  // Aborts if the parsing failed.
  if (value_str == nullptr) return false;

  // Converts the string value to a bool.
  *value = IsTruthyFlagValue(value_str);
  return true;
}

bool ParseInt32Flag(const char* str, const char* flag, int32_t* value) {
  // Gets the value of the flag as a string.
  const char* const value_str = ParseFlagValue(str, flag, false);

  // Aborts if the parsing failed.
  if (value_str == nullptr) return false;

  // Sets *value to the value of the flag.
  return ParseInt32(std::string("The value of flag --") + flag, value_str,
                    value);
}

bool ParseDoubleFlag(const char* str, const char* flag, double* value) {
  // Gets the value of the flag as a string.
  const char* const value_str = ParseFlagValue(str, flag, false);

  // Aborts if the parsing failed.
  if (value_str == nullptr) return false;

  // Sets *value to the value of the flag.
  return ParseDouble(std::string("The value of flag --") + flag, value_str,
                     value);
}

bool ParseStringFlag(const char* str, const char* flag, std::string* value) {
  // Gets the value of the flag as a string.
  const char* const value_str = ParseFlagValue(str, flag, false);

  // Aborts if the parsing failed.
  if (value_str == nullptr) return false;

  *value = value_str;
  return true;
}

bool ParseKeyValueFlag(const char* str, const char* flag,
                       std::map<std::string, std::string>* value) {
  const char* const value_str = ParseFlagValue(str, flag, false);

  if (value_str == nullptr) return false;

  for (const auto& kvpair : StrSplit(value_str, ',')) {
    const auto kv = StrSplit(kvpair, '=');
    if (kv.size() != 2) return false;
    value->emplace(kv[0], kv[1]);
  }

  return true;
}

bool IsFlag(const char* str, const char* flag) {
  return (ParseFlagValue(str, flag, true) != nullptr);
}

bool IsTruthyFlagValue(const std::string& value) {
  if (value.size() == 1) {
    char v = value[0];
    return isalnum(v) &&
           !(v == '0' || v == 'f' || v == 'F' || v == 'n' || v == 'N');
  } else if (!value.empty()) {
    std::string value_lower(value);
    std::transform(value_lower.begin(), value_lower.end(), value_lower.begin(),
                   [](char c) { return static_cast<char>(::tolower(c)); });
    return !(value_lower == "false" || value_lower == "no" ||
             value_lower == "off");
  } else
    return true;
}

}  // end namespace benchmark
