#include "tools/jotpluggler/sketch_layout.h"

#include <capnp/dynamic.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

std::string curve_leaf(std::string_view series_name) {
  if (series_name.empty()) {
    return "plot";
  }
  size_t last = series_name.find_last_of('/');
  if (last == std::string_view::npos) {
    return std::string(series_name);
  }
  return std::string(series_name.substr(last + 1));
}

std::string curve_label(std::string_view series_name) {
  if (series_name.empty() || series_name.front() != '/') {
    return std::string(series_name);
  }

  std::vector<std::string_view> parts;
  size_t start = 1;
  while (start < series_name.size()) {
    size_t end = series_name.find('/', start);
    parts.push_back(series_name.substr(start, end == std::string_view::npos ? series_name.size() - start : end - start));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  if (parts.size() >= 2) {
    const std::string_view parent = parts[parts.size() - 2];
    if (parent.find("canState") == 0 || std::all_of(parent.begin(), parent.end(), ::isdigit)) {
      return std::string(parent) + "/" + std::string(parts.back());
    }
  }
  return curve_leaf(series_name);
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

  workspace_node.orientation = attr(node, "orientation", "-") == "-"
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

std::optional<double> dynamic_value_to_double(const capnp::DynamicValue::Reader &value) {
  switch (value.getType()) {
    case capnp::DynamicValue::Type::BOOL:
      return value.as<bool>() ? 1.0 : 0.0;
    case capnp::DynamicValue::Type::INT:
      return static_cast<double>(value.as<int64_t>());
    case capnp::DynamicValue::Type::UINT:
      return static_cast<double>(value.as<uint64_t>());
    case capnp::DynamicValue::Type::FLOAT:
      return value.as<double>();
    case capnp::DynamicValue::Type::ENUM:
      return static_cast<double>(value.as<capnp::DynamicEnum>().getRaw());
    default:
      return std::nullopt;
  }
}

bool is_absolute_curve(const std::string &name) {
  return !name.empty() && name.front() == '/';
}

void append_scalar_point(const std::string &path,
                         double tm,
                         double value,
                         std::unordered_map<std::string, RouteSeries> *series_by_path) {
  RouteSeries &series = (*series_by_path)[path];
  if (series.path.empty()) {
    series.path = path;
  }
  series.times.push_back(tm);
  series.values.push_back(value);
}

void append_dynamic_series(const std::string &path,
                           const capnp::DynamicValue::Reader &value,
                           double tm,
                           std::unordered_map<std::string, RouteSeries> *series_by_path) {
  if (std::optional<double> scalar = dynamic_value_to_double(value); scalar.has_value()) {
    append_scalar_point(path, tm, *scalar, series_by_path);
    return;
  }

  switch (value.getType()) {
    case capnp::DynamicValue::Type::STRUCT: {
      const capnp::DynamicStruct::Reader reader = value.as<capnp::DynamicStruct>();
      for (auto field : reader.getSchema().getFields()) {
        if (!reader.has(field)) {
          continue;
        }
        const std::string child_path = path + "/" + field.getProto().getName().cStr();
        append_dynamic_series(child_path, reader.get(field), tm, series_by_path);
      }
      return;
    }
    case capnp::DynamicValue::Type::LIST: {
      const capnp::DynamicList::Reader list = value.as<capnp::DynamicList>();
      if (list.size() == 0) {
        return;
      }
      const capnp::DynamicValue::Reader first = list[0];
      if ((first.getType() == capnp::DynamicValue::Type::INT
           || first.getType() == capnp::DynamicValue::Type::UINT
           || first.getType() == capnp::DynamicValue::Type::FLOAT
           || first.getType() == capnp::DynamicValue::Type::BOOL
           || first.getType() == capnp::DynamicValue::Type::ENUM) && list.size() > 16) {
        return;
      }
      if (first.getType() == capnp::DynamicValue::Type::TEXT
          || first.getType() == capnp::DynamicValue::Type::DATA
          || first.getType() == capnp::DynamicValue::Type::CAPABILITY
          || first.getType() == capnp::DynamicValue::Type::UNKNOWN) {
        return;
      }
      for (uint i = 0; i < list.size(); ++i) {
        append_dynamic_series(path + "/" + std::to_string(i), list[i], tm, series_by_path);
      }
      return;
    }
    default:
      return;
  }
}

void append_events_to_series(const std::vector<Event> &events,
                             std::unordered_map<std::string, RouteSeries> *series_by_path) {
  for (const Event &event_record : events) {
    capnp::FlatArrayMessageReader event_reader(event_record.data);
    const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
    const capnp::DynamicStruct::Reader dynamic_event(event);
    auto maybe_active_field = dynamic_event.which();
    KJ_IF_MAYBE(active_field, maybe_active_field) {
      const std::string service((*active_field).getProto().getName().cStr());
      const capnp::DynamicValue::Reader payload = dynamic_event.get(*active_field);
      const double tm = static_cast<double>(event.getLogMonoTime()) / 1.0e9;
      append_dynamic_series("/" + service, payload, tm, series_by_path);
    }
  }
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
  RouteData route_data;
  if (route_name.empty()) {
    return route_data;
  }

  const auto segments = resolve_route_segments(route_name, data_dir);
  if (progress) {
    RouteLoadProgress update;
    update.stage = RouteLoadStage::Resolving;
    update.segment_count = segments.size();
    progress(update);
  }

  std::unordered_map<std::string, RouteSeries> series_by_path;
  size_t segment_index = 0;
  for (const auto &[segment_number, segment] : segments) {
    const std::string &log_path = !segment.rlog.empty() ? segment.rlog : segment.qlog;
    if (log_path.empty()) {
      ++segment_index;
      continue;
    }

    LogReader reader;
    const std::string segment_name = std::to_string(segment_number);
    auto reader_progress = [&](LogReader::ProgressStage stage, uint64_t current, uint64_t total) {
      if (!progress) {
        return;
      }
      RouteLoadProgress update;
      update.stage = stage == LogReader::ProgressStage::Downloading
        ? RouteLoadStage::DownloadingSegment
        : RouteLoadStage::ParsingSegment;
      update.segment_index = segment_index;
      update.segment_count = segments.size();
      update.current = current;
      update.total = total;
      update.segment_name = segment_name;
      progress(update);
    };
    if (!reader.load(log_path, nullptr, true, reader_progress)) {
      throw std::runtime_error("Failed to load log segment: " + log_path);
    }
    append_events_to_series(reader.events, &series_by_path);
    ++segment_index;
  }

  if (progress) {
    RouteLoadProgress update;
    update.stage = RouteLoadStage::Finished;
    update.segment_index = segments.size();
    update.segment_count = segments.size();
    update.current = 1;
    update.total = 1;
    progress(update);
  }

  route_data.series.reserve(series_by_path.size());
  route_data.paths.reserve(series_by_path.size());
  for (auto &[path, series] : series_by_path) {
    if (series.times.empty()) {
      continue;
    }
    route_data.has_time_range = true;
    route_data.x_min = route_data.series.empty() ? series.times.front() : std::min(route_data.x_min, series.times.front());
    route_data.x_max = route_data.series.empty() ? series.times.back() : std::max(route_data.x_max, series.times.back());
    route_data.paths.push_back(path);
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

}  // namespace jotpluggler
