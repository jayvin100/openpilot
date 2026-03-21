#include "tools/jotpluggler/sketch_layout.h"
#include "tools/jotpluggler/dbc_core.h"

#include <capnp/dynamic.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <limits>
#include <numeric>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/util.h"
#include "third_party/json11/json11.hpp"
#include "tools/replay/logreader.h"
#include "tools/replay/py_downloader.h"

namespace jotpluggler {
namespace fs = std::filesystem;

namespace {

struct RouteSelection {
  std::string dongle_id;
  std::string timestamp;
  int begin_segment = 0;
  int end_segment = -1;
  std::string canonical_name;
};

struct SegmentLogs {
  std::string rlog;
  std::string qlog;
  std::string fcamera;
};

enum class ScalarKind {
  None,
  Bool,
  Int,
  UInt,
  Float,
  Enum,
};

enum class ResolvedNodeKind {
  Ignore,
  Scalar,
  Struct,
  List,
};

struct ResolvedNode {
  ResolvedNodeKind kind = ResolvedNodeKind::Ignore;
  ScalarKind scalar_kind = ScalarKind::None;
  int fixed_slot = -1;
  bool has_field = false;
  capnp::StructSchema::Field field;
  std::string segment;
  std::string path;
  bool skip_large_scalar_list = false;
  std::vector<ResolvedNode> children;
  std::unique_ptr<ResolvedNode> element;
};

struct ResolvedService {
  uint16_t event_which = 0;
  capnp::StructSchema::Field union_field;
  std::string service_name;
  int valid_slot = -1;
  int log_mono_time_slot = -1;
  int seconds_slot = -1;
  ResolvedNode payload;
};

struct SchemaIndex {
  std::vector<std::optional<ResolvedService>> by_which;
  size_t fixed_series_count = 0;
  std::vector<std::string> fixed_paths;

  static const SchemaIndex &instance();
};

constexpr size_t kInvalidDynamicSlot = std::numeric_limits<size_t>::max();

struct SeriesAccumulator {
  explicit SeriesAccumulator(size_t fixed_count = 0) : fixed_series(fixed_count) {}

  std::vector<RouteSeries> fixed_series;
  std::vector<RouteSeries> dynamic_series;
  std::unordered_map<std::string, size_t> dynamic_slots;
  std::unordered_map<std::string, std::vector<size_t>> list_scalar_slots;
  std::unordered_map<std::string, EnumInfo> enum_info;
};

struct LoadedRouteArtifacts {
  std::vector<RouteSeries> series;
  std::vector<LogEntry> logs;
  std::unordered_map<std::string, EnumInfo> enum_info;
};

struct RouteMetadata {
  std::string car_fingerprint;
};

struct LoadStats {
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  struct SegmentStats {
    int segment_number = -1;
    std::string log_path;
    double download_seconds = 0.0;
    double decompress_seconds = 0.0;
    double parse_seconds = 0.0;
    double extract_seconds = 0.0;
    size_t compressed_bytes = 0;
    size_t decompressed_bytes = 0;
    size_t event_count = 0;
    size_t series_count = 0;
    bool failed = false;
  };

  explicit LoadStats(const RouteLoadProgressCallback &callback) : progress(callback) {}

  void publish(RouteLoadStage stage, size_t segment_index, const std::string &segment_name) {
    if (!progress) {
      return;
    }
    RouteLoadProgress update;
    update.stage = stage;
    update.segment_index = segment_index;
    update.segment_count = segment_count;
    update.current = stage == RouteLoadStage::DownloadingSegment
      ? segments_downloaded.load()
      : segments_parsed.load();
    update.total = total_segments.load();
    update.segments_downloaded = segments_downloaded.load();
    update.segments_parsed = segments_parsed.load();
    update.total_segments = total_segments.load();
    update.bytes_downloaded = bytes_downloaded.load();
    update.num_workers = num_workers;
    update.segment_name = segment_name;
    std::lock_guard<std::mutex> lock(progress_mutex);
    progress(update);
  }

  void print_summary(size_t final_series_count) const {
    const auto duration_seconds = [](TimePoint start, TimePoint end) {
      return std::chrono::duration<double>(end - start).count();
    };

    double total_download = 0.0;
    double total_decompress = 0.0;
    double total_parse = 0.0;
    double total_extract = 0.0;
    size_t total_events = 0;
    size_t total_compressed = 0;
    size_t total_decompressed = 0;
    for (const SegmentStats &segment : segments) {
      total_download += segment.download_seconds;
      total_decompress += segment.decompress_seconds;
      total_parse += segment.parse_seconds;
      total_extract += segment.extract_seconds;
      total_events += segment.event_count;
      total_compressed += segment.compressed_bytes;
      total_decompressed += segment.decompressed_bytes;
    }

    std::cerr << std::fixed << std::setprecision(1);
    std::cerr << "route loaded in " << duration_seconds(load_start, load_end)
              << "s (" << segment_count << " segments, worker budget " << num_workers << ")\n";
    std::cerr << "  resolve:      " << duration_seconds(load_start, resolve_end) << "s\n";
    std::cerr << "  fetch comp:   " << total_download << "s total (" << (segment_count == 0 ? 0.0 : total_download / segment_count)
              << "s avg, " << (static_cast<double>(total_compressed) / (1024.0 * 1024.0)) << " MB)\n";
    std::cerr << "  decompress:   " << total_decompress << "s total (" << (static_cast<double>(total_decompressed) / (1024.0 * 1024.0))
              << " MB)\n";
    std::cerr << "  capnp parse:  " << total_parse << "s total (" << total_events << " events)\n";
    std::cerr << "  series ext:   " << total_extract << "s total\n";
    std::cerr << "  merge:        " << duration_seconds(merge_start, merge_end) << "s\n";
    std::cerr << "  series:       " << final_series_count << " paths\n";
    std::cerr << "  per segment:\n";
    for (const SegmentStats &segment : segments) {
      std::cerr << "    seg " << std::setw(2) << segment.segment_number << ": "
                << (segment.failed ? "FAILED" : "fetch " + std::to_string(segment.download_seconds)
                  + "s  decompress " + std::to_string(segment.decompress_seconds)
                  + "s  parse " + std::to_string(segment.parse_seconds)
                  + "s  extract " + std::to_string(segment.extract_seconds)
                  + "s (" + std::to_string(segment.event_count) + " events, "
                  + std::to_string(segment.series_count) + " series)") << "\n";
    }
    std::cerr.unsetf(std::ios::floatfield);
  }

  TimePoint load_start;
  TimePoint resolve_end;
  TimePoint merge_start;
  TimePoint merge_end;
  TimePoint load_end;
  size_t segment_count = 0;
  int num_workers = 1;
  std::vector<SegmentStats> segments;
  std::atomic<size_t> segments_downloaded{0};
  std::atomic<size_t> segments_parsed{0};
  std::atomic<size_t> total_segments{0};
  std::atomic<uint64_t> bytes_downloaded{0};
  RouteLoadProgressCallback progress;
  mutable std::mutex progress_mutex;
};

std::string curve_label(std::string_view series_name) {
  return std::string(series_name.empty() ? std::string_view{"plot"} : series_name);
}

bool parse_segment_number(std::string_view value, int *out) {
  if (value.empty()) {
    return false;
  }
  char *end = nullptr;
  const long parsed = std::strtol(std::string(value).c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = static_cast<int>(parsed);
  return true;
}

RouteSelection parse_route_selection(const std::string &route_name) {
  RouteSelection route = {};
  static const std::regex pattern(R"(^(([a-z0-9]{16})[|_/])?(.{20})((--|/)((-?\d+(:(-?\d+)?)?)|(:-?\d+)))?$)");
  std::smatch match;
  if (!std::regex_match(route_name, match, pattern)) {
    return route;
  }

  route.dongle_id = match[2].str();
  route.timestamp = match[3].str();
  route.canonical_name = route.dongle_id + "|" + route.timestamp;

  const std::string separator = match[5].str();
  const std::string range_str = match[6].str();
  if (!range_str.empty()) {
    if (separator == "/") {
      size_t pos = range_str.find(':');
      int begin_segment = 0;
      if (!parse_segment_number(range_str.substr(0, pos), &begin_segment)) {
        return {};
      }
      route.begin_segment = begin_segment;
      route.end_segment = begin_segment;
      if (pos != std::string::npos) {
        int end_segment = -1;
        const std::string end_str = range_str.substr(pos + 1);
        if (!end_str.empty() && !parse_segment_number(end_str, &end_segment)) {
          return {};
        }
        route.end_segment = end_str.empty() ? -1 : end_segment;
      }
    } else if (separator == "--") {
      int begin_segment = 0;
      if (!parse_segment_number(range_str, &begin_segment)) {
        return {};
      }
      route.begin_segment = begin_segment;
    }
  }
  return route;
}

void add_log_file_to_segments(std::map<int, SegmentLogs> *segments, int segment_number, const std::string &file) {
  std::string name = extractFileName(file);
  const size_t pos = name.find_last_of("--");
  name = pos != std::string::npos ? name.substr(pos + 2) : name;
  SegmentLogs &segment = (*segments)[segment_number];
  if (name == "rlog.bz2" || name == "rlog.zst" || name == "rlog") {
    segment.rlog = file;
  } else if (name == "qlog.bz2" || name == "qlog.zst" || name == "qlog") {
    segment.qlog = file;
  } else if (name == "fcamera.hevc") {
    segment.fcamera = file;
  }
}

std::map<int, SegmentLogs> trim_segments(std::map<int, SegmentLogs> segments, const RouteSelection &route) {
  if (route.begin_segment > 0) {
    segments.erase(segments.begin(), segments.lower_bound(route.begin_segment));
  }
  if (route.end_segment >= 0) {
    segments.erase(segments.upper_bound(route.end_segment), segments.end());
  }
  return segments;
}

std::map<int, SegmentLogs> load_segments_from_json(const json11::Json &json) {
  std::map<int, SegmentLogs> segments;
  static const std::regex rx(R"(\/(\d+)\/)");
  for (const auto &value : json.object_items()) {
    for (const auto &url : value.second.array_items()) {
      const std::string url_str = url.string_value();
      std::smatch match;
      if (!std::regex_search(url_str, match, rx)) {
        continue;
      }
      add_log_file_to_segments(&segments, std::stoi(match[1].str()), url_str);
    }
  }
  return segments;
}

std::map<int, SegmentLogs> load_segments_from_server(const RouteSelection &route) {
  const std::string result = PyDownloader::getRouteFiles(route.canonical_name);
  if (result.empty()) {
    throw std::runtime_error("Failed to fetch route files for " + route.canonical_name);
  }

  std::string parse_error;
  const auto json = json11::Json::parse(result, parse_error);
  if (!parse_error.empty()) {
    throw std::runtime_error("Failed to parse route file list for " + route.canonical_name);
  }
  if (json.is_object() && json["error"].is_string()) {
    throw std::runtime_error("Route API error for " + route.canonical_name + ": " + json["error"].string_value());
  }
  return load_segments_from_json(json);
}

std::map<int, SegmentLogs> load_segments_from_local(const RouteSelection &route, const std::string &data_dir) {
  std::map<int, SegmentLogs> segments;
  const std::string pattern = route.timestamp + "--";
  for (const auto &entry : fs::directory_iterator(data_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string dirname = entry.path().filename().string();
    if (dirname.find(pattern) == std::string::npos) {
      continue;
    }
    const size_t marker = dirname.rfind("--");
    if (marker == std::string::npos) {
      continue;
    }
    int segment_number = 0;
    if (!parse_segment_number(dirname.substr(marker + 2), &segment_number)) {
      continue;
    }
    for (const auto &file : fs::directory_iterator(entry.path())) {
      if (file.is_regular_file()) {
        add_log_file_to_segments(&segments, segment_number, file.path().string());
      }
    }
  }
  return segments;
}

std::map<int, SegmentLogs> resolve_route_segments(const std::string &route_name, const std::string &data_dir) {
  const RouteSelection route = parse_route_selection(route_name);
  if (route.canonical_name.empty() || (data_dir.empty() && route.dongle_id.empty())) {
    throw std::runtime_error("Invalid route format: " + route_name);
  }

  std::map<int, SegmentLogs> segments = data_dir.empty()
    ? load_segments_from_server(route)
    : load_segments_from_local(route, data_dir);
  segments = trim_segments(std::move(segments), route);
  if (segments.empty()) {
    throw std::runtime_error("No log segments found for " + route_name);
  }
  return segments;
}

std::string basedir() {
  if (const char *env_basedir = std::getenv("BASEDIR"); env_basedir != nullptr && std::strlen(env_basedir) > 0) {
    return env_basedir;
  }
#ifdef JOTP_REPO_ROOT
  return JOTP_REPO_ROOT;
#else
  return fs::current_path().string();
#endif
}

const std::unordered_map<std::string, std::string> &car_fingerprint_to_dbc_map() {
  static const std::unordered_map<std::string, std::string> map = []() {
    std::unordered_map<std::string, std::string> out;
    const fs::path json_path = fs::path(basedir()) / "tools" / "cabana" / "dbc" / "car_fingerprint_to_dbc.json";
    const std::string raw = util::read_file(json_path.string());
    if (raw.empty()) {
      return out;
    }
    std::string parse_error;
    const json11::Json parsed = json11::Json::parse(raw, parse_error);
    if (!parse_error.empty() || !parsed.is_object()) {
      return out;
    }
    for (const auto &[fingerprint, dbc] : parsed.object_items()) {
      if (dbc.is_string() && !dbc.string_value().empty()) {
        out.emplace(fingerprint, dbc.string_value());
      }
    }
    return out;
  }();
  return map;
}

std::string detect_dbc_for_fingerprint(std::string_view car_fingerprint) {
  if (car_fingerprint.empty()) {
    return {};
  }
  const auto &map = car_fingerprint_to_dbc_map();
  auto it = map.find(std::string(car_fingerprint));
  return it == map.end() ? std::string() : it->second;
}

std::vector<std::string> available_dbc_names_impl() {
  std::set<std::string> names;
  for (const fs::path &dbc_dir : {
         fs::path(basedir()) / "opendbc" / "dbc",
         fs::path(basedir()) / "tools" / "jotpluggler" / "generated_dbcs",
       }) {
    if (fs::exists(dbc_dir) && fs::is_directory(dbc_dir)) {
      for (const auto &entry : fs::directory_iterator(dbc_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dbc") {
          continue;
        }
        names.insert(entry.path().stem().string());
      }
    }
  }
  for (const auto &[_, dbc_name] : car_fingerprint_to_dbc_map()) {
    if (!dbc_name.empty()) {
      names.insert(dbc_name);
    }
  }
  return std::vector<std::string>(names.begin(), names.end());
}

fs::path resolve_dbc_path(const std::string &dbc_name) {
  for (const fs::path &candidate : {
         fs::path(basedir()) / "opendbc" / "dbc" / (dbc_name + ".dbc"),
         fs::path(basedir()) / "tools" / "jotpluggler" / "generated_dbcs" / (dbc_name + ".dbc"),
       }) {
    if (fs::exists(candidate)) {
      return candidate;
    }
  }
  throw std::runtime_error("DBC not found: " + dbc_name);
}

std::array<uint8_t, 3> parse_color(std::string_view color) {
  if (!color.empty() && color.front() == '#') {
    color.remove_prefix(1);
  }
  if (color.size() != 6) {
    return {160, 170, 180};
  }

  std::array<uint8_t, 3> out = {};
  for (size_t i = 0; i < 3; ++i) {
    const std::string byte(color.substr(i * 2, 2));
    char *end = nullptr;
    const long parsed = std::strtol(byte.c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || parsed < 0 || parsed > 255) {
      return {160, 170, 180};
    }
    out[i] = static_cast<uint8_t>(parsed);
  }
  return out;
}

uint8_t android_priority_to_level(uint8_t priority) {
  switch (priority) {
    case 2:
    case 3:
      return 10;
    case 4:
      return 20;
    case 5:
      return 30;
    case 6:
      return 40;
    case 7:
    default:
      return 50;
  }
}

uint8_t alert_status_to_level(cereal::SelfdriveState::AlertStatus status) {
  switch (status) {
    case cereal::SelfdriveState::AlertStatus::NORMAL:
      return 20;
    case cereal::SelfdriveState::AlertStatus::USER_PROMPT:
      return 30;
    case cereal::SelfdriveState::AlertStatus::CRITICAL:
      return 40;
  }
  return 20;
}

double android_wall_time_seconds(uint64_t timestamp) {
  if (timestamp == 0) {
    return 0.0;
  }
  if (timestamp > 1000000000000ULL) {
    return static_cast<double>(timestamp) / 1.0e9;
  }
  if (timestamp > 1000000000ULL) {
    return static_cast<double>(timestamp) / 1.0e6;
  }
  return static_cast<double>(timestamp);
}

std::optional<uint64_t> json_u64_value(const json11::Json &value) {
  if (value.is_number()) {
    const double number = value.number_value();
    if (number >= 0.0) {
      return static_cast<uint64_t>(number);
    }
  }
  if (value.is_string()) {
    try {
      return static_cast<uint64_t>(std::stoull(value.string_value()));
    } catch (...) {
    }
  }
  return std::nullopt;
}

std::optional<int> json_int_value(const json11::Json &value) {
  if (value.is_number()) {
    return value.int_value();
  }
  if (value.is_string()) {
    try {
      return std::stoi(value.string_value());
    } catch (...) {
    }
  }
  return std::nullopt;
}

std::string json_value_for_log(const json11::Json &value) {
  if (value.is_string()) {
    return value.string_value();
  }
  if (value.is_bool()) {
    return value.bool_value() ? "true" : "false";
  }
  if (value.is_number()) {
    return value.dump();
  }
  return value.dump();
}

std::string format_journal_context(const json11::Json &parsed, int pid, int tid) {
  std::vector<std::string> lines;
  if (pid != 0 || tid != 0) {
    lines.push_back("pid=" + std::to_string(pid) + ", tid=" + std::to_string(tid));
  }

  const std::array<const char *, 5> preferred_keys = {
    "_HOSTNAME",
    "_TRANSPORT",
    "PRIORITY",
    "SYSLOG_FACILITY",
    "__MONOTONIC_TIMESTAMP",
  };
  for (const char *key : preferred_keys) {
    const json11::Json &value = parsed[key];
    if (!value.is_null()) {
      lines.push_back(std::string(key) + "=" + json_value_for_log(value));
    }
  }
  return join(lines, "\n");
}

std::string alert_message_text(const cereal::SelfdriveState::Reader &state) {
  std::string text = state.getAlertText1().cStr();
  const std::string text2 = state.getAlertText2().cStr();
  if (!text2.empty()) {
    text += " - " + text2;
  }
  return text;
}

bool same_log_entry(const LogEntry &a, const LogEntry &b) {
  return a.mono_time == b.mono_time
      && a.level == b.level
      && a.source == b.source
      && a.func == b.func
      && a.message == b.message
      && a.context == b.context
      && a.origin == b.origin;
}

void append_log_event(cereal::Event::Which which,
                      const cereal::Event::Reader &event,
                      double time_offset,
                      std::vector<LogEntry> *logs,
                      std::string *last_alert_key) {
  const double boot_time = static_cast<double>(event.getLogMonoTime()) / 1.0e9;
  const double mono_time = boot_time - time_offset;

  switch (which) {
    case cereal::Event::Which::LOG_MESSAGE:
    case cereal::Event::Which::ERROR_LOG_MESSAGE: {
      const std::string raw = which == cereal::Event::Which::LOG_MESSAGE
        ? event.getLogMessage().cStr()
        : event.getErrorLogMessage().cStr();
      std::string parse_error;
      const json11::Json parsed = json11::Json::parse(raw, parse_error);

      LogEntry entry;
      entry.mono_time = mono_time;
      entry.boot_time = boot_time;
      entry.origin = LogOrigin::Log;
      entry.level = which == cereal::Event::Which::ERROR_LOG_MESSAGE ? 40 : 20;
      entry.source = "log";
      entry.message = raw;
      if (parse_error.empty() && parsed.is_object()) {
        entry.wall_time = parsed["created"].number_value();
        if (parsed["levelnum"].is_number()) {
          entry.level = static_cast<uint8_t>(parsed["levelnum"].int_value());
        }
        const std::string filename = parsed["filename"].string_value();
        const int lineno = parsed["lineno"].is_number() ? parsed["lineno"].int_value() : 0;
        entry.source = filename.empty() ? "log" : filename + (lineno > 0 ? ":" + std::to_string(lineno) : "");
        entry.func = parsed["funcname"].string_value();
        if (parsed["msg"].is_string()) {
          entry.message = parsed["msg"].string_value();
        }
        if (!parsed["ctx"].is_null()) {
          entry.context = parsed["ctx"].dump();
        }
      }
      logs->push_back(std::move(entry));
      break;
    }
    case cereal::Event::Which::ANDROID_LOG: {
      const auto android = event.getAndroidLog();
      LogEntry entry;
      entry.mono_time = mono_time;
      entry.boot_time = boot_time;
      entry.wall_time = android_wall_time_seconds(android.getTs());
      entry.level = android_priority_to_level(android.getPriority());
      entry.source = android.hasTag() ? android.getTag().cStr() : "android";
      entry.message = android.hasMessage() ? android.getMessage().cStr() : std::string();
      entry.context = "pid=" + std::to_string(android.getPid()) + ", tid=" + std::to_string(android.getTid());
      if (!entry.message.empty()) {
        std::string parse_error;
        const json11::Json parsed = json11::Json::parse(entry.message, parse_error);
        if (parse_error.empty() && parsed.is_object()) {
          if (parsed["MESSAGE"].is_string()) {
            entry.message = parsed["MESSAGE"].string_value();
          }
          if (parsed["SYSLOG_IDENTIFIER"].is_string() && !parsed["SYSLOG_IDENTIFIER"].string_value().empty()) {
            entry.source = parsed["SYSLOG_IDENTIFIER"].string_value();
          }
          if (const std::optional<int> priority = json_int_value(parsed["PRIORITY"]); priority.has_value()) {
            entry.level = android_priority_to_level(priority.value());
          }
          if (const std::optional<uint64_t> wall_ts = json_u64_value(parsed["__REALTIME_TIMESTAMP"]); wall_ts.has_value()) {
            entry.wall_time = android_wall_time_seconds(wall_ts.value());
          }
          entry.context = format_journal_context(parsed, android.getPid(), android.getTid());
        }
      }
      entry.origin = LogOrigin::Android;
      logs->push_back(std::move(entry));
      break;
    }
    case cereal::Event::Which::SELFDRIVE_STATE: {
      const auto state = event.getSelfdriveState();
      const std::string alert_type = state.getAlertType().cStr();
      const std::string alert_text1 = state.getAlertText1().cStr();
      if (alert_text1.empty() && alert_type.empty()) {
        break;
      }
      const std::string current_key = alert_type + "\n" + alert_text1 + "\n" + std::string(state.getAlertText2().cStr());
      if (last_alert_key != nullptr && current_key == *last_alert_key) {
        break;
      }
      if (last_alert_key != nullptr) {
        *last_alert_key = current_key;
      }

      LogEntry entry;
      entry.mono_time = mono_time;
      entry.boot_time = boot_time;
      entry.level = alert_status_to_level(state.getAlertStatus());
      entry.source = "alert";
      entry.func = alert_type;
      entry.message = alert_message_text(state);
      entry.origin = LogOrigin::Alert;
      logs->push_back(std::move(entry));
      break;
    }
    default:
      break;
  }
}

std::vector<LogEntry> extract_segment_logs(const std::vector<Event> &events) {
  std::vector<LogEntry> logs;
  logs.reserve(events.size() / 8);
  std::string last_alert_key;

  for (const Event &event_record : events) {
    capnp::FlatArrayMessageReader event_reader(event_record.data);
    const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
    append_log_event(event_record.which, event, 0.0, &logs, &last_alert_key);
  }

  return logs;
}

RouteMetadata extract_segment_metadata(const std::vector<Event> &events) {
  RouteMetadata metadata;
  for (const Event &event_record : events) {
    if (event_record.which != cereal::Event::Which::CAR_PARAMS) {
      continue;
    }
    capnp::FlatArrayMessageReader event_reader(event_record.data);
    const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
    metadata.car_fingerprint = event.getCarParams().getCarFingerprint().cStr();
    if (!metadata.car_fingerprint.empty()) {
      break;
    }
  }
  return metadata;
}

RouteMetadata detect_route_metadata(const std::map<int, SegmentLogs> &segments) {
  for (const auto &[segment_number, segment] : segments) {
    (void)segment_number;
    const std::string &log_path = !segment.qlog.empty() ? segment.qlog : segment.rlog;
    if (log_path.empty()) {
      continue;
    }
    LogReader reader;
    if (!reader.load(log_path, nullptr, true)) {
      continue;
    }
    RouteMetadata metadata = extract_segment_metadata(reader.events);
    if (!metadata.car_fingerprint.empty()) {
      return metadata;
    }
  }
  return {};
}

std::vector<double> normalize_sizes(const json11::Json &sizes_json, size_t child_count) {
  std::vector<double> parsed;
  if (sizes_json.is_array()) {
    for (const json11::Json &value : sizes_json.array_items()) {
      if (value.is_number()) {
        parsed.push_back(std::max(value.number_value(), 0.0));
      }
    }
  }

  if (parsed.size() != child_count || child_count == 0) {
    return std::vector<double>(child_count, child_count == 0 ? 0.0 : 1.0 / static_cast<double>(child_count));
  }

  const double total = std::accumulate(parsed.begin(), parsed.end(), 0.0);
  if (total <= 0.0) {
    return std::vector<double>(child_count, 1.0 / static_cast<double>(child_count));
  }
  for (double &value : parsed) {
    value /= total;
  }
  return parsed;
}

PlotRange parse_range(const json11::Json &pane_node) {
  PlotRange range;
  const json11::Json &range_node = pane_node["range"];
  if (range_node.is_object()) {
    range.valid = true;
    range.left = range_node["left"].number_value();
    range.right = range_node["right"].number_value();
    range.bottom = range_node["bottom"].number_value();
    range.top = range_node["top"].is_number() ? range_node["top"].number_value() : 1.0;
  }
  const json11::Json &limit_y_node = pane_node["y_limits"];
  if (limit_y_node.is_object()) {
    if (limit_y_node["min"].is_number()) {
      range.has_y_limit_min = true;
      range.y_limit_min = limit_y_node["min"].number_value();
    }
    if (limit_y_node["max"].is_number()) {
      range.has_y_limit_max = true;
      range.y_limit_max = limit_y_node["max"].number_value();
    }
  }
  return range;
}

Curve parse_curve(const json11::Json &curve_node) {
  Curve curve;
  curve.name = curve_node["name"].string_value();
  curve.label = curve_label(curve.name);
  curve.color = parse_color(curve_node["color"].string_value());

  const std::string transform_name = curve_node["transform"].string_value();
  if (transform_name == "derivative") {
    curve.derivative = true;
    curve.derivative_dt = curve_node["derivative_dt"].is_number() ? curve_node["derivative_dt"].number_value() : 0.0;
  } else if (transform_name == "scale") {
    curve.value_scale = curve_node["scale"].is_number() ? curve_node["scale"].number_value() : 1.0;
    curve.value_offset = curve_node["offset"].is_number() ? curve_node["offset"].number_value() : 0.0;
  }
  const json11::Json &custom_node = curve_node["custom_python"];
  if (custom_node.is_object()) {
    CustomPythonSeries spec;
    spec.linked_source = custom_node["linked_source"].string_value();
    spec.globals_code = custom_node["globals_code"].string_value();
    spec.function_code = custom_node["function_code"].string_value();
    for (const json11::Json &source : custom_node["additional_sources"].array_items()) {
      if (source.is_string()) {
        spec.additional_sources.push_back(source.string_value());
      }
    }
    curve.custom_python = std::move(spec);
  }
  return curve;
}

std::string pane_title(const json11::Json &dock_area_node) {
  const std::string raw = dock_area_node["title"].string_value();
  return raw.empty() ? "..." : raw;
}

Pane parse_dock_area(const json11::Json &dock_area_node) {
  Pane pane;
  pane.range = parse_range(dock_area_node);
  const json11::Json &curves_node = dock_area_node["curves"];
  if (curves_node.is_array()) {
    for (const json11::Json &curve_node : curves_node.array_items()) {
      if (curve_node.is_object()) {
        pane.curves.push_back(parse_curve(curve_node));
      }
    }
  }
  pane.title = pane_title(dock_area_node);
  return pane;
}

WorkspaceNode parse_workspace_node(const json11::Json &node, WorkspaceTab *tab) {
  WorkspaceNode workspace_node;
  if (!node.is_object()) {
    return workspace_node;
  }

  if (node["curves"].is_array()) {
    workspace_node.is_pane = true;
    workspace_node.pane_index = static_cast<int>(tab->panes.size());
    tab->panes.push_back(parse_dock_area(node));
    return workspace_node;
  }

  const json11::Json &children_node = node["children"];
  if (!children_node.is_array()) {
    return workspace_node;
  }

  const std::vector<json11::Json> children = children_node.array_items();
  if (children.empty()) {
    return workspace_node;
  }

  const std::string split = node["split"].string_value();
  workspace_node.orientation = split == "vertical" ? SplitOrientation::Vertical : SplitOrientation::Horizontal;
  const std::vector<double> sizes = normalize_sizes(node["sizes"], children.size());
  workspace_node.sizes.reserve(sizes.size());
  workspace_node.children.reserve(children.size());
  for (size_t i = 0; i < children.size(); ++i) {
    workspace_node.sizes.push_back(static_cast<float>(sizes[i]));
    workspace_node.children.push_back(parse_workspace_node(children[i], tab));
  }
  return workspace_node;
}

WorkspaceTab parse_tab(const json11::Json &tab, const fs::path &layout_path) {
  WorkspaceTab workspace_tab;
  workspace_tab.tab_name = tab["name"].string_value().empty() ? "tab1" : tab["name"].string_value();
  const json11::Json &dock_root = tab["root"];
  if (!dock_root.is_object()) {
    throw std::runtime_error("Layout tab has no dock content: " + layout_path.string());
  }
  workspace_tab.root = parse_workspace_node(dock_root, &workspace_tab);
  return workspace_tab;
}

SketchLayout parse_layout(const fs::path &layout_path) {
  const std::string text = util::read_file(layout_path.string());
  if (text.empty()) {
    throw std::runtime_error("Failed to read layout JSON: " + layout_path.string());
  }

  std::string parse_error;
  const json11::Json root = json11::Json::parse(text, parse_error);
  if (!parse_error.empty() || !root.is_object()) {
    throw std::runtime_error("Failed to parse layout JSON: " + layout_path.string());
  }
  SketchLayout layout;
  for (const json11::Json &tab : root["tabs"].array_items()) {
    if (tab.is_object()) {
      layout.tabs.push_back(parse_tab(tab, layout_path));
    }
  }
  if (layout.tabs.empty()) {
    throw std::runtime_error("Layout has no tabs: " + layout_path.string());
  }
  const json11::Json &tab_index = root["current_tab_index"].is_number() ? root["current_tab_index"] : root["currentTabIndex"];
  layout.current_tab_index = std::clamp(tab_index.is_number() ? tab_index.int_value() : 0,
                                        0,
                                        static_cast<int>(layout.tabs.size()) - 1);
  return layout;
}

ScalarKind scalar_kind_for_type(const capnp::Type &type) {
  if (type.isBool()) {
    return ScalarKind::Bool;
  }
  if (type.isInt8() || type.isInt16() || type.isInt32() || type.isInt64()) {
    return ScalarKind::Int;
  }
  if (type.isUInt8() || type.isUInt16() || type.isUInt32() || type.isUInt64()) {
    return ScalarKind::UInt;
  }
  if (type.isFloat32() || type.isFloat64()) {
    return ScalarKind::Float;
  }
  if (type.isEnum()) {
    return ScalarKind::Enum;
  }
  return ScalarKind::None;
}

ResolvedNode build_resolved_type(const capnp::Type &type,
                                 bool has_field,
                                 capnp::StructSchema::Field field,
                                 std::string segment,
                                 std::string path,
                                 size_t *next_fixed_slot,
                                 std::vector<std::string> *fixed_paths,
                                 bool dynamic_path = false) {
  ResolvedNode node;
  node.has_field = has_field;
  node.field = field;
  node.segment = std::move(segment);
  node.path = std::move(path);
  node.scalar_kind = scalar_kind_for_type(type);
  if (node.scalar_kind != ScalarKind::None) {
    node.kind = ResolvedNodeKind::Scalar;
    if (!dynamic_path) {
      node.fixed_slot = static_cast<int>((*next_fixed_slot)++);
      fixed_paths->push_back(node.path);
    }
    return node;
  }

  if (type.isStruct()) {
    node.kind = ResolvedNodeKind::Struct;
    for (auto child : type.asStruct().getFields()) {
      const std::string child_segment = child.getProto().getName().cStr();
      node.children.push_back(build_resolved_type(
        child.getType(),
        true,
        child,
        child_segment,
        node.path + "/" + child_segment,
        next_fixed_slot,
        fixed_paths,
        dynamic_path));
    }
    return node;
  }

  if (type.isList()) {
    const capnp::Type element_type = type.asList().getElementType();
    if (element_type.isText() || element_type.isData() || element_type.isInterface() || element_type.isAnyPointer()) {
      node.kind = ResolvedNodeKind::Ignore;
      return node;
    }
    node.kind = ResolvedNodeKind::List;
    node.skip_large_scalar_list = scalar_kind_for_type(element_type) != ScalarKind::None;
    node.element = std::make_unique<ResolvedNode>(
      build_resolved_type(element_type,
                          false,
                          capnp::StructSchema::Field(),
                          "",
                          node.path,
                          next_fixed_slot,
                          fixed_paths,
                          true));
    return node;
  }

  node.kind = ResolvedNodeKind::Ignore;
  return node;
}

int register_fixed_series_path(const std::string &path,
                               size_t *next_fixed_slot,
                               std::vector<std::string> *fixed_paths) {
  const int slot = static_cast<int>((*next_fixed_slot)++);
  fixed_paths->push_back(path);
  return slot;
}

const SchemaIndex &SchemaIndex::instance() {
  static const SchemaIndex index = [] {
    SchemaIndex out;
    const auto event_schema = capnp::Schema::from<cereal::Event>().asStruct();
    uint16_t max_discriminant = 0;
    for (auto union_field : event_schema.getUnionFields()) {
      max_discriminant = std::max<uint16_t>(max_discriminant, union_field.getProto().getDiscriminantValue());
    }
    out.by_which.resize(static_cast<size_t>(max_discriminant) + 1);
    size_t next_fixed_slot = 0;
    for (auto union_field : event_schema.getUnionFields()) {
      ResolvedService service;
      service.event_which = union_field.getProto().getDiscriminantValue();
      service.union_field = union_field;
      service.service_name = union_field.getProto().getName().cStr();
      service.valid_slot = register_fixed_series_path(
        "/" + service.service_name + "/valid", &next_fixed_slot, &out.fixed_paths);
      service.log_mono_time_slot = register_fixed_series_path(
        "/" + service.service_name + "/logMonoTime", &next_fixed_slot, &out.fixed_paths);
      service.seconds_slot = register_fixed_series_path(
        "/" + service.service_name + "/t", &next_fixed_slot, &out.fixed_paths);
      service.payload = build_resolved_type(
        union_field.getType(),
        false,
        capnp::StructSchema::Field(),
        service.service_name,
        "/" + service.service_name,
        &next_fixed_slot,
        &out.fixed_paths);
      out.by_which[service.event_which] = std::move(service);
    }
    out.fixed_series_count = next_fixed_slot;
    return out;
  }();
  return index;
}

bool is_absolute_curve(const std::string &name) {
  return !name.empty() && name.front() == '/';
}

std::vector<std::string> collect_route_roots(const std::vector<std::string> &paths);

std::optional<double> scalar_value_to_double(const capnp::DynamicValue::Reader &value, ScalarKind kind) {
  switch (kind) {
    case ScalarKind::Bool:
      return value.as<bool>() ? 1.0 : 0.0;
    case ScalarKind::Int:
      return static_cast<double>(value.as<int64_t>());
    case ScalarKind::UInt:
      return static_cast<double>(value.as<uint64_t>());
    case ScalarKind::Float:
      return value.as<double>();
    case ScalarKind::Enum:
      return static_cast<double>(value.as<capnp::DynamicEnum>().getRaw());
    case ScalarKind::None:
      return std::nullopt;
  }
  return std::nullopt;
}

void capture_enum_info(const std::string &path,
                       const capnp::DynamicValue::Reader &value,
                       SeriesAccumulator *series) {
  if (series->enum_info.find(path) != series->enum_info.end()) {
    return;
  }

  const auto dynamic_enum = value.as<capnp::DynamicEnum>();
  EnumInfo info;
  for (auto enumerant : dynamic_enum.getSchema().getEnumerants()) {
    const uint16_t ordinal = enumerant.getOrdinal();
    if (ordinal >= info.names.size()) {
      info.names.resize(static_cast<size_t>(ordinal) + 1);
    }
    info.names[ordinal] = enumerant.getProto().getName().cStr();
  }
  if (!info.names.empty()) {
    series->enum_info.emplace(path, std::move(info));
  }
}

void append_scalar_point(RouteSeries *series,
                         const std::string &path,
                         double tm,
                         double value) {
  if (series->path.empty()) {
    series->path = path;
  }
  series->times.push_back(tm);
  series->values.push_back(value);
}

void append_fixed_scalar_point(RouteSeries *series, double tm, double value) {
  series->times.push_back(tm);
  series->values.push_back(value);
}

SeriesAccumulator make_series_accumulator(const SchemaIndex &schema) {
  SeriesAccumulator out(schema.fixed_series_count);
  for (size_t i = 0; i < schema.fixed_paths.size(); ++i) {
    out.fixed_series[i].path = schema.fixed_paths[i];
  }
  return out;
}

size_t ensure_dynamic_slot(const std::string &path, SeriesAccumulator *series) {
  auto [it, inserted] = series->dynamic_slots.try_emplace(path, series->dynamic_series.size());
  if (inserted) {
    series->dynamic_series.push_back(RouteSeries{it->first});
  }
  return it->second;
}

RouteSeries *ensure_dynamic_series(const std::string &path, SeriesAccumulator *series) {
  return &series->dynamic_series[ensure_dynamic_slot(path, series)];
}

RouteSeries *ensure_list_scalar_series(const std::string &base_path, size_t index, SeriesAccumulator *series) {
  auto [it, _] = series->list_scalar_slots.try_emplace(base_path);
  std::vector<size_t> &slots = it->second;
  if (slots.size() <= index) {
    slots.resize(index + 1, kInvalidDynamicSlot);
  }
  if (slots[index] == kInvalidDynamicSlot) {
    slots[index] = ensure_dynamic_slot(base_path + "/" + std::to_string(index), series);
  }
  return &series->dynamic_series[slots[index]];
}

void append_dynamic_scalar_point(const std::string &path, double tm, double value, SeriesAccumulator *series) {
  append_scalar_point(ensure_dynamic_series(path, series), path, tm, value);
}

void append_scalar_value(const ResolvedNode &node,
                         const std::string *path_override,
                         const capnp::DynamicValue::Reader &raw_value,
                         double tm,
                         double value,
                         SeriesAccumulator *series) {
  if (path_override == nullptr && node.fixed_slot >= 0) {
    if (node.scalar_kind == ScalarKind::Enum) {
      capture_enum_info(node.path, raw_value, series);
    }
    append_fixed_scalar_point(&series->fixed_series[static_cast<size_t>(node.fixed_slot)], tm, value);
    return;
  }

  const std::string &path = path_override != nullptr ? *path_override : node.path;
  if (node.scalar_kind == ScalarKind::Enum) {
    capture_enum_info(path, raw_value, series);
  }
  append_dynamic_scalar_point(path, tm, value, series);
}

void append_fast_node(const ResolvedNode &node,
                      const capnp::DynamicValue::Reader &value,
                      double tm,
                      SeriesAccumulator *series,
                      const std::string *path_override = nullptr) {
  switch (node.kind) {
    case ResolvedNodeKind::Scalar: {
      if (std::optional<double> scalar = scalar_value_to_double(value, node.scalar_kind); scalar.has_value()) {
        append_scalar_value(node, path_override, value, tm, *scalar, series);
      }
      return;
    }
    case ResolvedNodeKind::Struct: {
      const capnp::DynamicStruct::Reader reader = value.as<capnp::DynamicStruct>();
      for (const ResolvedNode &child : node.children) {
        if (!child.has_field || !reader.has(child.field)) {
          continue;
        }
        if (path_override == nullptr) {
          append_fast_node(child, reader.get(child.field), tm, series, nullptr);
        } else {
          const std::string child_path = child.segment.empty() ? *path_override : (*path_override + "/" + child.segment);
          append_fast_node(child, reader.get(child.field), tm, series, &child_path);
        }
      }
      return;
    }
    case ResolvedNodeKind::List: {
      if (!node.element) {
        return;
      }
      const capnp::DynamicList::Reader list = value.as<capnp::DynamicList>();
      if (list.size() == 0) {
        return;
      }
      if (node.skip_large_scalar_list && list.size() > 16) {
        return;
      }
      const std::string &base_path = path_override != nullptr ? *path_override : node.path;
      if (node.element->kind == ResolvedNodeKind::Scalar) {
        for (uint i = 0; i < list.size(); ++i) {
          if (std::optional<double> scalar = scalar_value_to_double(list[i], node.element->scalar_kind); scalar.has_value()) {
            RouteSeries *item_series = ensure_list_scalar_series(base_path, i, series);
            if (node.element->scalar_kind == ScalarKind::Enum && !item_series->path.empty()) {
              capture_enum_info(item_series->path, list[i], series);
            }
            append_fixed_scalar_point(item_series, tm, *scalar);
          }
        }
        return;
      }
      for (uint i = 0; i < list.size(); ++i) {
        const std::string item_path = base_path + "/" + std::to_string(i);
        append_fast_node(*node.element, list[i], tm, series, &item_path);
      }
      return;
    }
    case ResolvedNodeKind::Ignore:
      return;
  }
}

void append_event_fast(cereal::Event::Which which,
                       int32_t eidx_segnum,
                       kj::ArrayPtr<const capnp::word> data,
                       const SchemaIndex &schema,
                       const dbc_core::Database *can_dbc,
                       bool skip_raw_can,
                       double time_offset,
                       SeriesAccumulator *series) {
  if (eidx_segnum != -1) {
    return;
  }
  const uint16_t which_index = static_cast<uint16_t>(which);
  if (which_index >= schema.by_which.size() || !schema.by_which[which_index].has_value()) {
    return;
  }
  const ResolvedService &service = *schema.by_which[which_index];
  capnp::FlatArrayMessageReader event_reader(data);
  const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
  const double tm = static_cast<double>(event.getLogMonoTime()) / 1.0e9 - time_offset;
  append_fixed_scalar_point(&series->fixed_series[static_cast<size_t>(service.valid_slot)],
                            tm,
                            event.getValid() ? 1.0 : 0.0);
  append_fixed_scalar_point(&series->fixed_series[static_cast<size_t>(service.log_mono_time_slot)],
                            tm,
                            static_cast<double>(event.getLogMonoTime()));
  append_fixed_scalar_point(&series->fixed_series[static_cast<size_t>(service.seconds_slot)],
                            tm,
                            tm);
  if (skip_raw_can && (service.service_name == "can" || service.service_name == "sendcan")) {
    auto decode_message = [&](uint8_t bus, uint32_t address, const auto &dat_reader) {
      if (can_dbc == nullptr) {
        return;
      }
      const dbc_core::Message *message = can_dbc->message(address);
      if (message == nullptr) {
        return;
      }
      const auto bytes = dat_reader.begin();
      const uint8_t *raw = bytes;
      const size_t data_size = dat_reader.size();
      const std::string base_path = "/" + service.service_name + "/" + std::to_string(bus) + "/" + message->name;
      for (const dbc_core::Signal &signal : message->signals) {
        std::optional<double> value = dbc_core::signal_value(signal, *message, raw, data_size);
        if (!value.has_value()) {
          continue;
        }
        const std::string path = base_path + "/" + signal.name;
        append_dynamic_scalar_point(path, tm, *value, series);
        if (series->enum_info.find(path) == series->enum_info.end()) {
          std::vector<std::string> enum_names = can_dbc->enum_names(signal);
          if (!enum_names.empty()) {
            series->enum_info.emplace(path, EnumInfo{.names = std::move(enum_names)});
          }
        }
      }
    };
    if (service.service_name == "can") {
      for (const auto &msg : event.getCan()) {
        decode_message(static_cast<uint8_t>(msg.getSrc()), msg.getAddress(), msg.getDat());
      }
    } else {
      for (const auto &msg : event.getSendcan()) {
        decode_message(static_cast<uint8_t>(msg.getSrc()), msg.getAddress(), msg.getDat());
      }
    }
    return;
  }

  const capnp::DynamicStruct::Reader dynamic_event(event);
  append_fast_node(service.payload, dynamic_event.get(service.union_field), tm, series);
}

void append_events_fast_range(const std::vector<Event> &events,
                              size_t begin,
                              size_t end,
                              const SchemaIndex &schema,
                              const dbc_core::Database *can_dbc,
                              bool skip_raw_can,
                              SeriesAccumulator *series) {
  for (size_t i = begin; i < end; ++i) {
    const Event &event_record = events[i];
    append_event_fast(event_record.which,
                      event_record.eidx_segnum,
                      event_record.data,
                      schema,
                      can_dbc,
                      skip_raw_can,
                      0.0,
                      series);
  }
}

void merge_route_series(RouteSeries *dst, RouteSeries *src) {
  if (src->times.empty()) {
    return;
  }
  if (dst->times.empty()) {
    *dst = std::move(*src);
    return;
  }

  dst->times.reserve(dst->times.size() + src->times.size());
  dst->values.reserve(dst->values.size() + src->values.size());
  dst->times.insert(dst->times.end(), src->times.begin(), src->times.end());
  dst->values.insert(dst->values.end(), src->values.begin(), src->values.end());
}

void merge_series_accumulator(SeriesAccumulator *dst, SeriesAccumulator *src) {
  if (dst->fixed_series.size() != src->fixed_series.size()) {
    throw std::runtime_error("Fixed-series slot count mismatch during merge");
  }

  for (size_t i = 0; i < dst->fixed_series.size(); ++i) {
    merge_route_series(&dst->fixed_series[i], &src->fixed_series[i]);
  }
  for (auto &series : src->dynamic_series) {
    if (series.path.empty()) {
      continue;
    }
    RouteSeries &dst_series = dst->dynamic_series[ensure_dynamic_slot(series.path, dst)];
    merge_route_series(&dst_series, &series);
  }
  for (auto &[path, info] : src->enum_info) {
    dst->enum_info.try_emplace(path, std::move(info));
  }
}

size_t populated_series_count(const SeriesAccumulator &series) {
  size_t count = 0;
  for (const RouteSeries &fixed : series.fixed_series) {
    count += !fixed.times.empty();
  }
  for (const RouteSeries &dynamic : series.dynamic_series) {
    count += !dynamic.times.empty();
  }
  return count;
}

bool series_is_sorted(const RouteSeries &series) {
  for (size_t i = 1; i < series.times.size(); ++i) {
    if (series.times[i] < series.times[i - 1]) {
      return false;
    }
  }
  return true;
}

void sort_series_by_time(RouteSeries *series) {
  if (series->times.size() <= 1 || series_is_sorted(*series)) {
    return;
  }
  std::vector<size_t> order(series->times.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return series->times[a] < series->times[b];
  });

  std::vector<double> sorted_times(series->times.size());
  std::vector<double> sorted_values(series->values.size());
  for (size_t i = 0; i < order.size(); ++i) {
    sorted_times[i] = series->times[order[i]];
    sorted_values[i] = series->values[order[i]];
  }
  series->times = std::move(sorted_times);
  series->values = std::move(sorted_values);
}

std::vector<RouteSeries> collect_series(SeriesAccumulator &&series) {
  std::vector<RouteSeries> out;
  out.reserve(series.fixed_series.size() + series.dynamic_series.size());
  for (auto &fixed : series.fixed_series) {
    sort_series_by_time(&fixed);
    if (!fixed.times.empty()) {
      out.push_back(std::move(fixed));
    }
  }
  for (auto &dynamic : series.dynamic_series) {
    sort_series_by_time(&dynamic);
    if (!dynamic.times.empty()) {
      out.push_back(std::move(dynamic));
    }
  }
  return out;
}

RouteData build_route_data(std::vector<RouteSeries> &&series_list,
                           std::vector<LogEntry> &&logs,
                           std::unordered_map<std::string, EnumInfo> &&enum_info,
                           std::string car_fingerprint,
                           std::string dbc_name) {
  RouteData route_data;
  route_data.series.reserve(series_list.size());
  route_data.paths.reserve(series_list.size());
  for (RouteSeries &series : series_list) {
    if (series.times.empty()) {
      continue;
    }
    route_data.has_time_range = true;
    route_data.x_min = route_data.series.empty() ? series.times.front() : std::min(route_data.x_min, series.times.front());
    route_data.x_max = route_data.series.empty() ? series.times.back() : std::max(route_data.x_max, series.times.back());
    route_data.paths.push_back(series.path);
    route_data.series.push_back(std::move(series));
  }

  std::sort(route_data.paths.begin(), route_data.paths.end());
  std::sort(route_data.series.begin(), route_data.series.end(), [](const RouteSeries &a, const RouteSeries &b) {
    return a.path < b.path;
  });
  std::sort(logs.begin(), logs.end(), [](const LogEntry &a, const LogEntry &b) {
    return a.mono_time < b.mono_time;
  });
  logs.erase(std::unique(logs.begin(), logs.end(), [](const LogEntry &a, const LogEntry &b) {
               return same_log_entry(a, b);
             }),
             logs.end());

  std::vector<LogEntry> deduped_logs;
  deduped_logs.reserve(logs.size());
  for (LogEntry &entry : logs) {
    if (!deduped_logs.empty()
        && entry.origin == LogOrigin::Alert
        && deduped_logs.back().origin == LogOrigin::Alert
        && deduped_logs.back().func == entry.func
        && deduped_logs.back().message == entry.message) {
      continue;
    }
    deduped_logs.push_back(std::move(entry));
  }
  route_data.logs = std::move(deduped_logs);

  if (!route_data.has_time_range && !route_data.logs.empty()) {
    route_data.has_time_range = true;
    route_data.x_min = route_data.logs.front().mono_time;
    route_data.x_max = route_data.logs.back().mono_time;
  }

  if (route_data.has_time_range) {
    const double time_offset = route_data.x_min;
    for (RouteSeries &series : route_data.series) {
      for (double &tm : series.times) {
        tm -= time_offset;
      }
    }
    for (LogEntry &entry : route_data.logs) {
      entry.boot_time = entry.mono_time;
      entry.mono_time -= time_offset;
    }
    route_data.x_max -= time_offset;
    route_data.x_min = 0.0;
  }

  route_data.enum_info = std::move(enum_info);
  route_data.car_fingerprint = std::move(car_fingerprint);
  route_data.dbc_name = std::move(dbc_name);
  route_data.roots = collect_route_roots(route_data.paths);
  return route_data;
}

const RouteSeries *find_route_series(const RouteData &route_data, std::string_view path) {
  auto it = std::lower_bound(route_data.series.begin(), route_data.series.end(), path,
                             [](const RouteSeries &series, std::string_view target) {
                               return series.path < target;
                             });
  if (it == route_data.series.end() || it->path != path) {
    return nullptr;
  }
  return &(*it);
}

void build_road_camera_index(const std::map<int, SegmentLogs> &segments, RouteData *route_data) {
  route_data->road_camera = {};
  route_data->road_camera.segment_files.reserve(segments.size());

  std::unordered_set<int> available_segments;
  available_segments.reserve(segments.size());
  for (const auto &[segment_number, segment] : segments) {
    if (segment.fcamera.empty()) {
      continue;
    }
    route_data->road_camera.segment_files.push_back(CameraSegmentFile{
      .segment = segment_number,
      .path = segment.fcamera,
    });
    available_segments.insert(segment_number);
  }
  if (route_data->road_camera.segment_files.empty()) {
    return;
  }

  const RouteSeries *segment_numbers = find_route_series(*route_data, "/roadEncodeIdx/segmentNum");
  const RouteSeries *decode_indices = find_route_series(*route_data, "/roadEncodeIdx/segmentId");
  const RouteSeries *frame_ids = find_route_series(*route_data, "/roadEncodeIdx/frameId");
  if (segment_numbers == nullptr || decode_indices == nullptr) {
    return;
  }

  size_t count = std::min(segment_numbers->times.size(), segment_numbers->values.size());
  count = std::min(count, decode_indices->values.size());
  if (frame_ids != nullptr) {
    count = std::min(count, frame_ids->values.size());
  }
  route_data->road_camera.entries.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const int segment_number = static_cast<int>(std::llround(segment_numbers->values[i]));
    if (available_segments.find(segment_number) == available_segments.end()) {
      continue;
    }
    const int decode_index = static_cast<int>(std::llround(decode_indices->values[i]));
    const uint32_t frame_id = frame_ids != nullptr
      ? static_cast<uint32_t>(std::llround(frame_ids->values[i]))
      : static_cast<uint32_t>(std::max(0, decode_index));
    route_data->road_camera.entries.push_back(CameraFrameIndexEntry{
      .timestamp = segment_numbers->times[i],
      .segment = segment_number,
      .decode_index = decode_index,
      .frame_id = frame_id,
    });
  }

  std::sort(route_data->road_camera.entries.begin(), route_data->road_camera.entries.end(),
            [](const CameraFrameIndexEntry &a, const CameraFrameIndexEntry &b) {
              return a.timestamp < b.timestamp;
            });
}

size_t load_worker_budget() {
  size_t worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) {
    worker_count = 1;
  }
  if (const char *raw = std::getenv("JOTP_LOAD_WORKERS"); raw != nullptr && std::strlen(raw) > 0) {
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end != nullptr && *end == '\0' && parsed > 0) {
      worker_count = static_cast<size_t>(parsed);
    }
  }
  return std::max<size_t>(1, worker_count);
}

size_t segment_worker_count(size_t segment_count, size_t worker_budget) {
  return std::max<size_t>(1, std::min(worker_budget, segment_count));
}

size_t extract_chunk_count(size_t event_count, size_t worker_budget, size_t segment_workers) {
  if (event_count < 4096) {
    return 1;
  }
  const size_t per_segment_budget = std::max<size_t>(1, worker_budget / std::max<size_t>(1, segment_workers));
  const size_t chunk_target = std::max<size_t>(1, (event_count + 14999) / 15000);
  return std::max<size_t>(1, std::min({per_segment_budget, chunk_target, static_cast<size_t>(8)}));
}

SeriesAccumulator extract_segment_series(const std::vector<Event> &events,
                                         const SchemaIndex &schema,
                                         const dbc_core::Database *can_dbc,
                                         bool skip_raw_can,
                                         size_t worker_budget,
                                         size_t segment_workers) {
  const size_t chunk_count = extract_chunk_count(events.size(), worker_budget, segment_workers);
  if (chunk_count <= 1 || events.empty()) {
    SeriesAccumulator series = make_series_accumulator(schema);
    append_events_fast_range(events, 0, events.size(), schema, can_dbc, skip_raw_can, &series);
    return series;
  }

  const size_t events_per_chunk = (events.size() + chunk_count - 1) / chunk_count;
  std::vector<SeriesAccumulator> chunk_results;
  chunk_results.reserve(chunk_count);
  for (size_t i = 0; i < chunk_count; ++i) {
    chunk_results.push_back(make_series_accumulator(schema));
  }

  std::vector<std::thread> workers;
  workers.reserve(chunk_count > 0 ? chunk_count - 1 : 0);
  for (size_t chunk = 1; chunk < chunk_count; ++chunk) {
    workers.emplace_back([&, chunk]() {
      const size_t begin = chunk * events_per_chunk;
      const size_t end = std::min(events.size(), begin + events_per_chunk);
      append_events_fast_range(events, begin, end, schema, can_dbc, skip_raw_can, &chunk_results[chunk]);
    });
  }
  append_events_fast_range(events, 0, std::min(events.size(), events_per_chunk), schema, can_dbc, skip_raw_can, &chunk_results[0]);
  for (std::thread &worker : workers) {
    worker.join();
  }

  SeriesAccumulator merged = make_series_accumulator(schema);
  for (SeriesAccumulator &chunk : chunk_results) {
    merge_series_accumulator(&merged, &chunk);
  }
  return merged;
}

LoadedRouteArtifacts load_route_series_parallel(
    const std::map<int, SegmentLogs> &segments,
    const SchemaIndex &schema,
    const dbc_core::Database *can_dbc,
    bool skip_raw_can,
    LoadStats *stats) {
  struct SegmentResult {
    SeriesAccumulator series;
    std::vector<LogEntry> logs;
  };

  const std::vector<std::pair<int, SegmentLogs>> segment_list(segments.begin(), segments.end());
  std::vector<SegmentResult> results;
  results.reserve(segment_list.size());
  for (size_t i = 0; i < segment_list.size(); ++i) {
    results.emplace_back(SegmentResult{make_series_accumulator(schema)});
  }
  std::atomic<size_t> next_segment{0};
  std::mutex error_mutex;
  std::string first_error;
  const size_t worker_budget = static_cast<size_t>(stats->num_workers);
  const size_t segment_workers = segment_worker_count(segment_list.size(), worker_budget);

  auto worker = [&]() {
    while (true) {
      const size_t index = next_segment.fetch_add(1);
      if (index >= segment_list.size()) {
        return;
      }

      const auto &[segment_number, segment] = segment_list[index];
      const std::string &log_path = !segment.rlog.empty() ? segment.rlog : segment.qlog;
      LoadStats::SegmentStats &segment_stats = stats->segments[index];
      segment_stats.segment_number = segment_number;
      segment_stats.log_path = log_path;
      if (log_path.empty()) {
        segment_stats.failed = true;
        std::lock_guard<std::mutex> lock(error_mutex);
        if (first_error.empty()) {
          first_error = "Missing log path for segment " + std::to_string(segment_number);
        }
        stats->publish(RouteLoadStage::DownloadingSegment, index, std::to_string(segment_number));
        stats->publish(RouteLoadStage::ParsingSegment, index, std::to_string(segment_number));
        continue;
      }

      LogReader reader;
      if (!reader.load(log_path, nullptr, true)) {
        segment_stats.failed = true;
        std::lock_guard<std::mutex> lock(error_mutex);
        if (first_error.empty()) {
          first_error = "Failed to load log segment: " + log_path;
        }
        stats->publish(RouteLoadStage::DownloadingSegment, index, std::to_string(segment_number));
        stats->publish(RouteLoadStage::ParsingSegment, index, std::to_string(segment_number));
        continue;
      }

      segment_stats.download_seconds = reader.download_seconds();
      segment_stats.decompress_seconds = reader.decompress_seconds();
      segment_stats.parse_seconds = reader.parse_seconds();
      segment_stats.compressed_bytes = reader.compressed_size();
      segment_stats.decompressed_bytes = reader.decompressed_size();
      stats->bytes_downloaded.fetch_add(reader.compressed_size());
      stats->segments_downloaded.fetch_add(1);
      stats->publish(RouteLoadStage::DownloadingSegment, index, std::to_string(segment_number));

      const auto extract_start = LoadStats::Clock::now();
      results[index].series = extract_segment_series(reader.events, schema, can_dbc, skip_raw_can, worker_budget, segment_workers);
      results[index].logs = extract_segment_logs(reader.events);
      segment_stats.extract_seconds = std::chrono::duration<double>(LoadStats::Clock::now() - extract_start).count();
      segment_stats.event_count = reader.events.size();
      segment_stats.series_count = populated_series_count(results[index].series);
      stats->segments_parsed.fetch_add(1);
      stats->publish(RouteLoadStage::ParsingSegment, index, std::to_string(segment_number));
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(segment_workers);
  for (size_t i = 0; i < segment_workers; ++i) {
    workers.emplace_back(worker);
  }
  for (std::thread &thread : workers) {
    thread.join();
  }

  if (!first_error.empty()) {
    throw std::runtime_error(first_error);
  }

  stats->merge_start = LoadStats::Clock::now();
  SeriesAccumulator merged = make_series_accumulator(schema);
  for (size_t i = 0; i < results.size(); ++i) {
    merge_series_accumulator(&merged, &results[i].series);
  }
  std::vector<LogEntry> logs;
  for (SegmentResult &result : results) {
    if (!result.logs.empty()) {
      logs.insert(logs.end(),
                  std::make_move_iterator(result.logs.begin()),
                  std::make_move_iterator(result.logs.end()));
    }
  }
  LoadedRouteArtifacts artifacts;
  artifacts.series = collect_series(std::move(merged));
  artifacts.logs = std::move(logs);
  artifacts.enum_info = std::move(merged.enum_info);
  stats->merge_end = LoadStats::Clock::now();
  return artifacts;
}

std::vector<std::string> collect_layout_roots(const SketchLayout &layout) {
  std::vector<std::string> roots;
  for (const auto &tab : layout.tabs) {
    for (const auto &pane : tab.panes) {
      for (const auto &curve : pane.curves) {
        std::string root = "custom";
        if (is_absolute_curve(curve.name)) {
          const size_t slash = curve.name.find('/', 1);
          root = curve.name.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
        }
        if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
          roots.push_back(root);
        }
      }
    }
  }
  if (roots.empty()) {
    roots.push_back("layout");
  }
  return roots;
}

std::vector<std::string> collect_route_roots(const std::vector<std::string> &paths) {
  std::vector<std::string> roots;
  for (const std::string &path : paths) {
    if (!is_absolute_curve(path)) {
      continue;
    }
    const size_t slash = path.find('/', 1);
    const std::string root = path.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
    if (!root.empty() && std::find(roots.begin(), roots.end(), root) == roots.end()) {
      roots.push_back(root);
    }
  }
  std::sort(roots.begin(), roots.end());
  return roots;
}

}  // namespace

struct StreamAccumulator::Impl {
  const SchemaIndex &schema = SchemaIndex::instance();
  SeriesAccumulator series = make_series_accumulator(schema);
  std::vector<LogEntry> logs;
  std::string last_alert_key;
  std::string manual_dbc_name;
  std::string detected_dbc_name;
  std::string car_fingerprint;
  std::optional<dbc_core::Database> can_dbc;
  std::optional<double> time_offset;

  void refresh_dbc() {
    const std::string next_dbc = !manual_dbc_name.empty() ? manual_dbc_name : detect_dbc_for_fingerprint(car_fingerprint);
    if (next_dbc == detected_dbc_name) {
      return;
    }
    detected_dbc_name = next_dbc;
    can_dbc.reset();
    if (!detected_dbc_name.empty()) {
      can_dbc.emplace(resolve_dbc_path(detected_dbc_name));
    }
  }
};

StreamAccumulator::StreamAccumulator(const std::string &dbc_name, std::optional<double> time_offset)
  : impl_(std::make_unique<Impl>()) {
  impl_->manual_dbc_name = dbc_name;
  impl_->time_offset = time_offset;
  impl_->refresh_dbc();
}

StreamAccumulator::~StreamAccumulator() = default;

void StreamAccumulator::set_dbc_name(const std::string &dbc_name) {
  impl_->manual_dbc_name = dbc_name;
  impl_->refresh_dbc();
}

void StreamAccumulator::append_event(cereal::Event::Which which, kj::ArrayPtr<const capnp::word> data) {
  capnp::FlatArrayMessageReader event_reader(data);
  const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
  const double boot_time = static_cast<double>(event.getLogMonoTime()) / 1.0e9;
  if (!impl_->time_offset.has_value()) {
    impl_->time_offset = boot_time;
  }
  if (which == cereal::Event::Which::CAR_PARAMS) {
    const std::string fingerprint = event.getCarParams().getCarFingerprint().cStr();
    if (!fingerprint.empty() && fingerprint != impl_->car_fingerprint) {
      impl_->car_fingerprint = fingerprint;
      impl_->refresh_dbc();
    }
  }

  append_event_fast(which,
                    -1,
                    data,
                    impl_->schema,
                    impl_->can_dbc ? &*impl_->can_dbc : nullptr,
                    true,
                    *impl_->time_offset,
                    &impl_->series);
  append_log_event(which, event, *impl_->time_offset, &impl_->logs, &impl_->last_alert_key);
}

StreamExtractBatch StreamAccumulator::take_batch() {
  StreamExtractBatch batch;
  batch.car_fingerprint = impl_->car_fingerprint;
  batch.dbc_name = impl_->detected_dbc_name;
  if (impl_->time_offset.has_value()) {
    batch.has_time_offset = true;
    batch.time_offset = *impl_->time_offset;
  }
  if (impl_->logs.empty() && populated_series_count(impl_->series) == 0 && impl_->series.enum_info.empty()) {
    return batch;
  }

  SeriesAccumulator emitted = std::move(impl_->series);
  batch.enum_info = std::move(emitted.enum_info);
  batch.series = collect_series(std::move(emitted));
  batch.logs = std::move(impl_->logs);
  impl_->series = make_series_accumulator(impl_->schema);
  impl_->logs.clear();
  return batch;
}

const std::string &StreamAccumulator::car_fingerprint() const {
  return impl_->car_fingerprint;
}

const std::string &StreamAccumulator::dbc_name() const {
  return impl_->detected_dbc_name;
}

std::optional<double> StreamAccumulator::time_offset() const {
  return impl_->time_offset;
}

SketchLayout load_sketch_layout(const fs::path &layout_path) {
  SketchLayout layout = parse_layout(layout_path);
  layout.roots = collect_layout_roots(layout);
  return layout;
}

RouteData load_route_data(const std::string &route_name,
                          const std::string &data_dir,
                          const std::string &dbc_name,
                          const RouteLoadProgressCallback &progress) {
  if (route_name.empty()) {
    return RouteData{};
  }

  LoadStats stats(progress);
  stats.load_start = LoadStats::Clock::now();
  const auto segments = resolve_route_segments(route_name, data_dir);
  stats.resolve_end = LoadStats::Clock::now();
  stats.segment_count = segments.size();
  stats.total_segments.store(segments.size());
  stats.num_workers = static_cast<int>(load_worker_budget());
  stats.segments.resize(segments.size());
  stats.publish(RouteLoadStage::Resolving, 0, {});

  const RouteMetadata metadata = detect_route_metadata(segments);
  const std::string resolved_dbc = !dbc_name.empty() ? dbc_name : detect_dbc_for_fingerprint(metadata.car_fingerprint);
  const std::optional<dbc_core::Database> can_dbc = resolved_dbc.empty()
    ? std::nullopt
    : std::optional<dbc_core::Database>(std::in_place, resolve_dbc_path(resolved_dbc));

  const SchemaIndex &schema = SchemaIndex::instance();
  LoadedRouteArtifacts artifacts = load_route_series_parallel(segments, schema, can_dbc ? &*can_dbc : nullptr, !resolved_dbc.empty(), &stats);
  RouteData route_data = build_route_data(std::move(artifacts.series),
                                          std::move(artifacts.logs),
                                          std::move(artifacts.enum_info),
                                          metadata.car_fingerprint,
                                          resolved_dbc);
  build_road_camera_index(segments, &route_data);
  stats.load_end = LoadStats::Clock::now();
  stats.publish(RouteLoadStage::Finished, segments.size(), {});
  stats.print_summary(route_data.series.size());
  return route_data;
}

std::vector<std::string> available_dbc_names() {
  static const std::vector<std::string> names = available_dbc_names_impl();
  return names;
}

std::vector<std::string> collect_route_roots_for_paths(const std::vector<std::string> &paths) {
  return collect_route_roots(paths);
}

}  // namespace jotpluggler
