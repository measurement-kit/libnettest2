// Part of Measurement Kit <https://measurement-kit.github.io/>.
// Measurement Kit is free software under the BSD license. See AUTHORS
// and LICENSE for more information on the copying conditions.
#ifndef MEASUREMENT_KIT_LIBNETTEST2_HPP
#define MEASUREMENT_KIT_LIBNETTEST2_HPP

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <math.h>
#ifndef _WIN32
#include <netdb.h>
#endif
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <maxminddb.h>

// TODO(bassosimone): add documentation and restructure code such that what
// does not need to be documented and processed by a user lies below a specific
// line like we currently do for libndt.

// TODO(bassosimone): add support for telling cURL which CA to use. We will
// need this when we use this code with the mobile apps.

// Check dependencies
// ``````````````````

// TODO(bassosimone): make sure we can serialize a JSON. Specifically, there
// may be cases where the JSON input is not UTF-8 and, in such cases, the JSON
// library that we use will throw an exception.

#ifndef NLOHMANN_JSON_HPP
#error "Please include nlohmann/json before including this header"
#endif  // !NLOHMANN_JSON_HPP

#ifndef DATE_H
#error "Please include HowardHinnant/date before including this header"
#endif  // !DATE_H

namespace measurement_kit {
namespace libnettest2 {

constexpr const char *default_engine_name() noexcept {
  return "libnettest2";
}

// Versioning
// ``````````

/// Type containing a version number.
using Version = unsigned int;

/// Major API version number of measurement-kit/libnettest2.
constexpr Version version_major = Version{0};

/// Minor API version number of measurement-kit/libnettest2.
constexpr Version version_minor = Version{6};

/// Patch API version number of measurement-kit/libnettest2.
constexpr Version version_patch = Version{0};

/// Returns a string reresentation of the version
inline std::string version() noexcept {
  std::stringstream ss;
  ss << version_major << "." << version_minor << "." << version_patch;
  return ss.str();
}

// Timeout
// ```````

using Timeout = uint16_t;
constexpr Timeout TimeoutDefault = 90;

// Log level
// `````````

// Important: LogLevel MUST be uint32_t and its values MUST NOT be changed
// because the current values make the code binary compatible with MK.
enum class LogLevel : uint32_t {
  log_quiet = 0,
  log_err = 1,
  log_warning = 2,
  log_info = 3,
  log_debug = 4,
  log_debug2 = 5,
};

// Errors
// ``````

class ErrContext {
 public:
  int64_t code = 1;  // Set to nonzero because often zero means success
  std::string library_name;
  std::string library_version;
  std::string reason;
};

// Implementation note: PLEASE NEVER REMOVE LINES SUCH THAT WE CAN KEEP A
// STABLE ERROR RELATED ABI FOREVER WITH ZERO MAINTENANCE COSTS.
#define LIBNETTEST2_ENUM_OWN_ERRORS(XX) \
  XX(none)                              \
  XX(mmdb_enoent)                       \
  XX(mmdb_enodatafortype)

// Inherit from int64_t such that we can safely cast to int64_t when we
// are using a value of this enum to initialize a ErrContext::value.
enum class Errors : int64_t {
#define XX(e_) e_,
  LIBNETTEST2_ENUM_OWN_ERRORS(XX)
#undef XX
};

const char *libnettest2_strerror(Errors n) noexcept;

// Settings
// ````````

class Settings {
 public:
  //
  // top-level settings
  //
  std::map<std::string, std::string> annotations;
  std::vector<std::string> inputs;
  std::vector<std::string> input_filepaths;
  std::string log_filepath;
  // Note: the user passes us a string and we map such string into the
  // values of the LogLevel enumeration.
  LogLevel log_level = LogLevel::log_warning;
  std::string name;
  std::string output_filepath;
  //
  // settings inside the 'options' sub-dictionary
  //
  bool all_endpoints = false;
  std::string bouncer_base_url = "https://bouncer.ooni.io";
  std::string ca_bundle_path;
  std::string collector_base_url;
  std::string engine_name = default_engine_name();
  std::string engine_version = version();
  std::string engine_version_full = version();
  std::string geoip_asn_path;
  std::string geoip_country_path;
  Timeout max_runtime = TimeoutDefault;
  bool no_asn_lookup = false;
  bool no_bouncer = false;
  bool no_cc_lookup = false;
  bool no_collector = false;
  bool no_file_report = false;
  bool no_ip_lookup = false;
  bool no_resolver_lookup = false;
  uint8_t parallelism = 0;
  std::string platform;
  uint16_t port = 0;
  std::string probe_ip;
  std::string probe_asn;
  std::string probe_network_name;
  std::string probe_cc;
  bool randomize_input = true;
  bool save_real_probe_asn = true;
  bool save_real_probe_ip = false;
  bool save_real_probe_cc = true;
  bool save_real_resolver_ip = true;
  std::string server;
  std::string software_name = default_engine_name();
  std::string software_version = version();
};

bool parse_settings(std::string str, Settings *settings,
                    std::string *err) noexcept;

// EndpointInfo
// ````````````

using EndpointType = uint8_t;
constexpr EndpointType endpoint_type_none = EndpointType{0};
constexpr EndpointType endpoint_type_onion = EndpointType{1};
constexpr EndpointType endpoint_type_cloudfront = EndpointType{2};
constexpr EndpointType endpoint_type_https = EndpointType{3};

class EndpointInfo {
 public:
  EndpointType type = endpoint_type_none;
  std::string address;
  std::string front;  // Only valid for endpoint_type_cloudfront
};

// Nettest context
// ```````````````

class NettestContext {
 public:
  std::vector<EndpointInfo> collectors;
  std::string probe_asn;
  std::string probe_cc;
  std::string probe_ip;
  std::string probe_network_name;
  std::string report_id;
  std::string resolver_ip;
  std::map<std::string, std::vector<EndpointInfo>> test_helpers;
};

// Nettest
// ```````

class BytesInfo {
 public:
  // Implementation note: we use unsigned arithmetic here and accept the
  // fact that, if we transfer a very huge amount of data (unlikely for
  // all our tests), we will wrap around. Signed types are guaranteed to
  // wrap around. See <https://stackoverflow.com/a/10011488/4354461>.
  std::atomic<uint64_t> bytes_down{0};
  std::atomic<uint64_t> bytes_up{0};
};

class Nettest {
 public:
  virtual std::string name() const noexcept;

  virtual std::vector<std::string> test_helpers() const noexcept;

  virtual std::string version() const noexcept;

  virtual bool needs_input() const noexcept;

  virtual bool run(const Settings &settings,
                   const NettestContext &context,
                   std::string input,
                   nlohmann::json *test_keys,
                   BytesInfo *info) noexcept;

  virtual ~Nettest() noexcept;
};

// Runner
// ``````

class Runner {
 public:
  Runner(const Settings &settings, Nettest &nettest) noexcept;

  Runner(const Runner &) noexcept = delete;
  Runner &operator=(const Runner &) noexcept = delete;
  Runner(Runner &&) noexcept = delete;
  Runner &operator=(Runner &&) noexcept = delete;

  virtual ~Runner() noexcept;

  bool run() noexcept;

  void interrupt() noexcept;

  LogLevel get_log_level() const noexcept;

 protected:
  // Methods you typically want to override
  // ``````````````````````````````````````
  // The on_event() method is called when a event occurs. Note that this
  // method MAY be called from another thread context.

  virtual void on_event(const nlohmann::json &event) const noexcept;

  // Methods you generally DON'T want to override
  // ````````````````````````````````````````````
  // You may want to override them in unit tests, however.

 public:
  virtual void emit_ev(std::string key, nlohmann::json value) const noexcept;

  class BytesInfoWrapper {
   public:
    const Runner *owner = nullptr;
    BytesInfo *info = nullptr;
  };

 protected:
  virtual bool run_with_index32(
      const std::chrono::time_point<std::chrono::steady_clock> &begin,
      const std::string &test_start_time,
      const std::vector<std::string> &inputs, const NettestContext &ctx,
      const std::string &collector_base_url, uint32_t i,
      BytesInfo *info) const noexcept;

  virtual bool query_bouncer(std::string nettest_name,
                             std::vector<std::string> nettest_helper_names,
                             std::string nettest_version,
                             std::vector<EndpointInfo> *collectors,
                             std::map<std::string,
                               std::vector<EndpointInfo>> *helpers,
                             BytesInfo *info,
                             ErrContext *err) noexcept;

  virtual bool lookup_ip(std::string *ip, BytesInfo *info,
                         ErrContext *err) noexcept;

  virtual bool lookup_resolver_ip(std::string *ip, BytesInfo *info,
                                  ErrContext *err) noexcept;

  virtual bool open_report(const std::string &collector_base_url,
                           const std::string &test_start_time,
                           const NettestContext &context,
                           std::string *report_id,
                           BytesInfo *info, ErrContext *err) noexcept;

  virtual bool update_report(const std::string &collector_base_url,
                             const std::string &report_id,
                             const std::string &json_str,
                             BytesInfo *info, ErrContext *err) const noexcept;

  virtual bool close_report(const std::string &collector_base_url,
                            const std::string &report_id,
                            BytesInfo *info, ErrContext *err) noexcept;

  // MaxMindDB code
  // ``````````````

  virtual bool lookup_asn(const std::string &dbpath, const std::string &ip,
                          std::string *asn, std::string *network_name,
                          ErrContext *err) noexcept;

  virtual bool lookup_cc(const std::string &dbpath, const std::string &probe_ip,
                         std::string *cc, ErrContext *err) noexcept;

  // cURL code
  // `````````

  class CurlxDeleter {
   public:
    void operator()(CURL *handle) noexcept;
  };
  using UniqueCurlx = std::unique_ptr<CURL, CurlxDeleter>;

  virtual bool curlx_post_json(std::string url,
                               std::string requestbody,
                               long timeout,
                               std::string *responsebody,
                               BytesInfo *info,
                               ErrContext *err) const noexcept;

  virtual bool curlx_get(std::string url,
                         long timeout,
                         std::string *responsebody,
                         BytesInfo *info,
                         ErrContext *err) noexcept;

  virtual bool curlx_common(UniqueCurlx &handle,
                            std::string url,
                            long timeout,
                            std::string *responsebody,
                            BytesInfo *info,
                            ErrContext *err) const noexcept;

 private:
  // Private attributes
  // ``````````````````

  std::atomic_bool interrupted_{false};

  Nettest &nettest_;

  const Settings &settings_;
};

// Implementation section
// ``````````````````````
// This is a single header library. In some use cases you may want to split
// the interface and implementation using LIBNETTEST2_NO_INLINE_IMPL.
#ifndef LIBNETTEST2_NO_INLINE_IMPL

// Errors
// ``````

const char *libnettest2_strerror(Errors n) noexcept {
#define XX(e_) case Errors::e_: return #e_;
  switch (n) {
    LIBNETTEST2_ENUM_OWN_ERRORS(XX)
  }
#undef XX
  return "invalid_argument";
}

// JSON parsing of settings
// ````````````````````````

// describe_pointer<T> and DESCRIBE_POINTER are helpers designed to allow
// us to tell the user the expected type of a variable.
template <typename Type> struct describe_pointer;

using StringStringMap = std::map<std::string, std::string>;

#define DESCRIBE_POINTER(T, V)                   \
  template <>                                    \
  struct describe_pointer<T> {                   \
    static constexpr const char *type_name = V;  \
  }

DESCRIBE_POINTER(bool, "bool");
DESCRIBE_POINTER(double, "double");
DESCRIBE_POINTER(StringStringMap, "std::map<std::string, std::string>");
DESCRIBE_POINTER(std::vector<std::string>, "std::vector<std::string>");
DESCRIBE_POINTER(std::string, "std::string");
DESCRIBE_POINTER(uint8_t, "uint8_t");
DESCRIBE_POINTER(uint16_t, "uint16_t");

#undef DESCRIBE_POINTER  // Tidy

template <typename Type>
bool json_maybe_get(const nlohmann::json &doc, std::string ptr_string,
                    Type *value, std::string *err) noexcept {
  if (value == nullptr || err == nullptr) {
    return false;
  }
  nlohmann::json::json_pointer pointer{ptr_string};
  *value = {};
  nlohmann::json entry;
  try {
    entry = doc.at(pointer);
  } catch (const std::exception &exc) {
    return true;  // No entry? It's okay because we're maybe_get() not get()
  }
  try {
    *value = entry.get<Type>();
  } catch (const std::exception &) {
    std::stringstream ss;
    ss << "invalid_settings_error: cannot convert variable accessed using "
       << "'" << ptr_string << "' as JSON pointer from JSON type '"
       << entry.type_name() << "' to C++ type '"
       << describe_pointer<Type>::type_name << "'";
    *err = ss.str();
    return false;
  }
  return true;
}

template <typename Type>
std::string out_of_range_error_gen(
    const std::string &ptr, Type minimum, Type maximum) noexcept {
  std::stringstream ss;
  ss << "invalid_settings_error: cannot validate variable accessed using "
     << "'" << ptr << "' because the value is out of range (The "
     << "minimum acceptable value is " << minimum << " while the maximum "
     << "acceptable value is " << maximum << ")";
  return ss.str();
}

static std::string format_error_gen(const std::string &ptr) noexcept {
  std::stringstream ss;
  ss << "invalid_settings_error: cannot validate variable accessed using "
     << "'" << ptr << "' because the variable should be an integer "
     << "but you actually provided a floating point number";
  return ss.str();
}

bool parse_settings(std::string str, Settings *settings,
                    std::string *err, std::string *warn) noexcept {
  if (settings == nullptr || err == nullptr) {
    return false;
  }
  // Note: warn is currently only used by the feature allowing a app to pass
  // us a number to be treated like a boolean value.
  if (warn == nullptr) {
    return false;
  }
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(str);
  } catch (const std::exception &) {
    *err = "json_parse_error";
    return false;
  }
  if (!doc.is_object()) {
    *err = "invalid_settings_error: JSON document is not an object";
    return false;
  }
  if (doc.count("options") <= 0) {
    *err = "invalid_settings_error: missing 'options' entry";
    return false;
  }
  if (!doc.at("options").is_object()) {
    *err = "invalid_settings_error: 'options' entry is not an object";
    return false;
  }
  if (doc.count("name") <= 0) {
    *err = "invalid_settings_error: missing 'name' entry";
    return false;
  }
  if (!doc.at("name").is_string()) {
    *err = "invalid_settings_error: 'name' entry is not a string";
    return false;
  }
  //
  // MAYBE_GET allows to get a maximum-range variable. We have specific macros
  // to take care of variables with a reduced range (e.g. uint16_t). The reason
  // why there are such specific macros is that nlohmann::json will truncate
  // values when casting from a wider to a smaller range. So, to avoid any kind
  // of issue, integers can only be read with explicit macros.
  //
  // Additionally, for some time we'll keep a layer of backwards compatibility
  // with which we can treat numbers as booleans. This is necessary because
  // up until MK v0.9.0-alpha.9 we were using integers as booleans.
  //
#define MAYBE_GET(path, variable)                                       \
  do {                                                                  \
    static_assert(std::is_same<std::add_pointer<double>::type,          \
                               decltype(variable)>::value ||            \
                      std::is_same<std::add_pointer<                    \
                                       std::map<std::string,            \
                                                std::string>>::type,    \
                                   decltype(variable)>::value ||        \
                      std::is_same<std::add_pointer<                    \
                                       std::vector<std::string>>::type, \
                                   decltype(variable)>::value ||        \
                      std::is_same<std::add_pointer<std::string>::type, \
                                   decltype(variable)>::value,          \
                  "MAYBE_GET() passed an invalid argument");            \
    if (!json_maybe_get(doc, path, variable, err)) {                    \
      return false;                                                     \
    }                                                                   \
  } while (0)
  //
  // MAYBE_GET_BOOL is a backwards compatible layer designed to allow
  // to interpret numbers as booleans. If parsing a boolean fails, it
  // tries to parse a double and treats the double as boolean by setting
  // the result to false if it's 0.0 and to true otherwise.
  //
#define MAYBE_GET_BOOL(path, variable)                            \
  do {                                                            \
    static_assert(std::is_same<std::add_pointer<bool>::type,      \
                               decltype(variable)>::value,        \
                  "MAYBE_GET_BOOL() passed an invalid argument"); \
    if (!json_maybe_get(doc, path, variable, err)) {              \
      double scratch = {};                                        \
      std::string errx;                                           \
      if (!json_maybe_get(doc, path, &scratch, &errx)) {          \
        return false;                                             \
      }                                                           \
      *err = "";  /* Forget about error, we sorted it out */      \
      {                                                           \
        std::stringstream ss;                                     \
        ss << "Found number variable at '" << path << "' and "    \
           << "treating it as boolean. This is for backward "     \
           << "compatibility with MK <= 0.9.0-alpha.9 where we "  \
           << "did not allow boolean variables. Change your "     \
           << "code to use boolean to get rid of this warning. "  \
           << "Be aware that we will remove this backward "       \
           << "compatibility hack in the future, so change your " \
           << "code today to avoid your app breaking sometime "   \
           << "in the future. Please!";                           \
        *warn = ss.str();                                         \
      }                                                           \
      *variable = (scratch != 0.0);                               \
    }                                                             \
  } while (0)
  //
  // MAYBE_GET_UINT8 is specifically designed for uint8_t.
  //
#define MAYBE_GET_UINT8(path, variable)                            \
  do {                                                             \
    static_assert(std::is_same<std::add_pointer<uint8_t>::type,    \
                               decltype(variable)>::value,         \
                  "MAYBE_GET_UINT8() passed an invalid argument"); \
    double scratch = {};                                           \
    if (!json_maybe_get(doc, path, &scratch, err)) {               \
      return false;                                                \
    }                                                              \
    double unused = {};                                            \
    if (modf(scratch, &unused) != 0.0) {                           \
      *err = format_error_gen(path);                               \
      return false;                                                \
    }                                                              \
    if (scratch < 0.0 || scratch > UINT8_MAX) {                    \
      *err = out_of_range_error_gen(path, 0, UINT8_MAX);           \
      return false;                                                \
    }                                                              \
    *variable = (uint8_t)scratch;                                  \
  } while (0)
  //
  // MAYBE_GET_UINT16 is specifically designed for uint16_t.
  //
#define MAYBE_GET_UINT16(path, variable)                            \
  do {                                                              \
    static_assert(std::is_same<std::add_pointer<uint16_t>::type,    \
                               decltype(variable)>::value,          \
                  "MAYBE_GET_UINT16() passed an invalid argument"); \
    double scratch = {};                                            \
    if (!json_maybe_get(doc, path, &scratch, err)) {                \
      return false;                                                 \
    }                                                               \
    double unused = {};                                             \
    if (modf(scratch, &unused) != 0.0) {                            \
      *err = format_error_gen(path);                                \
      return false;                                                 \
    }                                                               \
    if (scratch < 0.0 || scratch > UINT16_MAX) {                    \
      *err = out_of_range_error_gen(path, 0, UINT16_MAX);           \
      return false;                                                 \
    }                                                               \
    *variable = (uint16_t)scratch;                                  \
  } while (0)
  //
  MAYBE_GET("/annotations", &settings->annotations);
  MAYBE_GET("/inputs", &settings->inputs);
  MAYBE_GET("/input_filepaths", &settings->input_filepaths);
  MAYBE_GET("/log_filepath", &settings->log_filepath);
  {
    std::string s;
    MAYBE_GET("/log_level", &s);
    if (s == "") {
      // NOTHING
    } else if (s == "QUIET") {
      settings->log_level = LogLevel::log_quiet;
    } else if (s == "ERR") {
      settings->log_level = LogLevel::log_err;
    } else if (s == "WARNING") {
      settings->log_level = LogLevel::log_warning;
    } else if (s == "INFO") {
      settings->log_level = LogLevel::log_info;
    } else if (s == "DEBUG") {
      settings->log_level = LogLevel::log_debug;
    } else if (s == "DEBUG2") {
      settings->log_level = LogLevel::log_debug2;
    } else {
      std::stringstream ss;
      ss << "invalid_settings_error: cannot convert variable accessed using "
         << "'/log_level' as JSON pointer to a C++ enumeration containing "
         << "one of: QUIET, ERR, WARNING, INFO, DEBUG, DEBUG2";
      *err = ss.str();
      return false;
    }
  }
  MAYBE_GET("/name", &settings->name);
  MAYBE_GET("/output_filepath", &settings->output_filepath);
  //
  MAYBE_GET_BOOL("/options/all_endpoints", &settings->all_endpoints);
  MAYBE_GET("/options/bouncer_base_url", &settings->bouncer_base_url);
  MAYBE_GET("/options/ca_bundle_path", &settings->ca_bundle_path);
  MAYBE_GET("/options/collector_base_url", &settings->collector_base_url);
  MAYBE_GET("/options/engine_name", &settings->engine_name);
  MAYBE_GET("/options/engine_version", &settings->engine_version);
  MAYBE_GET("/options/engine_version_full", &settings->engine_version_full);
  MAYBE_GET("/options/geoip_asn_path", &settings->geoip_asn_path);
  MAYBE_GET("/options/geoip_country_path", &settings->geoip_country_path);
  MAYBE_GET_UINT16("/options/max_runtime", &settings->max_runtime);
  MAYBE_GET_BOOL("/options/no_asn_lookup", &settings->no_asn_lookup);
  MAYBE_GET_BOOL("/options/no_bouncer", &settings->no_bouncer);
  MAYBE_GET_BOOL("/options/no_cc_lookup", &settings->no_cc_lookup);
  MAYBE_GET_BOOL("/options/no_collector", &settings->no_collector);
  MAYBE_GET_BOOL("/options/no_file_report", &settings->no_file_report);
  MAYBE_GET_BOOL("/options/no_ip_lookup", &settings->no_ip_lookup);
  MAYBE_GET_BOOL("/options/no_resolver_lookup", &settings->no_resolver_lookup);
  MAYBE_GET_UINT8("/options/parallelism", &settings->parallelism);
  MAYBE_GET("/options/platform", &settings->platform);
  MAYBE_GET_UINT16("/options/port", &settings->port);
  MAYBE_GET("/options/probe_ip", &settings->probe_ip);
  MAYBE_GET("/options/probe_asn", &settings->probe_asn);
  MAYBE_GET("/options/probe_network_name", &settings->probe_network_name);
  MAYBE_GET("/options/probe_cc", &settings->probe_cc);
  MAYBE_GET_BOOL("/options/randomize_input", &settings->randomize_input);
  MAYBE_GET_BOOL("/options/save_real_probe_asn", &settings->save_real_probe_asn);
  MAYBE_GET_BOOL("/options/save_real_probe_ip", &settings->save_real_probe_ip);
  MAYBE_GET_BOOL("/options/save_real_probe_cc", &settings->save_real_probe_cc);
  MAYBE_GET_BOOL("/options/save_real_resolver_ip", &settings->save_real_resolver_ip);
  MAYBE_GET("/options/server", &settings->server);
  MAYBE_GET("/options/software_name", &settings->software_name);
  MAYBE_GET("/options/software_version", &settings->software_version);
#undef MAYBE_GET
#undef MAYBE_GET_UINT8
#undef MAYBE_GET_UINT16
  return true;
}

// UUID4 code
// ``````````
// Derivative work of r-lyeh/sole@c61c49f10d. We include this code inline
// because we don't need this code in other parts of MK.
/*-
 * Portions Copyright (c) 2015 r-lyeh (https://github.com/r-lyeh)
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 */
namespace sole {

class uuid {
  public:
    std::string str();
    uint64_t ab;
    uint64_t cd;
};

uuid uuid4();

std::string uuid::str() {
  std::stringstream ss;
  ss << std::hex << std::nouppercase << std::setfill('0');

  uint32_t a = (ab >> 32);
  uint32_t b = (ab & 0xFFFFFFFF);
  uint32_t c = (cd >> 32);
  uint32_t d = (cd & 0xFFFFFFFF);

  ss << std::setw(8) << (a) << '-';
  ss << std::setw(4) << (b >> 16) << '-';
  ss << std::setw(4) << (b & 0xFFFF) << '-';
  ss << std::setw(4) << (c >> 16) << '-';
  ss << std::setw(4) << (c & 0xFFFF);
  ss << std::setw(8) << d;

  return ss.str();
}

uuid uuid4() {
  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist(0, (uint64_t)(~0));
  uuid my;

  my.ab = dist(rd);
  my.cd = dist(rd);

  /* The version 4 UUID is meant for generating UUIDs from truly-random or
     pseudo-random numbers.

     The algorithm is as follows:

     o  Set the four most significant bits (bits 12 through 15) of the
        time_hi_and_version field to the 4-bit version number from
        Section 4.1.3.

     o  Set the two most significant bits (bits 6 and 7) of the
        clock_seq_hi_and_reserved to zero and one, respectively.

     o  Set all the other bits to randomly (or pseudo-randomly) chosen
        values.

     See <https://tools.ietf.org/html/rfc4122#section-4.4>. */
  my.ab = (my.ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  my.cd = (my.cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  return my;
}

}  // namespace sole

/*
 * Guess the platform in which we are.
 *
 * See: <https://sourceforge.net/p/predef/wiki/OperatingSystems/>
 *      <http://stackoverflow.com/a/18729350>
 */
#if defined __ANDROID__
#  define LIBNETTEST2_PLATFORM "android"
#elif defined __linux__
#  define LIBNETTEST2_PLATFORM "linux"
#elif defined _WIN32
#  define LIBNETTEST2_PLATFORM "windows"
#elif defined __APPLE__
#  include <TargetConditionals.h>
#  if TARGET_OS_IPHONE
#    define LIBNETTEST2_PLATFORM "ios"
#  else
#    define LIBNETTEST2_PLATFORM "macos"
#  endif
#else
#  define LIBNETTEST2_PLATFORM "unknown"
#endif

#define LIBNETTEST2_EMIT_LOG(self, level, uppercase_level, statements) \
  do {                                                                 \
    if (self->get_log_level() >= LogLevel::log_##level) {              \
      std::stringstream ss;                                            \
      ss << "libnettest2: " << statements;                             \
      nlohmann::json value;                                            \
      value["log_level"] = #uppercase_level;                           \
      value["message"] = ss.str();                                     \
      self->emit_ev("log", std::move(value));                          \
    }                                                                  \
  } while (0)

#define LIBNETTEST2_EMIT_WARNING_EX(self, statements) \
  LIBNETTEST2_EMIT_LOG(self, warning, WARNING, statements)

#define LIBNETTEST2_EMIT_INFO_EX(self, statements) \
  LIBNETTEST2_EMIT_LOG(self, info, INFO, statements)

#define LIBNETTEST2_EMIT_DEBUG_EX(self, statements) \
  LIBNETTEST2_EMIT_LOG(self, debug, DEBUG, statements)

#define LIBNETTEST2_EMIT_WARNING(statements) \
  LIBNETTEST2_EMIT_WARNING_EX(this, statements)

#define LIBNETTEST2_EMIT_INFO(statements) \
  LIBNETTEST2_EMIT_INFO_EX(this, statements)

#define LIBNETTEST2_EMIT_DEBUG(statements) \
  LIBNETTEST2_EMIT_DEBUG_EX(this, statements)

// Nettest
// ```````

std::string Nettest::name() const noexcept { return ""; }

std::vector<std::string> Nettest::test_helpers() const noexcept { return {}; }

std::string Nettest::version() const noexcept { return "0.0.1"; }

bool Nettest::needs_input() const noexcept { return false; }

bool Nettest::run(const Settings &, const NettestContext &,
                  std::string, nlohmann::json *, BytesInfo *) noexcept {
  // Do nothing for two seconds, for testing
  std::this_thread::sleep_for(std::chrono::seconds(2));
  return true;
}

Nettest::~Nettest() noexcept {}

// Runner API
// ``````````

Runner::Runner(const Settings &settings, Nettest &nettest) noexcept
    : nettest_{nettest}, settings_{settings} {}

Runner::~Runner() noexcept {}

static std::mutex &global_mutex() noexcept {
  static std::mutex mtx;
  return mtx;
}

static std::string format_system_clock_now() noexcept {
  // Implementation note: to avoid using the C standard library that has
  // given us many headaches on Windows because of parameter validation we
  // go for a fully C++11 solution based on <chrono> and on the C++11
  // HowardInnant/date library, which will be available as part of the
  // C++ standard library starting from C++20.
  //
  // Explanation of the algorithm:
  //
  // 1. get the current system time
  // 2. round the time point obtained in the previous step to an integral
  //    number of seconds since the EPOCH used by the system clock
  // 3. create a system clock time point from the integral number of seconds
  // 4. convert the previous result to string using HowardInnant/date
  // 5. if there is a decimal component (there should be one given how the
  //    library we use works) remove it, because OONI doesn't like it
  //
  // (There was another way to deal with fractionary seconds, i.e. using '%OS',
  //  but this solution seems better to me because it's less obscure.)
  using namespace std::chrono;
  constexpr auto fmt = "%Y-%m-%d %H:%M:%S";
  auto sys_point = system_clock::now();                                    // 1
  auto as_seconds = duration_cast<seconds>(sys_point.time_since_epoch());  // 2
  auto back_as_sys_point = system_clock::time_point(as_seconds);           // 3
  auto s = date::format(fmt, back_as_sys_point);                           // 4
  if (s.find(".") != std::string::npos) s = s.substr(0, s.find("."));      // 5
  return s;
}

static void to_json(nlohmann::json &j, const ErrContext &ec) noexcept {
  j = nlohmann::json{{"code", ec.code},
                     {"library_name", ec.library_name},
                     {"library_version", ec.library_version},
                     {"reason", ec.reason}};
}

bool Runner::run() noexcept {
  BytesInfo info{};
  emit_ev("status.queued", nlohmann::json::object());
  // The following guarantees that just a single test may be active at any
  // given time. Note that we cannot guarantee FIFO queuing.
  std::unique_lock<std::mutex> _{global_mutex()};
  NettestContext ctx;
  emit_ev("status.started", nlohmann::json::object());
  {
    // TODO(bassosimone): the original code has a per-nettest flag that allows
    // a specific nettest to completely ignore the bouncer. However, that is
    // not super smart because we cannot get fresh collector info. This comment
    // is to remember that I need to double check my decision to omit said
    // flag from this reimplementation of the nettest workflow.
    if (!settings_.no_bouncer) {
      ErrContext err{};
      if (!query_bouncer(nettest_.name(), nettest_.test_helpers(),
                         nettest_.version(), &ctx.collectors,
                         &ctx.test_helpers, &info, &err)) {
        LIBNETTEST2_EMIT_WARNING("run: query_bouncer() failed");
        // TODO(bassosimone): shouldn't we introduce failure.query_bouncer?
        //
        // TODO(bassosimone): the original code did not continue to run
        // when it failed to contact the bouncer. What is the correct behavior?
        //
        // FALLTHROUGH
      }
    }
  }
  emit_ev("status.progress", {{"percentage", 0.1},
                              {"message", "contact bouncer"}});
  // Design note: the no_ip_lookup (and similar variables) control whether
  // we perform the lookup. Orthogonally, the save_real_probe_ip (and similar
  // variables) control whether we copy the information obtained with such
  // lookup (or a dummy value if it was not performed) into the report.
  {
    if (settings_.probe_ip == "") {
      // TODO(bassosimone): this is consistent with the existing behaviour
      // and we should update the spec before changing the code in here.
      ctx.probe_ip = "127.0.0.1";
      if (!settings_.no_ip_lookup) {
        ErrContext err{};
        if (!lookup_ip(&ctx.probe_ip, &info, &err)) {
          LIBNETTEST2_EMIT_WARNING("run: lookup_ip() failed");
          // TODO(bassosimone): this failure event is not consistent with
          // the specification, so we should probably simplify it.
          emit_ev("failure.ip_lookup", {
              {"failure", "library_error"},
              {"library_error_context", err},
          });
        } else {
          LIBNETTEST2_EMIT_INFO("Your public IP address: " << ctx.probe_ip);
        }
      }
      // TODO(bassosimone): emit here the warning that, since we don't
      // know the probe_ip, we may not be able to scrub the results. This
      // warning should be emitted both when the lookup fails and when
      // the user has decided to skip the IP lookup.
    } else {
      ctx.probe_ip = settings_.probe_ip;
    }
    // TODO(bassosimone): we need to make sure that we pass down the stack
    // the probe_ip to allow for scrubbing. In the original code, that
    // was passed down using an internal 'real_probe_ip_' setting.
  }
  {
    // Implementation detail: if probe_asn is empty then we will also overwrite
    // the value inside of probe_network_name even if it's non-empty.
    if (settings_.probe_asn == "") {
      // TODO(bassosimone): this is consistent with the existing behaviour
      // and we should update the spec before changing the code in here.
      ctx.probe_asn = "AS0";
      if (!settings_.no_asn_lookup) {
        ErrContext err{};
        if (!lookup_asn(settings_.geoip_asn_path, ctx.probe_ip, &ctx.probe_asn,
                        &ctx.probe_network_name, &err)) {
          LIBNETTEST2_EMIT_WARNING("run: lookup_asn() failed");
          emit_ev("failure.asn_lookup", {
              {"failure", "library_error"},
              {"library_error_context", err},
          });
        } else {
          LIBNETTEST2_EMIT_INFO("Your ISP number: " << ctx.probe_asn);
          LIBNETTEST2_EMIT_DEBUG("Your ISP name: " << ctx.probe_network_name);
        }
      }
    } else {
      ctx.probe_network_name = settings_.probe_network_name;
      ctx.probe_asn = settings_.probe_asn;
    }
  }
  {
    if (settings_.probe_cc == "") {
      // TODO(bassosimone): this is consistent with the existing behaviour
      // and we should update the spec before changing the code in here.
      ctx.probe_cc = "ZZ";
      if (!settings_.no_cc_lookup) {
        ErrContext err{};
        if (!lookup_cc(settings_.geoip_country_path, ctx.probe_ip,
                       &ctx.probe_cc, &err)) {
          LIBNETTEST2_EMIT_WARNING("run: lookup_cc() failed");
          emit_ev("failure.cc_lookup", {
              {"failure", "library_error"},
              {"library_error_context", err},
          });
        } else {
          LIBNETTEST2_EMIT_INFO("Your country: " << ctx.probe_cc);
        }
      }
    } else {
      ctx.probe_cc = settings_.probe_cc;
    }
  }
  emit_ev("status.progress", {{"percentage", 0.2},
                              {"message", "geoip lookup"}});
  // TODO(bassosimone): in this implementation the following event is
  // always emitted, while in the previous implementation that was not
  // the case: the event was emitted only in case the user requested
  // at least _some_ lookups. Let's document this change.
  emit_ev("status.geoip_lookup", {
                                     {"probe_cc", ctx.probe_cc},
                                     {"probe_asn", ctx.probe_asn},
                                     {"probe_ip", ctx.probe_ip},
                                     {"probe_network_name", ctx.probe_network_name},
                                 });
  {
    if (!settings_.no_resolver_lookup) {
      ErrContext err{};
      if (!lookup_resolver_ip(&ctx.resolver_ip, &info, &err)) {
        LIBNETTEST2_EMIT_WARNING("run: lookup_resolver_ip() failed");
        emit_ev("failure.resolver_lookup", {
            {"failure", "library_error"},
            {"library_error_context", err},
        });
      }
    }
    LIBNETTEST2_EMIT_DEBUG("resolver_ip: " << ctx.resolver_ip);
  }
  emit_ev("status.progress", {{"percentage", 0.3},
                              {"message", "resolver lookup"}});
  emit_ev("status.resolver_lookup", {{"resolver_ip", ctx.resolver_ip}});
  auto test_start_time = format_system_clock_now();
  std::string collector_base_url;
  if (!settings_.no_collector) {
    if (settings_.collector_base_url == "") {
      // TODO(bassosimone): here the algorithm for selecting a collector
      // is very basic but mirrors the one in MK. We should probably make
      // the code better to use cloudfronted and/or Tor if needed.
      for (auto &epnt : ctx.collectors) {
        if (epnt.type == endpoint_type_https) {
          LIBNETTEST2_EMIT_INFO("Using discovered collector: " << epnt.address);
          collector_base_url = epnt.address;
          break;
        }
      }
      // TODO(bassosimone): the original code bailed in case there was
      // no collector while here we continue running the nettest. I wonder
      // what is the correct behaviour.
      LIBNETTEST2_EMIT_INFO("Opening report; please be patient...");
      ErrContext err{};
      if (!open_report(collector_base_url, test_start_time, ctx,
                       &ctx.report_id, &info, &err)) {
        LIBNETTEST2_EMIT_WARNING("run: open_report() failed");
        emit_ev("failure.report_create", {
            {"failure", "library_error"},
            {"library_error_context", err},
        });
      } else {
        LIBNETTEST2_EMIT_INFO("Report ID: " << ctx.report_id);
        emit_ev("status.report_create", {{"report_id", ctx.report_id}});
      }
    } else {
      collector_base_url = settings_.collector_base_url;
    }
  }
  emit_ev("status.progress", {{"percentage", 0.4}, {"message", "open report"}});
  do {
    // TODO(bassosimone): the original code here would read files from the
    // file system and fill their content into the inputs vector. Do we want
    // to replicate this functionality here? Most likely.
    if (nettest_.needs_input() && settings_.inputs.empty()) {
      LIBNETTEST2_EMIT_WARNING("run: no input provided");
      break;
    }
    // Note: the specification modifies settings_.inputs in place but here
    // settings_ are immutable, so we actually fill a inputs vector using
    // the settings_ when we expect input. Otherwise we ignore settings_.inputs.
    std::vector<std::string> inputs;
    if (nettest_.needs_input()) {
      inputs.insert(inputs.end(), settings_.inputs.begin(),
                    settings_.inputs.end());
    } else {
      if (!settings_.inputs.empty()) {
        LIBNETTEST2_EMIT_WARNING("run: got unexpected input; ignoring it");
        // Note: ignoring settings_.inputs in this case
      }
      inputs.push_back("");  // just one entry
    }
    if (settings_.randomize_input) {
      std::random_device random_device;
      std::mt19937 mt19937{random_device()};
      std::shuffle(inputs.begin(), inputs.end(), mt19937);
    }
    // Implementation note: here we create a bunch of constant variables for
    // the lambda to access shared stuff in a thread safe way
    constexpr uint8_t default_parallelism = 3;
    uint8_t parallelism = ((nettest_.needs_input() == false)  //
                                     ? (uint8_t)1
                                     : ((settings_.parallelism > 0)  //
                                            ? settings_.parallelism
                                            : default_parallelism));
    std::atomic<uint8_t> active{0};
    auto begin = std::chrono::steady_clock::now();
    const std::chrono::time_point<std::chrono::steady_clock> &cbegin = begin;
    const std::string &ccollector_base_url = collector_base_url;
    const NettestContext &cctx = ctx;
    const std::vector<std::string> &cinputs = inputs;
    const Runner *cthis = this;
    std::atomic<uint64_t> i{0};
    std::mutex mutex;
    const std::string &ctest_start_time = test_start_time;
    auto pinfo = &info;
    // TODO(bassosimone): at this point, the original code was scaling
    // the progress between 0.1 and 0.8 included, so nettests assume that
    // they have the 0..1 range where actually it's smaller. We can also
    // adopt another strategy here for measuring the progress which is
    // less reliant onto the internal details of a nettest.
    for (uint8_t j = 0; j < parallelism; ++j) {
      // Implementation note: make sure this lambda has only access to either
      // constant stuff or to stuff that it's thread safe.
      auto main = [
        &active,               // atomic
        &cbegin,               // const ref
        &ccollector_base_url,  // const ref
        &cctx,                 // const ref
        &cinputs,              // const ref
        &ctest_start_time,     // const ref
        &cthis,                // const pointer
        &i,                    // atomic
        &mutex,                // thread safe
        pinfo                  // ptr to struct w/ only atomic fields
      ]() noexcept {
        active += 1;
        // TODO(bassosimone): more work is required to actually interrupt
        // "long" tests like NDT that take several seconds to complete. This
        // is actually broken also in Measurement Kit, where we cannot stop
        // the reactor easily because of the thread pool. So, it does not
        // matter much that we're shipping this sub-library with the interrupt
        // nettest functionality that is not fully functional.
        while (!cthis->interrupted_) {
          uint32_t idx = 0;
          {
            std::unique_lock<std::mutex> _{mutex};
            // Implementation note: we currently limit the maximum value of
            // the index to UINT32_MAX on the grounds that in Java it's painful
            // to deal with unsigned 64 bit integers.
            if (i > UINT32_MAX || i >= cinputs.size()) {
              break;
            }
            idx = (uint32_t)i;
            i += 1;
          }
          if (!cthis->run_with_index32(cbegin, ctest_start_time, cinputs, cctx,
                                       ccollector_base_url, idx, pinfo)) {
            break;
          }
        }
        active -= 1;
      };
      std::thread thread{std::move(main)};
      thread.detach();
    }
    while (active > 0) {
      constexpr auto msec = 250;
      std::this_thread::sleep_for(std::chrono::milliseconds(msec));
    }
    emit_ev("status.progress", {{"percentage", 0.9},
                                {"message", "measurement complete"}});
    if (!settings_.no_collector && !ctx.report_id.empty()) {
      ErrContext err{};
      if (!close_report(collector_base_url, ctx.report_id, &info, &err)) {
        LIBNETTEST2_EMIT_WARNING("run: close_report() failed");
        emit_ev("failure.report_close", {
            {"failure", "library_error"},
            {"library_error_context", err},
        });
      } else {
        emit_ev("status.report_close", {{"report_id", ctx.report_id}});
      }
    } else if (ctx.report_id.empty()) {
      emit_ev("failure.report_close", {{"failure", "report_not_open_error"}});
    }
    emit_ev("status.progress", {{"percentage", 1.0},
                                {"message", "report close"}});
  } while (0);
  // TODO(bassosimone): decide whether it makes sense to have an overall
  // precise error code in this context (it seems not so easy). For now just
  // always report success, which is what also legacy MK code does.
  emit_ev("status.end", {{"failure", ""},
                         {"downloaded_kb", info.bytes_down.load() / 1024.0},
                         {"uploaded_kb", info.bytes_up.load() / 1024.0}});
  return true;
}

void Runner::interrupt() noexcept { interrupted_ = true; }

LogLevel Runner::get_log_level() const noexcept { return settings_.log_level; }

// Methods you typically want to override
// ``````````````````````````````````````

void Runner::on_event(const nlohmann::json &event) const noexcept {
  // When running with -fsanitize=thread enable on macOS, a data race in
  // accessing std::clog is reported. Attempt to avoid that.
  static std::mutex no_data_race;
  std::unique_lock<std::mutex> _{no_data_race};
  std::clog << event.dump() << std::endl;
}

// Methods you generally DON'T want to override
// ````````````````````````````````````````````

void Runner::emit_ev(std::string key, nlohmann::json value) const noexcept {
  assert(value.is_object());
  on_event({{"key", std::move(key)}, {"value", std::move(value)}});
}

bool Runner::run_with_index32(
    const std::chrono::time_point<std::chrono::steady_clock> &begin,
    const std::string &test_start_time,
    const std::vector<std::string> &inputs, const NettestContext &ctx,
    const std::string &collector_base_url, uint32_t i,
    BytesInfo *info) const noexcept {
  if (info == nullptr) return false;
  // TODO(bassosimone): the old code here emitted an event telling the user
  // more about the progress. The progress is actually better computed in the
  // in the outer thread, emitting the progress based on the ETA and/or the
  // number of entries, however, by using that strategy we will loose the
  // possibility of telling the user about the current activity.
  {
    auto current_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = current_time - begin;
    // We call a nettest done when we reach 90% of the expected runtime. This
    // accounts for possible errors and for the time for closing the report.
    if (settings_.max_runtime >= 0 &&
        elapsed.count() >= settings_.max_runtime * 0.9) {
      LIBNETTEST2_EMIT_INFO("exceeded max runtime");
      return false;
    }
  }
  // TODO(bassosimone): from a conversation with @xanscale, I understood
  // that he finds confusing to have this event when the nettest has
  // no input. I think that, if this event is omitted, then we would need
  // to omit also similar events for such test. Do we want that?
  emit_ev("status.measurement_start", {{"idx", i}, {"input", inputs[i]}});
  nlohmann::json measurement;
  // TODO(bassosimone):
  //
  // 1. in the specification, it's unclear whether annotations are always
  //    (string, string) pairs, or whether other scalar values could be
  //    used for annotations. My assumption has always been strings, but
  //    it may make sense to double check.
  //
  // 2. it seems I actually forgot (this is what happens when you sleep
  //    over an open terminal and forget about details).
  //
  measurement["annotations"] = settings_.annotations;
  measurement["annotations"]["engine_name"] = settings_.engine_name;
  measurement["annotations"]["engine_version"] = settings_.engine_version;
  measurement["annotations"]["engine_version_full"] = settings_.engine_version_full;
  measurement["annotations"]["platform"] =
      !settings_.platform.empty()
          ? settings_.platform
          : LIBNETTEST2_PLATFORM;
  measurement["annotations"]["probe_network_name"] =
      settings_.save_real_probe_asn
          ? ctx.probe_network_name
          : "";
  measurement["id"] = sole::uuid4().str();
  measurement["input"] = inputs[i];
  // TODO(bassosimone): when the input is the empty string, we should actually
  // make sure to emit `null` in the JSON rather than the empty string. This
  // is perhaps also a great suggestion regarding tests that do not take input
  // even though IIRC we already specified that specific case.
  measurement["input_hashes"] = nlohmann::json::array();
  measurement["measurement_start_time"] = format_system_clock_now();
  // TODO(bassosimone): the following was actually also a bug of the code
  // in MK where we were not able to serialize options. We MAY want to add
  // support for this feature (could options leak information though?). I
  // think this is a good topic to discuss with @hellais.
  measurement["options"] = nlohmann::json::array();
  measurement["probe_asn"] = settings_.save_real_probe_asn ? ctx.probe_asn : "";
  measurement["probe_cc"] = settings_.save_real_probe_cc ? ctx.probe_cc : "";
  // TODO(bassosimone): this was not implemented in MK. Do we want to
  // implement it now, or would it provide a too detailed location?
  measurement["probe_city"] = nullptr;
  measurement["probe_ip"] = settings_.save_real_probe_ip ? ctx.probe_ip : "";
  measurement["report_id"] = ctx.report_id;
  measurement["sotfware_name"] = settings_.software_name;
  measurement["sotfware_version"] = settings_.software_version;
  {
    measurement["test_helpers"] = nlohmann::json::object();
    // TODO(bassosimone): make sure this is exactly what we should send as
    // I'm quite sure that MK sends less info than this.
    //
    // Here's a relevant snippet:
    //
    // ```
    //  "test_helpers": {
    //    "backend": {
    //      "address": "httpo://2lzg3f4r3eapfi6j.onion",
    //      "type": "onion"
    //    }
    //  },
    // ```
    //
    // So, it's the specific test helper that was used, rather than all
    // the discovered ones, indexed by the name with which it's called
    // internally as an option (this feels wrong to me). Still we
    // decided to use the possibly-wrong name for backward compatibility.
    for (auto &pair : ctx.test_helpers) {
      auto &key = pair.first;
      auto &values = pair.second;
      for (auto &epnt : values) {
        measurement["test_helpers"][key] = nlohmann::json::object();
        measurement["test_helpers"][key]["address"] = epnt.address;
        if (epnt.type == endpoint_type_onion) {
          measurement["test_helpers"][key]["type"] = "onion";
        } else if (epnt.type == endpoint_type_https) {
          measurement["test_helpers"][key]["type"] = "https";
        } else if (epnt.type == endpoint_type_cloudfront) {
          measurement["test_helpers"][key]["type"] = "cloudfront";
          measurement["test_helpers"][key]["front"] = epnt.front;
        } else {
          // NOTHING
        }
      }
    }
  }
  measurement["test_name"] = nettest_.name();
  measurement["test_start_time"] = test_start_time;
  measurement["test_version"] = nettest_.version();
  nlohmann::json test_keys;
  auto measurement_start = std::chrono::steady_clock::now();
  // TODO(bassosimone): make sure we correctly pass downstream the probe_ip
  // such that the consumer tests could use it to scrub the IP. Currently the
  // nettest with this requirements is WebConnectivity.
  auto rv = nettest_.run(settings_, ctx, inputs[i], &test_keys, info);
  {
    auto current_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = current_time - measurement_start;
    measurement["test_runtime"] = elapsed.count();
  }
  // We fill the resolver_ip after the measurement. Doing that before may allow
  // the nettest to overwrite the client_resolver field set by us.
  measurement["test_keys"] = test_keys;
  measurement["test_keys"]["client_resolver"] = settings_.save_real_resolver_ip
                                                ? ctx.resolver_ip
                                                : "";
  if (!rv) {
    // TODO(bassosimone): we should standardize the errors we emit. We can
    // probably emit something along the lines of library_error.
    emit_ev("failure.measurement", {
        {"failure", "generic_error"},
        {"idx", i},
    });
  }
  do {
    std::string str;
    try {
      str = measurement.dump();
    } catch (const std::exception &e) {
      LIBNETTEST2_EMIT_WARNING("run: cannot serialize JSON: " << e.what());
      // TODO(bassosimone): This is MK passing us an invalid JSON. Should we
      // submit something nonetheless as a form of telemetry? This is something
      // I should probably discuss with @hellais and/or @darkk.
      break;
    }
    if (!settings_.no_collector && !ctx.report_id.empty()) {
      ErrContext err{};
      // Implementation note: as you probably have noticed, this library does
      // not write anything on the disk. The caller however may want to do that
      // when there's need to do so, by overriding event handlers.
      if (!update_report(collector_base_url, ctx.report_id, str, info, &err)) {
        LIBNETTEST2_EMIT_WARNING("run: update_report() failed");
        emit_ev("failure.measurement_submission", {
            {"failure", "library_error"},
            {"library_error_context", err},
            {"idx", i},
            {"json_str", str},
        });
      } else {
        emit_ev("status.measurement_submission", {{"idx", i}});
      }
    } else if (ctx.report_id.empty()) {
      emit_ev("failure.measurement_submission", {{
          "failure", "report_not_open_error"
      }});
    }
    // According to several discussions with @lorenzoPrimi, it is much better
    // for this event to be emitted AFTER submitting the report.
    emit_ev("measurement", {{"idx", i}, {"json_str", std::move(str)}});
  } while (0);
  emit_ev("status.measurement_done", {{"idx", i}});
  return true;
}

// TODO(bassosimone): we should _probably_ make this configurable. One way to
// do that MAY be to use the net/timeout setting.
constexpr long curl_timeout = 5;

static std::string without_final_slash(std::string src) noexcept {
  while (src.size() > 0 && src[src.size() - 1] == '/') {
    src = src.substr(0, src.size() - 1);
  }
  return src;
}

static std::string nlohmann_json_version() noexcept {
  std::stringstream ss;
#ifdef NLOHMANN_JSON_VERSION_MAJOR
  ss << NLOHMANN_JSON_VERSION_MAJOR << "." << NLOHMANN_JSON_VERSION_MINOR
     << "." << NLOHMANN_JSON_VERSION_PATCH;
#else
  ss << "unknown";
#endif
  return ss.str();
}

// TODO(bassosimone): the original code had a notion of a production and a
// testing bouncer. The new code currently only uses whatever is passed into
// the bouncer_base_url variable. It would however make sense to consider
// having a notion of testing bouncer (perhaps not here though).
//
// TODO(bassosimone): the original code was bailing if it could not find
// a https test helper, while here we completely ignore the issue and
// proceed, eventually leaving the nettest without a test helper. I think
// this should be fixed to reimplement the previous behaviour.
bool Runner::query_bouncer(std::string nettest_name,
                           std::vector<std::string> nettest_helper_names,
                           std::string nettest_version,
                           std::vector<EndpointInfo> *collectors,
                           std::map<std::string,
                             std::vector<EndpointInfo>> *test_helpers,
                           BytesInfo *info, ErrContext *err) noexcept {
  LIBNETTEST2_EMIT_DEBUG("query_bouncer: nettest_name: " << nettest_name);
  for (auto &helper : nettest_helper_names) {
    LIBNETTEST2_EMIT_DEBUG("query_bouncer: helper: - " << helper);
  }
  LIBNETTEST2_EMIT_DEBUG("query_bouncer: nettest_version: " << nettest_version);
  if (collectors == nullptr || test_helpers == nullptr ||
      info == nullptr || err == nullptr) {
    LIBNETTEST2_EMIT_WARNING("query_bouncer: passed null pointers");
    return false;
  }
  test_helpers->clear();
  collectors->clear();
  std::string requestbody;
  try {
    nlohmann::json doc;
    doc["net-tests"] = nlohmann::json::array();
    doc["net-tests"][0] = nlohmann::json::object();
    doc["net-tests"][0]["input-hashes"] = nullptr;
    doc["net-tests"][0]["name"] = nettest_name;
    doc["net-tests"][0]["test-helpers"] = nettest_helper_names;
    doc["net-tests"][0]["version"] = nettest_version;
    requestbody = doc.dump();
  } catch (const std::exception &exc) {
    LIBNETTEST2_EMIT_WARNING("query_bouncer: cannot serialize request");
    err->reason = 1;
    err->library_name = "nlohmann/json";
    err->library_version = nlohmann_json_version();
    err->reason = exc.what();
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("query_bouncer: JSON request: " << requestbody);
  std::string responsebody;
  // TODO(bassosimone): we should probably discuss with @hellais whether we
  // like that currently we do not have a cloudfronted bouncer fallback. This
  // is to be done later since I want to reach feature parity with MK legacy
  // codebase first, and focus on perks later.
  std::string url = without_final_slash(settings_.bouncer_base_url);
  url += "/bouncer/net-tests";
  LIBNETTEST2_EMIT_INFO("Contacting bouncer: " << url);
  if (!curlx_post_json(std::move(url), std::move(requestbody), curl_timeout,
                       &responsebody, info, err)) {
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("query_bouncer: JSON reply: " << responsebody);
  try {
    // TODO(bassosimone): make processing more flexible and robust? Here we
    // are making strong assumptions on the returned object type. This is
    // also something that we can defer to the future.
    //
    // TODO(bassosimone): the original code spent some time trying to
    // understand what error was returned by the bouncer, but that mostly
    // looked like diagnostic, so I am not sure how to take that.
    auto doc = nlohmann::json::parse(responsebody);
    for (auto &entry : doc.at("net-tests")) {
      {
        EndpointInfo info;
        info.type = endpoint_type_onion;
        info.address = entry.at("collector");
        collectors->push_back(std::move(info));
      }
      for (auto &entry : entry.at("collector-alternate")) {
        EndpointInfo info;
        if (entry.at("type") == "https") {
          info.type = endpoint_type_https;
          info.address = entry.at("address");
        } else if (entry.at("type") == "cloudfront") {
          info.type = endpoint_type_cloudfront;
          info.address = entry.at("address");
          info.front = entry.at("front");
        } else {
          continue;
        }
        collectors->push_back(std::move(info));
      }
      // Rationale: MK is still using v2.x but after we integrate this
      // library we can get rid of most of MK code using v2.x, thus making
      // it much simpler to upgrade MK to the 3.0.0 version. Still, we
      // need here support for v2.x to be able to do that.
#ifdef NLOHMANN_JSON_VERSION_MAJOR  // >= v3.0.0
      for (auto &entry : entry.at("test-helpers").items()) {
#else
      for (auto &entry : nlohmann::json::iterator_wrapper(entry.at("test-helpers"))) {
#endif
        std::string key = entry.key();
        EndpointInfo info;
        info.type = endpoint_type_onion;
        info.address = entry.value();
        (*test_helpers)[key].push_back(std::move(info));
      }
#ifdef NLOHMANN_JSON_VERSION_MAJOR  // >= v3.0.0
      for (auto &entry : entry.at("test-helpers-alternate").items()) {
#else
      for (auto &entry : nlohmann::json::iterator_wrapper(entry.at("test-helpers-alternate"))) {
#endif
        std::string key = entry.key();
        for (auto &entry : entry.value()) {
          EndpointInfo info;
          if (entry.at("type") == "https") {
            info.type = endpoint_type_https;
            info.address = entry.at("address");
          } else if (entry.at("type") == "cloudfront") {
            info.type = endpoint_type_cloudfront;
            info.address = entry.at("address");
            info.front = entry.at("front");
          } else {
            continue;
          }
          (*test_helpers)[key].push_back(std::move(info));
        }
      }
    }
  } catch (const std::exception &exc) {
    LIBNETTEST2_EMIT_WARNING("query_bouncer: cannot process response: "
                             << exc.what());
    err->reason = 1;
    err->library_name = "nlohmann/json";
    err->library_version = nlohmann_json_version();
    err->reason = exc.what();
    return false;
  }
  for (auto &info : *collectors) {
    LIBNETTEST2_EMIT_DEBUG("query_bouncer: collector: address='"
      << info.address << "' type=" << (uint32_t)info.type
      << " front='" << info.front << "'");
  }
  for (auto &pair : *test_helpers) {
    auto &values = pair.second;
    auto &key = pair.first;
    for (auto &info : values) {
      LIBNETTEST2_EMIT_DEBUG("query_bouncer: test_helper: key='" << key
        << "' address='" << info.address << "' type=" << (uint32_t)info.type
        << " front='" << info.front << "'");
    }
  }
  return true;
}

static bool xml_extract(std::string input, std::string open_tag,
                        std::string close_tag, std::string *result) noexcept {
  if (result == nullptr) return false;
  auto pos = input.find(open_tag);
  if (pos == std::string::npos) return false;
  input = input.substr(pos + open_tag.size());
  pos = input.find(close_tag);
  if (pos == std::string::npos) return false;
  input = input.substr(0, pos);
  for (auto ch : input) {
    if (isspace(ch)) continue;
    // TODO(bassosimone): perhaps reject input that is not printable? This is
    // something that I may want to discuss with @hellais or @darkk.
    //
    // TODO(bassosimone): the original code actually validated the IP
    // address, so we should probably do something similar.
    *result += tolower(ch);
  }
  return true;
}

bool Runner::lookup_ip(std::string *ip, BytesInfo *info,
                       ErrContext *err) noexcept {
  if (ip == nullptr || info == nullptr || err == nullptr) return false;
  ip->clear();
  std::string responsebody;
  // TODO(bassosimone): as discussed several time with @hellais, here we
  // should use other services for getting the probe's IP address. Let us
  // reach feature parity first and then we can work on this.
  std::string url = "https://geoip.ubuntu.com/lookup";
  LIBNETTEST2_EMIT_DEBUG("lookup_ip: URL: " << url);
  if (!curlx_get(std::move(url), curl_timeout, &responsebody, info, err)) {
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("lookup_ip: response: " << responsebody);
  return xml_extract(responsebody, "<Ip>", "</Ip>", ip);
}

bool Runner::lookup_resolver_ip(
    std::string *ip, BytesInfo *info, ErrContext *err) noexcept {
  if (ip == nullptr || info == nullptr || err == nullptr) return false;
  ip->clear();
  // TODO(bassosimone): so, here we use getaddrinfo() because we want to know
  // what resolver has the user configured by default. However, the nettest
  // MAY use another resolver. It's important to decide whether this would be
  // a problem or not. There is also a _third_ case, i.e. the Vodafone-like
  // case where there is a transparent DNS proxy.
  //
  // TODO(bassosimone): currently we're using A only because we're doing what
  // MK does but we should consider doing a AAAA query as well.
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_flags |= AI_NUMERICSERV;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *rp = nullptr;
  {
    // Upper bound estimate: assume that the AF_INET query takes a maximum
    // size IP datagram (i.e. 512 bytes according to <arpa/nameser.h>)
    info->bytes_up += 512;
    info->bytes_down += 512;
  }
  auto rv = ::getaddrinfo("whoami.akamai.net", "443", &hints, &rp);
  if (rv != 0) {
    LIBNETTEST2_EMIT_WARNING("lookup_resolver_ip: " << gai_strerror(rv));
    err->code = rv;
    err->library_name = "libc/getaddrinfo";
    err->library_version = "";
    err->reason = gai_strerror(rv);
    return false;
  }
  for (auto ai = rp; ai != nullptr && ip->empty(); ai = ai->ai_next) {
    char host[NI_MAXHOST];
    if (::getnameinfo(ai->ai_addr, ai->ai_addrlen, host, NI_MAXHOST, nullptr,
                      0, NI_NUMERICHOST) != 0) {
      LIBNETTEST2_EMIT_WARNING("lookup_resolver_ip: getnameinfo() failed");
      break;  // This should not happen in a sane system
    }
    *ip = host;
  }
  ::freeaddrinfo(rp);
  return !ip->empty();
}

bool Runner::open_report(const std::string &collector_base_url,
                         const std::string &test_start_time,
                         const NettestContext &context,
                         std::string *report_id,
                         BytesInfo *info,
                         ErrContext *err) noexcept {
  if (report_id == nullptr || info == nullptr || err == nullptr) return false;
  report_id->clear();
  std::string requestbody;
  try {
    nlohmann::json doc;
    doc["data_format_version"] = "0.2.0";
    doc["format"] = "json";
    doc["input_hashes"] = nlohmann::json::array();
    doc["probe_asn"] = context.probe_asn;
    doc["probe_cc"] = context.probe_cc;
    doc["software_name"] = settings_.software_name;
    doc["software_version"] = settings_.software_version;
    doc["test_name"] = nettest_.name();
    doc["test_start_time"] = test_start_time,
    doc["test_version"] = nettest_.version();
    requestbody = doc.dump();
  } catch (const std::exception &exc) {
    LIBNETTEST2_EMIT_WARNING("open_report: cannot serialize JSON");
    err->reason = 1;
    err->library_name = "nlohmann/json";
    err->library_version = nlohmann_json_version();
    err->reason = exc.what();
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("open_report: JSON request: " << requestbody);
  std::string responsebody;
  std::string url = without_final_slash(collector_base_url);
  // TODO(bassosimone): to match the behaviour in the MK code, here we should
  // fail in case the collector URL is empty.
  //
  // TODO(bassosimone): to match the functionality currently in MK, here we
  // should implement talking to a domain fronted collector.
  //
  // TODO(bassosimone): to match the functionality currently in MK, here we
  // should return an error if the entry does not look like valid.
  url += "/report";
  LIBNETTEST2_EMIT_DEBUG("open_report: URL: " << url);
  if (!curlx_post_json(std::move(url), std::move(requestbody), curl_timeout,
                       &responsebody, info, err)) {
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("open_report: JSON reply: " << responsebody);
  try {
    auto doc = nlohmann::json::parse(responsebody);
    *report_id = doc.at("report_id");
  } catch (const std::exception &exc) {
    LIBNETTEST2_EMIT_WARNING("open_report: can't parse reply: " << exc.what());
    err->reason = 1;
    err->library_name = "nlohmann/json";
    err->library_version = nlohmann_json_version();
    err->reason = exc.what();
    return false;
  }
  return true;
}

bool Runner::update_report(const std::string &collector_base_url,
                           const std::string &report_id,
                           const std::string &json_str,
                           BytesInfo *info,
                           ErrContext *err) const noexcept {
  if (info == nullptr || err == nullptr) return false;
  std::string responsebody;
  std::string url = without_final_slash(collector_base_url);
  url += "/report/";
  url += report_id;
  nlohmann::json message;
  message["content"] = json_str;
  message["format"] = "json";
  std::string requestbody;
  try {
    requestbody = message.dump();
  } catch (const std::exception &exc) {
    LIBNETTEST2_EMIT_WARNING("update_report: cannot serialize request");
    err->reason = 1;
    err->library_name = "nlohmann/json";
    err->library_version = nlohmann_json_version();
    err->reason = exc.what();
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("update_report: JSON request: " << requestbody);
  LIBNETTEST2_EMIT_DEBUG("update_report: URL: " << url);
  if (!curlx_post_json(std::move(url), std::move(requestbody), curl_timeout,
                       &responsebody, info, err)) {
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("update_report: JSON reply: " << responsebody);
  return true;
}

bool Runner::close_report(const std::string &collector_base_url,
                          const std::string &report_id,
                          BytesInfo *info,
                          ErrContext *err) noexcept {
  if (info == nullptr || err == nullptr) return false;
  std::string responsebody;
  std::string url = without_final_slash(collector_base_url);
  url += "/report/" + report_id + "/close";
  LIBNETTEST2_EMIT_DEBUG("close_report: URL: " << url);
  if (!curlx_post_json(std::move(url), "", curl_timeout,
                       &responsebody, info, err)) {
    return false;
  }
  LIBNETTEST2_EMIT_DEBUG("close_report: response body: " << responsebody);
  return true;
}

// MaxMindDB code
// ``````````````

bool Runner::lookup_asn(const std::string &dbpath,
                        const std::string &probe_ip,
                        std::string *asn,
                        std::string *probe_network_name,
                        ErrContext *err) noexcept {
  if (asn == nullptr || probe_network_name == nullptr || err == nullptr) {
    return false;
  }
  asn->clear();
  probe_network_name->clear();
  // TODO(bassosimone): there is a great deal of duplication of basically equal
  // MMDB code here that can be solved by refactoring common code.
  MMDB_s mmdb{};
  auto mmdb_error = ::MMDB_open(dbpath.data(), MMDB_MODE_MMAP, &mmdb);
  if (mmdb_error != 0) {
    LIBNETTEST2_EMIT_WARNING("lookup_asn: " << MMDB_strerror(mmdb_error));
    err->code = mmdb_error;
    err->library_name = "libmaxminddb/MMDB_open";
    err->library_version = MMDB_lib_version();
    err->reason = MMDB_strerror(mmdb_error);
    return false;
  }
  auto rv = false;
  do {
    auto gai_error = 0;
    mmdb_error = 0;
    auto record = MMDB_lookup_string(&mmdb, probe_ip.data(),
                                     &gai_error, &mmdb_error);
    if (gai_error) {
      LIBNETTEST2_EMIT_WARNING("lookup_asn: " << gai_strerror(gai_error));
      // Note: MMDB_lookup_string() calls getaddrinfo() and the reported
      // gai_error error code originates from getaddrinfo().
      err->code = gai_error;
      err->library_name = "libc/getaddrinfo";
      err->library_version = "";
      err->reason = gai_strerror(gai_error);
      break;
    }
    if (mmdb_error) {
      LIBNETTEST2_EMIT_WARNING("lookup_asn: " << MMDB_strerror(mmdb_error));
      err->code = mmdb_error;
      err->library_name = "libmaxminddb/MMDB_lookup_string";
      err->library_version = MMDB_lib_version();
      err->reason = MMDB_strerror(mmdb_error);
      break;
    }
    if (!record.found_entry) {
      LIBNETTEST2_EMIT_WARNING("lookup_asn: no entry for: " << probe_ip);
      auto e = Errors::mmdb_enoent;
      err->code = (int64_t)e;
      err->library_name = default_engine_name();
      err->library_version = version();
      err->reason = libnettest2_strerror(e);
      break;
    }
    {
      MMDB_entry_data_s entry{};
      mmdb_error = MMDB_get_value(
          &record.entry, &entry, "autonomous_system_number", nullptr);
      if (mmdb_error != 0) {
        LIBNETTEST2_EMIT_WARNING("lookup_asn: " << MMDB_strerror(mmdb_error));
        err->code = mmdb_error;
        err->library_name = "libmaxminddb/MMDB_get_value";
        err->library_version = MMDB_lib_version();
        err->reason = MMDB_strerror(mmdb_error);
        break;
      }
      if (!entry.has_data || entry.type != MMDB_DATA_TYPE_UINT32) {
        LIBNETTEST2_EMIT_WARNING("lookup_cc: no data or unexpected data type");
        auto e = Errors::mmdb_enodatafortype;
        err->code = (int64_t)e;
        err->library_name = default_engine_name();
        err->library_version = version();
        err->reason = libnettest2_strerror(e);
        break;
      }
      *asn = std::string{"AS"} + std::to_string(entry.uint32);
    }
    {
      MMDB_entry_data_s entry{};
      mmdb_error = MMDB_get_value(
          &record.entry, &entry, "autonomous_system_organization", nullptr);
      if (mmdb_error != 0) {
        LIBNETTEST2_EMIT_WARNING("lookup_asn: " << MMDB_strerror(mmdb_error));
        err->code = mmdb_error;
        err->library_name = "libmaxminddb/MMDB_get_value";
        err->library_version = MMDB_lib_version();
        err->reason = MMDB_strerror(mmdb_error);
        break;
      }
      if (!entry.has_data || entry.type != MMDB_DATA_TYPE_UTF8_STRING) {
        LIBNETTEST2_EMIT_WARNING("lookup_cc: no data or unexpected data type");
        auto e = Errors::mmdb_enodatafortype;
        err->code = (int64_t)e;
        err->library_name = default_engine_name();
        err->library_version = version();
        err->reason = libnettest2_strerror(e);
        break;
      }
      *probe_network_name = std::string{entry.utf8_string, entry.data_size};
    }
    rv = true;
  } while (false);
  MMDB_close(&mmdb);
  return rv;
}

bool Runner::lookup_cc(const std::string &dbpath, const std::string &probe_ip,
                       std::string *cc, ErrContext *err) noexcept {
  if (cc == nullptr || err == nullptr) return false;
  cc->clear();
  MMDB_s mmdb{};
  auto mmdb_error = ::MMDB_open(dbpath.data(), MMDB_MODE_MMAP, &mmdb);
  if (mmdb_error != 0) {
    LIBNETTEST2_EMIT_WARNING("lookup_cc: " << MMDB_strerror(mmdb_error));
    err->code = mmdb_error;
    err->library_name = "libmaxminddb/MMDB_open";
    err->library_version = MMDB_lib_version();
    err->reason = MMDB_strerror(mmdb_error);
    return false;
  }
  auto rv = false;
  do {
    auto gai_error = 0;
    mmdb_error = 0;
    auto record = MMDB_lookup_string(&mmdb, probe_ip.data(),
                                     &gai_error, &mmdb_error);
    if (gai_error) {
      LIBNETTEST2_EMIT_WARNING("lookup_cc: " << gai_strerror(gai_error));
      // Note: MMDB_lookup_string() calls getaddrinfo() and the reported
      // gai_error error code originates from getaddrinfo().
      err->code = gai_error;
      err->library_name = "libc/getaddrinfo";
      err->library_version = "";
      err->reason = gai_strerror(gai_error);
      break;
    }
    if (mmdb_error) {
      LIBNETTEST2_EMIT_WARNING("lookup_cc: " << MMDB_strerror(mmdb_error));
      err->code = mmdb_error;
      err->library_name = "libmaxminddb/MMDB_lookup_string";
      err->library_version = MMDB_lib_version();
      err->reason = MMDB_strerror(mmdb_error);
      break;
    }
    if (!record.found_entry) {
      LIBNETTEST2_EMIT_WARNING("lookup_cc: no entry for: " << probe_ip);
      auto e = Errors::mmdb_enoent;
      err->code = (int64_t)e;
      err->library_name = default_engine_name();
      err->library_version = version();
      err->reason = libnettest2_strerror(e);
      break;
    }
    {
      MMDB_entry_data_s entry{};
      mmdb_error = MMDB_get_value(
          &record.entry, &entry, "registered_country", "iso_code", nullptr);
      if (mmdb_error != 0) {
        LIBNETTEST2_EMIT_WARNING("lookup_cc: " << MMDB_strerror(mmdb_error));
        err->code = mmdb_error;
        err->library_name = "libmaxminddb/MMDB_get_value";
        err->library_version = MMDB_lib_version();
        err->reason = MMDB_strerror(mmdb_error);
        break;
      }
      if (!entry.has_data || entry.type != MMDB_DATA_TYPE_UTF8_STRING) {
        LIBNETTEST2_EMIT_WARNING("lookup_cc: no data or unexpected data type");
        auto e = Errors::mmdb_enodatafortype;
        err->code = (int64_t)e;
        err->library_name = default_engine_name();
        err->library_version = version();
        err->reason = libnettest2_strerror(e);
        break;
      }
      *cc = std::string{entry.utf8_string, entry.data_size};
    }
    rv = true;
  } while (false);
  MMDB_close(&mmdb);
  return rv;
}

// cURL code
// `````````

void Runner::CurlxDeleter::operator()(CURL *handle) noexcept {
  curl_easy_cleanup(handle);  // handless null gracefully
}

class CurlxSlist {
 public:
  curl_slist *slist = nullptr;

  CurlxSlist() noexcept = default;
  CurlxSlist(const CurlxSlist &) noexcept = delete;
  CurlxSlist &operator=(const CurlxSlist &) noexcept = delete;
  CurlxSlist(CurlxSlist &&) noexcept = delete;
  CurlxSlist &operator=(CurlxSlist &&) noexcept = delete;

  ~CurlxSlist() noexcept;
};

CurlxSlist::~CurlxSlist() noexcept {
  curl_slist_free_all(slist);  // handles nullptr gracefully
}

bool Runner::curlx_post_json(std::string url,
                             std::string requestbody,
                             long timeout,
                             std::string *responsebody,
                             BytesInfo *info,
                             ErrContext *err) const noexcept {
  if (responsebody == nullptr || info == nullptr || err == nullptr) {
    return false;
  }
  *responsebody = "";
  UniqueCurlx handle;
  handle.reset(::curl_easy_init());
  if (!handle) {
    LIBNETTEST2_EMIT_WARNING("curlx_post_json: curl_easy_init() failed");
    return false;
  }
  CurlxSlist headers;
  // TODO(bassosimone): here we should implement support for Tor and for
  // cloudfronted. Code doing that was implemented by @hellais into the
  // measurement-kit/web-api-client repository. Deferred after we have a
  // status a feature parity with MK.
  if (!requestbody.empty()) {
    {
      if ((headers.slist = curl_slist_append(
               headers.slist, "Content-Type: application/json")) == nullptr) {
        LIBNETTEST2_EMIT_WARNING("curlx_post_json: curl_slist_append() failed");
        return false;
      }
      if (::curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER,
                             headers.slist) != CURLE_OK) {
        LIBNETTEST2_EMIT_WARNING(
            "curlx_post_json: curl_easy_setopt(CURLOPT_HTTPHEADER) failed");
        return false;
      }
    }
    if (::curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS,
                           requestbody.data()) != CURLE_OK) {
      LIBNETTEST2_EMIT_WARNING(
          "curlx_post_json: curl_easy_setopt(CURLOPT_POSTFIELDS) failed");
      return false;
    }
  }
  if (::curl_easy_setopt(handle.get(), CURLOPT_POST, 1) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_post_json: curl_easy_setopt(CURLOPT_POST) failed");
    return false;
  }
  return curlx_common(handle, std::move(url), timeout, responsebody, info, err);
}

bool Runner::curlx_get(std::string url,
                       long timeout,
                       std::string *responsebody,
                       BytesInfo *info,
                       ErrContext *err) noexcept {
  if (responsebody == nullptr || info == nullptr || err == nullptr) {
    return false;
  }
  *responsebody = "";
  UniqueCurlx handle;
  handle.reset(::curl_easy_init());
  if (!handle) {
    LIBNETTEST2_EMIT_WARNING("curlx_get: curl_easy_init() failed");
    return false;
  }
  return curlx_common(handle, std::move(url), timeout, responsebody, info, err);
}

}  // namespace libnettest2
}  // namespace measurement_kit
extern "C" {

static size_t libnettest2_curl_stringstream_callback(
    char *ptr, size_t size, size_t nmemb, void *userdata) noexcept {
  if (nmemb <= 0) {
    return 0;  // This means "no body"
  }
  if (size > SIZE_MAX / nmemb) {
    assert(false);  // Also catches case where size is zero
    return 0;
  }
  auto realsiz = size * nmemb;  // Overflow not possible (see above)
  auto ss = static_cast<std::stringstream *>(userdata);
  (*ss) << std::string{ptr, realsiz};
  // From fwrite(3): "[the return value] equals the number of bytes
  // written _only_ when `size` equals `1`". See also
  // https://sourceware.org/git/?p=glibc.git;a=blob;f=libio/iofwrite.c;h=800341b7da546e5b7fd2005c5536f4c90037f50d;hb=HEAD#l29
  return nmemb;
}

static int libnettest2_curl_debugfn(CURL *handle,
                                    curl_infotype type,
                                    char *data,
                                    size_t size,
                                    void *userptr) {
  (void)handle;
  using namespace measurement_kit::libnettest2;
  auto wrapper = static_cast<Runner::BytesInfoWrapper *>(userptr);
  auto info = wrapper->info;
  auto owner = wrapper->owner;
  // Emit debug messages if the log level allows that
  if (owner->get_log_level() >= LogLevel::log_debug) {
    auto log_many_lines = [&](std::string prefix, std::string str) {
      std::stringstream ss;
      ss << str;
      std::string line;
      while (std::getline(ss, line, '\n')) {
        LIBNETTEST2_EMIT_DEBUG_EX(owner, "curl: " << prefix << line);
      }
    };
    switch (type) {
      case CURLINFO_TEXT:
        log_many_lines("", std::string{(char *)data, size});
        break;
      case CURLINFO_HEADER_IN:
        log_many_lines("< ", std::string{(char *)data, size});
        break;
      case CURLINFO_DATA_IN:
        LIBNETTEST2_EMIT_DEBUG_EX(owner, "curl: < data{" << size << "}");
        break;
      case CURLINFO_SSL_DATA_IN:
        LIBNETTEST2_EMIT_DEBUG_EX(owner, "curl: < ssl_data{" << size << "}");
        break;
      case CURLINFO_HEADER_OUT:
        log_many_lines("> ", std::string{(char *)data, size});
        break;
      case CURLINFO_DATA_OUT:
        LIBNETTEST2_EMIT_DEBUG_EX(owner, "curl: > data{" << size << "}");
        break;
      case CURLINFO_SSL_DATA_OUT:
        LIBNETTEST2_EMIT_DEBUG_EX(owner, "curl: > ssl_data{" << size << "}");
        break;
      case CURLINFO_END:
        /* NOTHING */
        break;
    }
  }
  // Note regarding counting TLS data
  // ````````````````````````````````
  //
  // I am using the technique recommended by Stenberg on Stack Overflow [1]. It
  // was initially not clear to me whether cURL using OpenSSL counted the data
  // twice, once encrypted and once in clear text. However, using cURL using
  // OpenSSL on Linux and reading the source code [2] helped me to clarify that
  // it does indeed the right thing [3]. When using other TLS backends, it may
  // be that TLS data is not counted, but that's okay since we tell to users
  // that this is an estimate of the amount of used data.
  //
  // Notes
  // `````
  //
  // .. [1] https://stackoverflow.com/a/26905099
  //
  // .. [2] https://github.com/curl/curl/blob/6684653b/lib/vtls/openssl.c#L2295
  //
  // .. [3] the SSL function used is SSL_CTX_set_msg_callback which "[is] never
  //        [called for] application_data(23) because the callback will only be
  //        called for protocol messages" [4].
  //
  // .. [4] https://www.openssl.org/docs/man1.1.0/ssl/SSL_CTX_set_msg_callback.html
  switch (type) {
    case CURLINFO_HEADER_IN:
    case CURLINFO_DATA_IN:
    case CURLINFO_SSL_DATA_IN:
      info->bytes_down += size;
      break;
    case CURLINFO_HEADER_OUT:
    case CURLINFO_DATA_OUT:
    case CURLINFO_SSL_DATA_OUT:
      info->bytes_up += size;
      break;
    case CURLINFO_TEXT:
    case CURLINFO_END:
      /* NOTHING */
      break;
  }
  return 0;
}

}  // extern "C"
namespace measurement_kit {
namespace libnettest2 {

bool Runner::curlx_common(UniqueCurlx &handle,
                          std::string url,
                          long timeout,
                          std::string *responsebody,
                          BytesInfo *info,
                          ErrContext *err) const noexcept {
  if (responsebody == nullptr || info == nullptr || err == nullptr) {
    return false;
  }
  *responsebody = "";
  if (::curl_easy_setopt(handle.get(), CURLOPT_URL, url.data()) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_URL) failed");
    return false;
  }
  if (::curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION,
                         libnettest2_curl_stringstream_callback) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_WRITEFUNCTION) failed");
    return false;
  }
  std::stringstream ss;
  if (::curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &ss) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_WRITEDATA) failed");
    return false;
  }
  if (::curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_TIMEOUT) failed");
    return false;
  }
  if (::curl_easy_setopt(handle.get(), CURLOPT_DEBUGFUNCTION,
                         libnettest2_curl_debugfn) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_DEBUGFUNCTION) failed");
    return false;
  }
  BytesInfoWrapper w;
  w.owner = this;
  w.info = info;
  if (::curl_easy_setopt(handle.get(), CURLOPT_DEBUGDATA, &w) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_DEBUGDATA) failed");
    return false;
  }
  if (::curl_easy_setopt(handle.get(), CURLOPT_VERBOSE, 1L) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_VERBOSE) failed");
    return false;
  }
  if (::curl_easy_setopt(handle.get(), CURLOPT_FAILONERROR, 1L) != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING(
        "curlx_common: curl_easy_setopt(CURLOPT_FAILONERROR) failed");
    return false;
  }
  auto curle = ::curl_easy_perform(handle.get());
  if (curle != CURLE_OK) {
    LIBNETTEST2_EMIT_WARNING("curlx_common: curl_easy_perform() failed");
    // Here's a reasonable assumption: in general the most likely cURL API that
    // could fail is curl_perform(). So just gather the error in here.
    err->code = curle;
    err->library_name = "libcurl";
    err->library_version = LIBCURL_VERSION;
    err->reason = ::curl_easy_strerror(curle);
    return false;
  }
  *responsebody = ss.str();
  return true;
}

#endif  // LIBNETTEST2_NO_INLINE_IMPL
}  // namespace libnettest2
}  // namespace measurement_kit
#endif
