#include "tools/jotpluggler/jotpluggler.h"
#include "tools/jotpluggler/app_map.h"

#include <GLFW/glfw3.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
}

#include <array>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "third_party/json11/json11.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int MAP_MIN_ZOOM = 1;
constexpr int MAP_MAX_ZOOM = 18;
constexpr int MAP_SINGLE_POINT_MIN_ZOOM = 14;
constexpr double MAP_TRACE_PAD_FRAC = 0.45;
constexpr double MAP_TRACE_MIN_LAT_PAD = 0.01;
constexpr double MAP_BOUNDS_GRID = 0.005;
constexpr const char *MAP_QUERY_ENDPOINTS[] = {
  "https://overpass-api.de/api/interpreter",
  "https://overpass.private.coffee/api/interpreter",
};
struct GeoPoint {
  double lat = 0.0;
  double lon = 0.0;
};

struct GeoBounds {
  double south = 0.0;
  double west = 0.0;
  double north = 0.0;
  double east = 0.0;

  bool valid() const {
    return south < north && west < east;
  }
};

struct FeatureBounds {
  double south = 0.0;
  double west = 0.0;
  double north = 0.0;
  double east = 0.0;
};

enum class RoadClass : uint8_t {
  Motorway,
  Primary,
  Secondary,
  Local,
};

struct RoadFeature {
  RoadClass road_class = RoadClass::Local;
  FeatureBounds bounds;
  std::vector<GeoPoint> points;
};

struct WaterLineFeature {
  FeatureBounds bounds;
  std::vector<GeoPoint> points;
};

struct WaterPolygonFeature {
  FeatureBounds bounds;
  std::vector<GeoPoint> ring;
};

}  // namespace

struct RouteBasemap {
  std::string key;
  GeoBounds bounds;
  std::vector<RoadFeature> roads;
  std::vector<WaterLineFeature> water_lines;
  std::vector<WaterPolygonFeature> water_polygons;
};

namespace {

double lon_to_world_x(double lon, int z) {
  return (lon + 180.0) / 360.0 * static_cast<double>(1 << z) * 256.0;
}

double lat_to_world_y(double lat, int z) {
  const double lat_rad = lat * M_PI / 180.0;
  return (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI)
       / 2.0 * static_cast<double>(1 << z) * 256.0;
}

double world_x_to_lon(double x, int z) {
  return x / (static_cast<double>(1 << z) * 256.0) * 360.0 - 180.0;
}

double world_y_to_lat(double y, int z) {
  const double n = M_PI - (2.0 * M_PI * y) / (static_cast<double>(1 << z) * 256.0);
  return 180.0 / M_PI * std::atan(std::sinh(n));
}

double map_trace_center_lat(const GpsTrace &trace) {
  return (trace.min_lat + trace.max_lat) * 0.5;
}

double map_trace_center_lon(const GpsTrace &trace) {
  return (trace.min_lon + trace.max_lon) * 0.5;
}

double clamp_lat(double lat) {
  return std::clamp(lat, -85.0, 85.0);
}

double clamp_lon(double lon) {
  return std::clamp(lon, -179.999, 179.999);
}

double cos_lat_scale(double lat) {
  return std::max(0.2, std::cos(lat * M_PI / 180.0));
}

double quantize_down(double value, double step) {
  return std::floor(value / step) * step;
}

double quantize_up(double value, double step) {
  return std::ceil(value / step) * step;
}

FeatureBounds compute_feature_bounds(const std::vector<GeoPoint> &points) {
  FeatureBounds bounds;
  if (points.empty()) {
    return bounds;
  }
  bounds.south = bounds.north = points.front().lat;
  bounds.west = bounds.east = points.front().lon;
  for (const GeoPoint &point : points) {
    bounds.south = std::min(bounds.south, point.lat);
    bounds.north = std::max(bounds.north, point.lat);
    bounds.west = std::min(bounds.west, point.lon);
    bounds.east = std::max(bounds.east, point.lon);
  }
  return bounds;
}

bool bounds_contains_bounds(const GeoBounds &outer, const GeoBounds &inner) {
  return outer.valid() && inner.valid()
      && outer.south <= inner.south
      && outer.north >= inner.north
      && outer.west <= inner.west
      && outer.east >= inner.east;
}

bool feature_intersects_view(const FeatureBounds &feature, const GeoBounds &view) {
  return !(feature.east < view.west || feature.west > view.east
        || feature.north < view.south || feature.south > view.north);
}

GeoBounds requested_bounds_for_trace(const GpsTrace &trace) {
  if (trace.points.empty()) {
    return {};
  }
  const double center_lat = map_trace_center_lat(trace);
  const double lat_span = std::max(trace.max_lat - trace.min_lat, 0.002);
  const double lon_span = std::max(trace.max_lon - trace.min_lon, 0.002 / cos_lat_scale(center_lat));
  const double lat_pad = std::max(lat_span * MAP_TRACE_PAD_FRAC, MAP_TRACE_MIN_LAT_PAD);
  const double lon_pad = std::max(lon_span * MAP_TRACE_PAD_FRAC, MAP_TRACE_MIN_LAT_PAD / cos_lat_scale(center_lat));

  GeoBounds bounds;
  bounds.south = clamp_lat(quantize_down(trace.min_lat - lat_pad, MAP_BOUNDS_GRID));
  bounds.north = clamp_lat(quantize_up(trace.max_lat + lat_pad, MAP_BOUNDS_GRID));
  bounds.west = clamp_lon(quantize_down(trace.min_lon - lon_pad, MAP_BOUNDS_GRID));
  bounds.east = clamp_lon(quantize_up(trace.max_lon + lon_pad, MAP_BOUNDS_GRID));
  return bounds;
}

GeoBounds view_bounds(double top_left_x, double top_left_y, float width, float height, int zoom) {
  const double west = world_x_to_lon(top_left_x, zoom);
  const double east = world_x_to_lon(top_left_x + width, zoom);
  const double north = world_y_to_lat(top_left_y, zoom);
  const double south = world_y_to_lat(top_left_y + height, zoom);
  return GeoBounds{
    .south = std::min(south, north),
    .west = std::min(west, east),
    .north = std::max(south, north),
    .east = std::max(west, east),
  };
}

int fit_map_zoom_for_bounds(const GeoBounds &bounds, float width, float height) {
  if (!bounds.valid()) {
    return MAP_MIN_ZOOM;
  }
  for (int z = MAP_MAX_ZOOM; z >= MAP_MIN_ZOOM; --z) {
    const double pixel_width = std::abs(lon_to_world_x(bounds.east, z) - lon_to_world_x(bounds.west, z));
    const double pixel_height = std::abs(lat_to_world_y(bounds.south, z) - lat_to_world_y(bounds.north, z));
    if (pixel_width <= width * 0.84 && pixel_height <= height * 0.84) {
      return z;
    }
  }
  return MAP_MIN_ZOOM;
}

int fit_map_zoom_for_trace(const GpsTrace &trace, float width, float height) {
  return fit_map_zoom_for_bounds(requested_bounds_for_trace(trace), width, height);
}

int minimum_allowed_map_zoom(const GeoBounds &bounds, const GpsTrace &trace, ImVec2 size) {
  if (trace.points.size() <= 1) {
    return MAP_SINGLE_POINT_MIN_ZOOM;
  }
  const int fit_zoom = fit_map_zoom_for_bounds(bounds.valid() ? bounds : requested_bounds_for_trace(trace), size.x, size.y);
  return std::clamp(fit_zoom - 1, MAP_MIN_ZOOM, MAP_MAX_ZOOM);
}

std::optional<GpsPoint> interpolate_gps(const GpsTrace &trace, double time_value) {
  if (trace.points.empty()) {
    return std::nullopt;
  }
  if (time_value <= trace.points.front().time) {
    return trace.points.front();
  }
  if (time_value >= trace.points.back().time) {
    return trace.points.back();
  }
  auto upper = std::lower_bound(trace.points.begin(), trace.points.end(), time_value,
                                [](const GpsPoint &point, double target) {
                                  return point.time < target;
                                });
  if (upper == trace.points.begin()) {
    return trace.points.front();
  }
  const GpsPoint &p1 = *upper;
  const GpsPoint &p0 = *(upper - 1);
  const double dt = p1.time - p0.time;
  if (dt <= 1.0e-9) {
    return p0;
  }
  const double alpha = (time_value - p0.time) / dt;
  GpsPoint out;
  out.time = time_value;
  out.lat = p0.lat + (p1.lat - p0.lat) * alpha;
  out.lon = p0.lon + (p1.lon - p0.lon) * alpha;
  out.bearing = static_cast<float>(p0.bearing + (p1.bearing - p0.bearing) * alpha);
  out.type = alpha < 0.5 ? p0.type : p1.type;
  return out;
}

ImU32 map_timeline_color(TimelineEntry::Type type, float alpha = 1.0f) {
  switch (type) {
    case TimelineEntry::Type::Engaged:
      return ImGui::GetColorU32(color_rgb(0, 163, 108, alpha));
    case TimelineEntry::Type::AlertInfo:
      return ImGui::GetColorU32(color_rgb(255, 195, 0, alpha));
    case TimelineEntry::Type::AlertWarning:
    case TimelineEntry::Type::AlertCritical:
      return ImGui::GetColorU32(color_rgb(199, 0, 57, alpha));
    case TimelineEntry::Type::None:
    default:
      return ImGui::GetColorU32(color_rgb(140, 150, 165, alpha));
  }
}

ImVec2 gps_to_screen(double lat, double lon, int zoom, double top_left_x, double top_left_y, const ImVec2 &rect_min) {
  return ImVec2(rect_min.x + static_cast<float>(lon_to_world_x(lon, zoom) - top_left_x),
                rect_min.y + static_cast<float>(lat_to_world_y(lat, zoom) - top_left_y));
}

bool point_in_rect_with_margin(const ImVec2 &point, const ImVec2 &rect_min, const ImVec2 &rect_max,
                               float margin_fraction) {
  const float width = rect_max.x - rect_min.x;
  const float height = rect_max.y - rect_min.y;
  const float margin_x = width * margin_fraction;
  const float margin_y = height * margin_fraction;
  return point.x >= rect_min.x + margin_x && point.x <= rect_max.x - margin_x
      && point.y >= rect_min.y + margin_y && point.y <= rect_max.y - margin_y;
}

void draw_car_marker(ImDrawList *draw_list, ImVec2 center, float bearing_deg, ImU32 color, float size) {
  const float rad = bearing_deg * static_cast<float>(M_PI / 180.0);
  const ImVec2 forward(std::sin(rad), -std::cos(rad));
  const ImVec2 perp(-forward.y, forward.x);
  const ImVec2 tip(center.x + forward.x * size, center.y + forward.y * size);
  const ImVec2 base(center.x - forward.x * size * 0.45f, center.y - forward.y * size * 0.45f);
  const ImVec2 left(base.x + perp.x * size * 0.6f, base.y + perp.y * size * 0.6f);
  const ImVec2 right(base.x - perp.x * size * 0.6f, base.y - perp.y * size * 0.6f);
  draw_list->AddTriangleFilled(tip, left, right, color);
  draw_list->AddTriangle(tip, left, right, IM_COL32(255, 255, 255, 210), 2.0f);
}

bool is_convex_ring(const std::vector<ImVec2> &points) {
  if (points.size() < 4) {
    return false;
  }
  float sign = 0.0f;
  const size_t n = points.size();
  for (size_t i = 0; i < n; ++i) {
    const ImVec2 &a = points[i];
    const ImVec2 &b = points[(i + 1) % n];
    const ImVec2 &c = points[(i + 2) % n];
    const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    if (std::abs(cross) < 1.0e-3f) {
      continue;
    }
    if (sign == 0.0f) {
      sign = cross;
    } else if ((cross > 0.0f) != (sign > 0.0f)) {
      return false;
    }
  }
  return sign != 0.0f;
}

uint64_t fnv1a64(std::string_view text) {
  uint64_t value = 1469598103934665603ULL;
  for (unsigned char c : text) {
    value ^= static_cast<uint64_t>(c);
    value *= 1099511628211ULL;
  }
  return value;
}

fs::path basemap_cache_root() {
  const char *home = std::getenv("HOME");
  fs::path root = home != nullptr ? fs::path(home) / ".comma" : fs::temp_directory_path();
  root /= "jotpluggler_vector_map";
  fs::create_directories(root);
  return root;
}

std::string bounds_key(const GeoBounds &bounds) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "v1_%.5f_%.5f_%.5f_%.5f",
                bounds.south, bounds.west, bounds.north, bounds.east);
  return buf;
}

fs::path basemap_cache_path(const std::string &key) {
  const uint64_t hash = fnv1a64(key);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%016llx.json", static_cast<unsigned long long>(hash));
  return basemap_cache_root() / buf;
}

std::string read_binary_file(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_binary_file(const fs::path &path, const std::string &contents) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (out) {
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }
}

std::string percent_encode(std::string_view text) {
  std::string out;
  out.reserve(text.size() * 3);
  for (unsigned char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[4];
      std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned int>(c));
      out += buf;
    }
  }
  return out;
}

std::string overpass_query(const GeoBounds &bounds) {
  char bbox[128];
  std::snprintf(bbox, sizeof(bbox), "%.6f,%.6f,%.6f,%.6f",
                bounds.south, bounds.west, bounds.north, bounds.east);
  return std::string("[out:json][timeout:25];(")
       + "way[\"highway\"][\"area\"!=\"yes\"](" + bbox + ");"
       + "way[\"natural\"=\"water\"](" + bbox + ");"
       + "way[\"waterway\"=\"riverbank\"](" + bbox + ");"
       + "way[\"waterway\"~\"river|stream|canal\"](" + bbox + ");"
       + ");out tags geom;";
}

bool fetch_overpass_json(const GeoBounds &bounds, std::string *out) {
  static std::once_flag network_once;
  std::call_once(network_once, []() {
    avformat_network_init();
  });

  const std::string body = std::string("data=") + percent_encode(overpass_query(bounds));
  const std::string headers = "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
                              "User-Agent: jotpluggler-vector-map/1.0\r\n";
  for (const char *endpoint : MAP_QUERY_ENDPOINTS) {
    std::string response;
    AVDictionary *options = nullptr;
    av_dict_set(&options, "method", "POST", 0);
    av_dict_set(&options, "headers", headers.c_str(), 0);
    av_dict_set(&options, "post_data", body.c_str(), 0);
    av_dict_set(&options, "send_expect_100", "0", 0);
    av_dict_set(&options, "timeout", "15000000", 0);
    AVIOContext *io = nullptr;
    const int open_err = avio_open2(&io, endpoint, AVIO_FLAG_READ, nullptr, &options);
    av_dict_free(&options);
    if (open_err < 0 || io == nullptr) {
      if (io != nullptr) {
        avio_close(io);
      }
      continue;
    }

    std::array<uint8_t, 16384> buffer = {};
    while (true) {
      const int n = avio_read(io, buffer.data(), static_cast<int>(buffer.size()));
      if (n > 0) {
        response.append(reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(n));
        continue;
      }
      if (n == AVERROR_EOF || n == 0) {
        break;
      }
      response.clear();
      break;
    }
    avio_close(io);

    if (!response.empty() && response.front() == '{') {
      *out = std::move(response);
      return true;
    }
  }
  return false;
}

std::string load_overpass_json(const GeoBounds &bounds, const std::string &key) {
  const fs::path cache_path = basemap_cache_path(key);
  if (fs::exists(cache_path)) {
    return read_binary_file(cache_path);
  }
  std::string response;
  if (!fetch_overpass_json(bounds, &response)) {
    return {};
  }
  write_binary_file(cache_path, response);
  return response;
}

std::vector<GeoPoint> geometry_points(const json11::Json &geometry_json) {
  std::vector<GeoPoint> points;
  const auto items = geometry_json.array_items();
  points.reserve(items.size());
  for (const json11::Json &point : items) {
    if (!point["lat"].is_number() || !point["lon"].is_number()) {
      continue;
    }
    points.push_back(GeoPoint{.lat = point["lat"].number_value(), .lon = point["lon"].number_value()});
  }
  return points;
}

std::optional<RoadClass> classify_road(std::string_view highway) {
  if (highway == "motorway" || highway == "motorway_link" || highway == "trunk" || highway == "trunk_link") {
    return RoadClass::Motorway;
  }
  if (highway == "primary" || highway == "primary_link") {
    return RoadClass::Primary;
  }
  if (highway == "secondary" || highway == "secondary_link" || highway == "tertiary" || highway == "tertiary_link") {
    return RoadClass::Secondary;
  }
  if (highway == "residential" || highway == "unclassified" || highway == "living_street" || highway == "road") {
    return RoadClass::Local;
  }
  return std::nullopt;
}

std::optional<RouteBasemap> parse_basemap_json(const std::string &raw, const GeoBounds &bounds, const std::string &key) {
  std::string parse_error;
  const json11::Json root = json11::Json::parse(raw, parse_error);
  if (!parse_error.empty() || !root.is_object()) {
    return std::nullopt;
  }

  RouteBasemap basemap;
  basemap.key = key;
  basemap.bounds = bounds;

  for (const json11::Json &element : root["elements"].array_items()) {
    if (element["type"].string_value() != "way") {
      continue;
    }
    const json11::Json &tags = element["tags"];
    const std::vector<GeoPoint> points = geometry_points(element["geometry"]);
    if (points.size() < 2) {
      continue;
    }

    const std::string highway = tags["highway"].string_value();
    if (!highway.empty()) {
      const std::optional<RoadClass> road_class = classify_road(highway);
      if (!road_class.has_value()) {
        continue;
      }
      basemap.roads.push_back(RoadFeature{
        .road_class = *road_class,
        .bounds = compute_feature_bounds(points),
        .points = points,
      });
      continue;
    }

    const std::string natural = tags["natural"].string_value();
    const std::string waterway = tags["waterway"].string_value();
    const bool closed = points.size() >= 4
                     && std::abs(points.front().lat - points.back().lat) < 1.0e-9
                     && std::abs(points.front().lon - points.back().lon) < 1.0e-9;
    if ((natural == "water" || waterway == "riverbank") && closed) {
      basemap.water_polygons.push_back(WaterPolygonFeature{
        .bounds = compute_feature_bounds(points),
        .ring = points,
      });
      continue;
    }
    if (waterway == "river" || waterway == "stream" || waterway == "canal") {
      basemap.water_lines.push_back(WaterLineFeature{
        .bounds = compute_feature_bounds(points),
        .points = points,
      });
    }
  }

  return basemap;
}

struct RoadPaint {
  ImU32 casing = 0;
  ImU32 fill = 0;
  float casing_width = 1.0f;
  float fill_width = 1.0f;
};

constexpr ImU32 MAP_BG_COLOR = IM_COL32(244, 243, 238, 255);
constexpr ImU32 MAP_WATER_FILL = IM_COL32(193, 216, 235, 185);
constexpr ImU32 MAP_WATER_OUTLINE = IM_COL32(143, 173, 201, 220);
constexpr ImU32 MAP_WATER_LINE = IM_COL32(156, 186, 214, 205);
constexpr ImU32 MAP_ROUTE_HALO = IM_COL32(31, 40, 50, 92);

RoadPaint road_paint(RoadClass road_class, int zoom) {
  const float scale = std::clamp(0.88f + 0.12f * static_cast<float>(zoom - 12), 0.76f, 1.95f);
  switch (road_class) {
    case RoadClass::Motorway:
      return {
        .casing = IM_COL32(163, 157, 149, 235),
        .fill = IM_COL32(245, 235, 215, 255),
        .casing_width = 5.6f * scale,
        .fill_width = 3.7f * scale,
      };
    case RoadClass::Primary:
      return {
        .casing = IM_COL32(171, 171, 168, 220),
        .fill = IM_COL32(249, 246, 237, 248),
        .casing_width = 4.6f * scale,
        .fill_width = 2.95f * scale,
      };
    case RoadClass::Secondary:
      return {
        .casing = IM_COL32(183, 186, 189, 210),
        .fill = IM_COL32(252, 251, 247, 240),
        .casing_width = 3.5f * scale,
        .fill_width = 2.15f * scale,
      };
    case RoadClass::Local:
    default:
      return {
        .casing = IM_COL32(200, 202, 205, 195),
        .fill = IM_COL32(255, 255, 254, 230),
        .casing_width = 2.5f * scale,
        .fill_width = 1.5f * scale,
      };
  }
}

void clamp_map_center(TabUiState::MapPaneState *map_state, const GeoBounds &bounds, const ImVec2 &size) {
  if (!bounds.valid() || size.x <= 1.0f || size.y <= 1.0f) {
    return;
  }
  const int zoom = map_state->zoom;
  const double min_x = lon_to_world_x(bounds.west, zoom);
  const double max_x = lon_to_world_x(bounds.east, zoom);
  const double min_y = lat_to_world_y(bounds.north, zoom);
  const double max_y = lat_to_world_y(bounds.south, zoom);
  const double half_w = size.x * 0.5;
  const double half_h = size.y * 0.5;
  double center_x = lon_to_world_x(map_state->center_lon, zoom);
  double center_y = lat_to_world_y(map_state->center_lat, zoom);
  if (max_x - min_x <= size.x) {
    center_x = (min_x + max_x) * 0.5;
  } else {
    center_x = std::clamp(center_x, min_x + half_w, max_x - half_w);
  }
  if (max_y - min_y <= size.y) {
    center_y = (min_y + max_y) * 0.5;
  } else {
    center_y = std::clamp(center_y, min_y + half_h, max_y - half_h);
  }
  map_state->center_lon = world_x_to_lon(center_x, zoom);
  map_state->center_lat = world_y_to_lat(center_y, zoom);
}

void initialize_map_pane_state(TabUiState::MapPaneState *map_state,
                               const GpsTrace &trace,
                               const GeoBounds &bounds,
                               ImVec2 size,
                               SessionDataMode mode,
                               std::optional<GpsPoint> cursor_point) {
  if (trace.points.empty()) {
    return;
  }
  map_state->initialized = true;
  map_state->follow = mode == SessionDataMode::Stream;
  const int min_zoom = minimum_allowed_map_zoom(bounds, trace, size);
  if (mode == SessionDataMode::Stream && cursor_point.has_value()) {
    map_state->zoom = std::max(16, min_zoom);
    map_state->center_lat = cursor_point->lat;
    map_state->center_lon = cursor_point->lon;
  } else {
    map_state->zoom = std::max(fit_map_zoom_for_trace(trace, size.x, size.y), min_zoom);
    map_state->center_lat = map_trace_center_lat(trace);
    map_state->center_lon = map_trace_center_lon(trace);
  }
  clamp_map_center(map_state, bounds, size);
}

void draw_feature_polyline(ImDrawList *draw_list,
                           const std::vector<GeoPoint> &points,
                           int zoom,
                           double top_left_x,
                           double top_left_y,
                           const ImVec2 &rect_min,
                           ImU32 color,
                           float thickness,
                           bool closed = false) {
  if (points.size() < 2) {
    return;
  }
  std::vector<ImVec2> screen;
  screen.reserve(points.size());
  for (const GeoPoint &point : points) {
    screen.push_back(gps_to_screen(point.lat, point.lon, zoom, top_left_x, top_left_y, rect_min));
  }
  draw_list->AddPolyline(screen.data(), static_cast<int>(screen.size()), color,
                         closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
}

void draw_water_polygon(ImDrawList *draw_list,
                        const WaterPolygonFeature &feature,
                        int zoom,
                        double top_left_x,
                        double top_left_y,
                        const ImVec2 &rect_min) {
  if (feature.ring.size() < 3) {
    return;
  }
  std::vector<ImVec2> screen;
  screen.reserve(feature.ring.size());
  for (const GeoPoint &point : feature.ring) {
    screen.push_back(gps_to_screen(point.lat, point.lon, zoom, top_left_x, top_left_y, rect_min));
  }
  if (screen.size() >= 3 && is_convex_ring(screen)) {
    draw_list->AddConvexPolyFilled(screen.data(), static_cast<int>(screen.size()), MAP_WATER_FILL);
  }
  draw_list->AddPolyline(screen.data(), static_cast<int>(screen.size()), MAP_WATER_OUTLINE,
                         ImDrawFlags_Closed, 1.8f);
}

}  // namespace

struct MapDataManager::Impl {
  struct Request {
    std::string key;
    GeoBounds bounds;
  };

  Impl() : worker([this]() { run(); }) {}

  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
    }
    cv.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void ensureTrace(const GpsTrace &trace) {
    if (trace.points.empty()) {
      return;
    }
    const GeoBounds wanted = requested_bounds_for_trace(trace);
    if (!wanted.valid()) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (current && bounds_contains_bounds(current->bounds, wanted)) {
      return;
    }
    if (pending && bounds_contains_bounds(pending->bounds, wanted)) {
      return;
    }

    pending = Request{
      .key = bounds_key(wanted),
      .bounds = wanted,
    };
    current.reset();
    cv.notify_one();
  }

  void pump() {
    std::optional<RouteBasemap> ready;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (completed) {
        ready = std::move(completed);
        completed.reset();
      }
    }
    if (ready) {
      current = std::make_unique<RouteBasemap>(std::move(*ready));
    }
  }

  bool loading() const {
    std::lock_guard<std::mutex> lock(mutex);
    return active || pending;
  }

  const RouteBasemap *currentData() const {
    return current.get();
  }

  void run() {
    while (true) {
      Request request;
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return stopping || pending.has_value(); });
        if (stopping) {
          return;
        }
        request = *pending;
        active = pending;
        pending.reset();
      }

      std::optional<RouteBasemap> parsed;
      const std::string raw = load_overpass_json(request.bounds, request.key);
      if (!raw.empty()) {
        parsed = parse_basemap_json(raw, request.bounds, request.key);
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        if (active && active->key == request.key) {
          completed = std::move(parsed);
          active.reset();
        }
      }
    }
  }

  mutable std::mutex mutex;
  std::condition_variable cv;
  bool stopping = false;
  std::optional<Request> pending;
  std::optional<Request> active;
  std::optional<RouteBasemap> completed;
  std::unique_ptr<RouteBasemap> current;
  std::thread worker;
};

MapDataManager::MapDataManager() : impl_(std::make_unique<Impl>()) {}
MapDataManager::~MapDataManager() = default;

void MapDataManager::pump() {
  impl_->pump();
}

void MapDataManager::ensureTrace(const GpsTrace &trace) {
  impl_->ensureTrace(trace);
}

bool MapDataManager::loading() const {
  return impl_->loading();
}

const RouteBasemap *MapDataManager::current() const {
  return impl_->currentData();
}

void draw_map_pane(AppSession *session, UiState *state, Pane *, int pane_index) {
  TabUiState *tab_state = app_active_tab_state(state);
  if (tab_state == nullptr || pane_index < 0 || pane_index >= static_cast<int>(tab_state->map_panes.size())) {
    ImGui::TextUnformatted("Map unavailable");
    return;
  }
  if (!session->map_data) {
    ImGui::TextUnformatted("Map unavailable");
    return;
  }

  session->map_data->ensureTrace(session->route_data.gps_trace);
  session->map_data->pump();

  TabUiState::MapPaneState &map_state = tab_state->map_panes[static_cast<size_t>(pane_index)];
  const GpsTrace &trace = session->route_data.gps_trace;
  const RouteBasemap *basemap = session->map_data->current();
  const GeoBounds map_bounds = basemap != nullptr ? basemap->bounds : requested_bounds_for_trace(trace);

  const ImVec2 rect_min = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  const ImVec2 input_size(std::max(1.0f, size.x - 22.0f), std::max(1.0f, size.y));
  ImGui::InvisibleButton("##map_canvas", input_size);
  const ImVec2 rect_max(rect_min.x + size.x, rect_min.y + size.y);
  const float rect_width = rect_max.x - rect_min.x;
  const float rect_height = rect_max.y - rect_min.y;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  draw_list->PushClipRect(rect_min, rect_max, true);
  draw_list->AddRectFilled(rect_min, rect_max, MAP_BG_COLOR);

  if (trace.points.empty()) {
    const char *label = session->async_route_loading ? "Loading map..." : "No GPS trace";
    const ImVec2 text = ImGui::CalcTextSize(label);
    draw_list->AddText(ImVec2(rect_min.x + (rect_width - text.x) * 0.5f,
                              rect_min.y + (rect_height - text.y) * 0.5f),
                       IM_COL32(110, 118, 128, 255), label);
    draw_list->PopClipRect();
    return;
  }

  const std::optional<GpsPoint> cursor_point = state->has_tracker_time
    ? interpolate_gps(trace, state->tracker_time)
    : std::optional<GpsPoint>{};
  if (!map_state.initialized) {
    initialize_map_pane_state(&map_state, trace, map_bounds, size, session->data_mode, cursor_point);
  }

  const int min_zoom = minimum_allowed_map_zoom(map_bounds, trace, size);
  if (map_state.follow && cursor_point.has_value()) {
    const int follow_zoom = std::clamp(map_state.zoom, min_zoom, MAP_MAX_ZOOM);
    const double center_x = lon_to_world_x(map_state.center_lon, follow_zoom);
    const double center_y = lat_to_world_y(map_state.center_lat, follow_zoom);
    const double top_left_x = center_x - rect_width * 0.5;
    const double top_left_y = center_y - rect_height * 0.5;
    const ImVec2 car_screen = gps_to_screen(cursor_point->lat, cursor_point->lon, follow_zoom, top_left_x, top_left_y, rect_min);
    if (!point_in_rect_with_margin(car_screen, rect_min, rect_max, 0.22f)) {
      map_state.center_lat = cursor_point->lat;
      map_state.center_lon = cursor_point->lon;
    }
  }

  map_state.zoom = std::clamp(map_state.zoom, min_zoom, MAP_MAX_ZOOM);
  clamp_map_center(&map_state, map_bounds, size);

  const int zoom = map_state.zoom;
  const double center_x = lon_to_world_x(map_state.center_lon, zoom);
  const double center_y = lat_to_world_y(map_state.center_lat, zoom);
  const double top_left_x = center_x - rect_width * 0.5;
  const double top_left_y = center_y - rect_height * 0.5;
  const GeoBounds current_view = view_bounds(top_left_x, top_left_y, rect_width, rect_height, zoom);

  if (basemap != nullptr) {
    for (const WaterPolygonFeature &water : basemap->water_polygons) {
      if (feature_intersects_view(water.bounds, current_view)) {
        draw_water_polygon(draw_list, water, zoom, top_left_x, top_left_y, rect_min);
      }
    }
    for (const WaterLineFeature &water : basemap->water_lines) {
      if (feature_intersects_view(water.bounds, current_view)) {
        draw_feature_polyline(draw_list, water.points, zoom, top_left_x, top_left_y, rect_min,
                              MAP_WATER_LINE, 2.4f);
      }
    }

    std::array<RoadClass, 4> order = {
      RoadClass::Local,
      RoadClass::Secondary,
      RoadClass::Primary,
      RoadClass::Motorway,
    };
    for (RoadClass road_class : order) {
      const RoadPaint paint = road_paint(road_class, zoom);
      for (const RoadFeature &road : basemap->roads) {
        if (road.road_class != road_class || !feature_intersects_view(road.bounds, current_view)) {
          continue;
        }
        draw_feature_polyline(draw_list, road.points, zoom, top_left_x, top_left_y, rect_min,
                              paint.casing, paint.casing_width);
        draw_feature_polyline(draw_list, road.points, zoom, top_left_x, top_left_y, rect_min,
                              paint.fill, paint.fill_width);
      }
    }
  }

  for (size_t i = 1; i < trace.points.size(); ++i) {
    const GpsPoint &p0 = trace.points[i - 1];
    const GpsPoint &p1 = trace.points[i];
    const ImVec2 s0 = gps_to_screen(p0.lat, p0.lon, zoom, top_left_x, top_left_y, rect_min);
    const ImVec2 s1 = gps_to_screen(p1.lat, p1.lon, zoom, top_left_x, top_left_y, rect_min);
    draw_list->AddLine(s0, s1, MAP_ROUTE_HALO, 5.8f);
    draw_list->AddLine(s0, s1, map_timeline_color(p1.type, 1.0f), 3.25f);
  }

  if (cursor_point.has_value()) {
    const ImVec2 marker = gps_to_screen(cursor_point->lat, cursor_point->lon, zoom, top_left_x, top_left_y, rect_min);
    const float marker_size = std::clamp(9.0f + 1.0f * static_cast<float>(zoom - min_zoom), 9.0f, 20.0f);
    draw_car_marker(draw_list, marker, cursor_point->bearing, map_timeline_color(cursor_point->type, 1.0f), marker_size);
  }

  if (session->map_data->loading()) {
    const char *label = basemap != nullptr ? "Refreshing roads..." : "Loading roads...";
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 pos(rect_min.x + 12.0f, rect_max.y - text.y - 12.0f);
    draw_list->AddRectFilled(ImVec2(pos.x - 6.0f, pos.y - 4.0f),
                             ImVec2(pos.x + text.x + 6.0f, pos.y + text.y + 4.0f),
                             IM_COL32(255, 255, 255, 180), 4.0f);
    draw_list->AddText(pos, IM_COL32(84, 93, 105, 255), label);
  }
  draw_list->PopClipRect();

  const bool hovered = ImGui::IsItemHovered();
  const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    const int next_zoom = std::clamp(zoom + (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1), min_zoom, MAP_MAX_ZOOM);
    if (next_zoom != zoom) {
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const double mouse_world_x = top_left_x + (mouse.x - rect_min.x);
      const double mouse_world_y = top_left_y + (mouse.y - rect_min.y);
      const double mouse_lon = world_x_to_lon(mouse_world_x, zoom);
      const double mouse_lat = world_y_to_lat(mouse_world_y, zoom);
      const double next_center_x = lon_to_world_x(mouse_lon, next_zoom) - (mouse.x - rect_min.x) + rect_width * 0.5;
      const double next_center_y = lat_to_world_y(mouse_lat, next_zoom) - (mouse.y - rect_min.y) + rect_height * 0.5;
      map_state.zoom = next_zoom;
      map_state.center_lon = world_x_to_lon(next_center_x, next_zoom);
      map_state.center_lat = world_y_to_lat(next_center_y, next_zoom);
      map_state.follow = false;
      clamp_map_center(&map_state, map_bounds, size);
    }
  }
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    const double next_center_x = center_x - delta.x;
    const double next_center_y = center_y - delta.y;
    map_state.center_lon = world_x_to_lon(next_center_x, zoom);
    map_state.center_lat = world_y_to_lat(next_center_y, zoom);
    map_state.follow = false;
    clamp_map_center(&map_state, map_bounds, size);
  } else if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    const ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    if (drag_delta.x * drag_delta.x + drag_delta.y * drag_delta.y < 16.0f) {
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      double best_dist = std::numeric_limits<double>::max();
      double best_time = state->tracker_time;
      for (const GpsPoint &point : trace.points) {
        const ImVec2 screen = gps_to_screen(point.lat, point.lon, zoom, top_left_x, top_left_y, rect_min);
        const double dx = static_cast<double>(screen.x - mouse.x);
        const double dy = static_cast<double>(screen.y - mouse.y);
        const double dist = dx * dx + dy * dy;
        if (dist < best_dist) {
          best_dist = dist;
          best_time = point.time;
        }
      }
      state->tracker_time = best_time;
      state->has_tracker_time = true;
    }
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
  }
  if (double_clicked) {
    map_state.initialized = false;
  }
}
