#include "tools/jotpluggler/sketch_layout.h"

#include <capnp/dynamic.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

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

bool is_element(xmlNodePtr node, const char *name) {
  return node != nullptr && node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name);
}

xmlNodePtr first_child(xmlNodePtr parent, const char *name) {
  if (parent == nullptr) {
    return nullptr;
  }
  for (xmlNodePtr child = parent->children; child != nullptr; child = child->next) {
    if (is_element(child, name)) {
      return child;
    }
  }
  return nullptr;
}

xmlNodePtr first_descendant(xmlNodePtr node, const char *name) {
  for (xmlNodePtr cur = node; cur != nullptr; cur = cur->next) {
    if (is_element(cur, name)) {
      return cur;
    }
    if (xmlNodePtr found = first_descendant(cur->children, name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

std::vector<xmlNodePtr> split_children(xmlNodePtr node) {
  std::vector<xmlNodePtr> out;
  if (node == nullptr) {
    return out;
  }
  for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
    if (is_element(child, "DockSplitter") || is_element(child, "DockArea")) {
      out.push_back(child);
    }
  }
  return out;
}

std::string attr(xmlNodePtr node, const char *name, std::string default_value = "") {
  if (node == nullptr) {
    return default_value;
  }
  xmlChar *raw = xmlGetProp(node, BAD_CAST name);
  if (raw == nullptr) {
    return default_value;
  }
  std::string out(reinterpret_cast<const char *>(raw));
  xmlFree(raw);
  return out;
}

double attr_double(xmlNodePtr node, const char *name, double default_value) {
  const std::string value = attr(node, name);
  if (value.empty()) {
    return default_value;
  }
  char *end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  return end != nullptr && *end == '\0' ? parsed : default_value;
}

int attr_int(xmlNodePtr node, const char *name, int default_value) {
  const std::string value = attr(node, name);
  if (value.empty()) {
    return default_value;
  }
  char *end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  return end != nullptr && *end == '\0' ? static_cast<int>(parsed) : default_value;
}

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

std::vector<double> normalize_sizes(std::string_view sizes_text, size_t child_count) {
  std::vector<double> parsed;
  size_t start = 0;
  while (start <= sizes_text.size()) {
    size_t end = sizes_text.find(';', start);
    const std::string part(sizes_text.substr(start, end == std::string_view::npos ? sizes_text.size() - start : end - start));
    if (!part.empty()) {
      char *parse_end = nullptr;
      const double value = std::strtod(part.c_str(), &parse_end);
      parsed.push_back(parse_end != nullptr && *parse_end == '\0' ? std::max(value, 0.0) : 0.0);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
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

PlotRange parse_range(xmlNodePtr plot_node) {
  PlotRange range;
  if (xmlNodePtr range_node = first_child(plot_node, "range"); range_node != nullptr) {
    range.valid = true;
    range.left = attr_double(range_node, "left", 0.0);
    range.right = attr_double(range_node, "right", 0.0);
    range.bottom = attr_double(range_node, "bottom", 0.0);
    range.top = attr_double(range_node, "top", 1.0);
  }
  if (xmlNodePtr limit_y_node = first_child(plot_node, "limitY"); limit_y_node != nullptr) {
    if (!attr(limit_y_node, "min").empty()) {
      range.has_y_limit_min = true;
      range.y_limit_min = attr_double(limit_y_node, "min", 0.0);
    }
    if (!attr(limit_y_node, "max").empty()) {
      range.has_y_limit_max = true;
      range.y_limit_max = attr_double(limit_y_node, "max", 1.0);
    }
  }
  return range;
}

Curve parse_curve(xmlNodePtr curve_node) {
  Curve curve;
  curve.name = attr(curve_node, "name");
  curve.label = curve_label(curve.name);
  curve.color = parse_color(attr(curve_node, "color"));

  for (xmlNodePtr child = curve_node->children; child != nullptr; child = child->next) {
    if (!is_element(child, "transform")) {
      continue;
    }

    const std::string transform_name = attr(child, "name");
    if (transform_name == "Derivative") {
      curve.derivative = true;
    } else if (transform_name == "Scale/Offset") {
      xmlNodePtr options_node = first_child(child, "options");
      curve.value_scale = attr_double(options_node, "value_scale", 1.0);
      curve.value_offset = attr_double(options_node, "value_offset", 0.0);
    }
  }
  return curve;
}

std::string pane_title(xmlNodePtr dock_area_node, const std::vector<Curve> &curves) {
  (void)curves;
  const std::string raw = attr(dock_area_node, "name");
  return raw.empty() ? "..." : raw;
}

Pane parse_dock_area(xmlNodePtr dock_area_node) {
  Pane pane;
  xmlNodePtr plot_node = first_child(dock_area_node, "plot");
  pane.range = parse_range(plot_node);
  if (plot_node != nullptr) {
    for (xmlNodePtr child = plot_node->children; child != nullptr; child = child->next) {
      if (is_element(child, "curve")) {
        pane.curves.push_back(parse_curve(child));
      }
    }
  }
  pane.title = pane_title(dock_area_node, pane.curves);
  return pane;
}

WorkspaceNode parse_workspace_node(xmlNodePtr node, WorkspaceTab *tab) {
  WorkspaceNode workspace_node;
  if (node == nullptr) {
    return workspace_node;
  }

  if (is_element(node, "DockArea")) {
    workspace_node.is_pane = true;
    workspace_node.pane_index = static_cast<int>(tab->panes.size());
    tab->panes.push_back(parse_dock_area(node));
    return workspace_node;
  }

  if (!is_element(node, "DockSplitter")) {
    for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
      if (is_element(child, "DockSplitter") || is_element(child, "DockArea")) {
        return parse_workspace_node(child, tab);
      }
    }
    return workspace_node;
  }

  const std::vector<xmlNodePtr> children = split_children(node);
  if (children.empty()) {
    return workspace_node;
  }

  workspace_node.orientation = attr(node, "orientation", "|") == "|"
    ? SplitOrientation::Horizontal
    : SplitOrientation::Vertical;
  const std::vector<double> sizes = normalize_sizes(attr(node, "sizes"), children.size());
  workspace_node.sizes.reserve(sizes.size());
  workspace_node.children.reserve(children.size());
  for (size_t i = 0; i < children.size(); ++i) {
    workspace_node.sizes.push_back(static_cast<float>(sizes[i]));
    workspace_node.children.push_back(parse_workspace_node(children[i], tab));
  }
  return workspace_node;
}

WorkspaceTab parse_tab(xmlNodePtr tab, const fs::path &layout_path) {
  WorkspaceTab workspace_tab;
  workspace_tab.tab_name = attr(tab, "tab_name", "tab1");

  xmlNodePtr container = first_child(tab, "Container");
  if (container == nullptr) {
    throw std::runtime_error("Layout tab has no Container: " + layout_path.string());
  }

  xmlNodePtr dock_root = nullptr;
  for (xmlNodePtr child = container->children; child != nullptr; child = child->next) {
    if (is_element(child, "DockSplitter") || is_element(child, "DockArea")) {
      dock_root = child;
      break;
    }
  }
  if (dock_root == nullptr) {
    throw std::runtime_error("Layout tab has no dock content: " + layout_path.string());
  }

  workspace_tab.root = parse_workspace_node(dock_root, &workspace_tab);
  return workspace_tab;
}

SketchLayout parse_layout(const fs::path &layout_path) {
  xmlDocPtr raw_doc = xmlReadFile(layout_path.c_str(), nullptr, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if (raw_doc == nullptr) {
    throw std::runtime_error("Failed to parse layout XML: " + layout_path.string());
  }

  std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(raw_doc, xmlFreeDoc);
  xmlNodePtr root = xmlDocGetRootElement(doc.get());
  if (root == nullptr) {
    throw std::runtime_error("Layout XML has no root node: " + layout_path.string());
  }

  xmlNodePtr tabbed_widget = first_descendant(root, "tabbed_widget");
  if (tabbed_widget == nullptr) {
    throw std::runtime_error("Layout has no tab widget: " + layout_path.string());
  }

  SketchLayout layout;
  for (xmlNodePtr tab = tabbed_widget->children; tab != nullptr; tab = tab->next) {
    if (!is_element(tab, "Tab")) {
      continue;
    }
    layout.tabs.push_back(parse_tab(tab, layout_path));
  }
  if (layout.tabs.empty()) {
    throw std::runtime_error("Layout has no tabs: " + layout_path.string());
  }
  layout.current_tab_index = std::clamp(attr_int(first_child(tabbed_widget, "currentTabIndex"), "index", 0),
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
                         double tm,
                         double value,
                         SeriesAccumulator *series) {
  if (path_override == nullptr && node.fixed_slot >= 0) {
    append_fixed_scalar_point(&series->fixed_series[static_cast<size_t>(node.fixed_slot)], tm, value);
    return;
  }

  const std::string &path = path_override != nullptr ? *path_override : node.path;
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
        append_scalar_value(node, path_override, tm, *scalar, series);
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

void append_events_fast_range(const std::vector<Event> &events,
                              size_t begin,
                              size_t end,
                              const SchemaIndex &schema,
                              SeriesAccumulator *series) {
  for (size_t i = begin; i < end; ++i) {
    const Event &event_record = events[i];
    const uint16_t which = static_cast<uint16_t>(event_record.which);
    if (which >= schema.by_which.size() || !schema.by_which[which].has_value()) {
      continue;
    }
    const ResolvedService &service = *schema.by_which[which];

    capnp::FlatArrayMessageReader event_reader(event_record.data);
    const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
    const capnp::DynamicStruct::Reader dynamic_event(event);
    const double tm = static_cast<double>(event.getLogMonoTime()) / 1.0e9;
    append_fast_node(service.payload, dynamic_event.get(service.union_field), tm, series);
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

RouteData build_route_data(std::vector<RouteSeries> &&series_list) {
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

  if (route_data.has_time_range) {
    const double time_offset = route_data.x_min;
    for (RouteSeries &series : route_data.series) {
      for (double &tm : series.times) {
        tm -= time_offset;
      }
    }
    route_data.x_max -= time_offset;
    route_data.x_min = 0.0;
  }

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
                                         size_t worker_budget,
                                         size_t segment_workers) {
  const size_t chunk_count = extract_chunk_count(events.size(), worker_budget, segment_workers);
  if (chunk_count <= 1 || events.empty()) {
    SeriesAccumulator series = make_series_accumulator(schema);
    append_events_fast_range(events, 0, events.size(), schema, &series);
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
      append_events_fast_range(events, begin, end, schema, &chunk_results[chunk]);
    });
  }
  append_events_fast_range(events, 0, std::min(events.size(), events_per_chunk), schema, &chunk_results[0]);
  for (std::thread &worker : workers) {
    worker.join();
  }

  SeriesAccumulator merged = make_series_accumulator(schema);
  for (SeriesAccumulator &chunk : chunk_results) {
    merge_series_accumulator(&merged, &chunk);
  }
  return merged;
}

std::vector<RouteSeries> load_route_series_parallel(
    const std::map<int, SegmentLogs> &segments,
    const SchemaIndex &schema,
    LoadStats *stats) {
  struct SegmentResult {
    SeriesAccumulator series;
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
      results[index].series = extract_segment_series(reader.events, schema, worker_budget, segment_workers);
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
  std::vector<RouteSeries> all_series = collect_series(std::move(merged));
  stats->merge_end = LoadStats::Clock::now();
  return all_series;
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

SketchLayout load_sketch_layout(const fs::path &layout_path) {
  SketchLayout layout = parse_layout(layout_path);
  layout.roots = collect_layout_roots(layout);
  return layout;
}

RouteData load_route_data(const std::string &route_name,
                          const std::string &data_dir,
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

  const SchemaIndex &schema = SchemaIndex::instance();
  std::vector<RouteSeries> series = load_route_series_parallel(segments, schema, &stats);
  RouteData route_data = build_route_data(std::move(series));
  build_road_camera_index(segments, &route_data);
  stats.load_end = LoadStats::Clock::now();
  stats.publish(RouteLoadStage::Finished, segments.size(), {});
  stats.print_summary(route_data.series.size());
  return route_data;
}

}  // namespace jotpluggler
