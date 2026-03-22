#include "tools/jotpluggler/jotpluggler.h"
#include "tools/jotpluggler/app_map.h"

#include "imgui_impl_opengl3_loader.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <GLFW/glfw3.h>

#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "tools/replay/py_downloader.h"

namespace fs = std::filesystem;

namespace {

constexpr int MAP_TILE_SIZE = 256;
constexpr int MAP_MIN_ZOOM = 1;
constexpr int MAP_MAX_ZOOM = 18;
constexpr int MAP_REASONABLE_MIN_ZOOM = 8;
constexpr int MAP_SINGLE_POINT_MIN_ZOOM = 10;
constexpr size_t MAP_MAX_TEXTURES = 128;
constexpr const char *MAP_TILE_STYLE = "osm_standard";

struct TileRequest {
  int z = 0;
  int x = 0;
  int y = 0;
};

struct TileResult {
  int z = 0;
  int x = 0;
  int y = 0;
  std::string png;
};

struct UploadedTile {
  uint32_t texture_id = 0;
  int z = 0;
  int x = 0;
  int y = 0;
};

struct DecodedTile {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;
};

uint64_t tile_key(int z, int x, int y) {
  return (static_cast<uint64_t>(z & 0xff) << 56)
       | (static_cast<uint64_t>(x & 0x0fffffff) << 28)
       | static_cast<uint64_t>(y & 0x0fffffff);
}

fs::path tile_cache_path() {
  const char *home = std::getenv("HOME");
  fs::path dir = home != nullptr ? fs::path(home) / ".comma" : fs::temp_directory_path();
  fs::create_directories(dir);
  return dir / "jotpluggler_tiles.db";
}

std::string tile_url(int z, int x, int y) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
  return buf;
}

std::string read_binary_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

double lon_to_world_x(double lon, int z) {
  return (lon + 180.0) / 360.0 * static_cast<double>(1 << z) * MAP_TILE_SIZE;
}

double lat_to_world_y(double lat, int z) {
  const double lat_rad = lat * M_PI / 180.0;
  return (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI)
       / 2.0 * static_cast<double>(1 << z) * MAP_TILE_SIZE;
}

double world_x_to_lon(double x, int z) {
  return x / (static_cast<double>(1 << z) * MAP_TILE_SIZE) * 360.0 - 180.0;
}

double world_y_to_lat(double y, int z) {
  const double n = M_PI - (2.0 * M_PI * y) / (static_cast<double>(1 << z) * MAP_TILE_SIZE);
  return 180.0 / M_PI * std::atan(std::sinh(n));
}

int fit_map_zoom(const GpsTrace &trace, float width, float height) {
  if (trace.points.size() <= 1) {
    return 16;
  }
  for (int z = MAP_MAX_ZOOM; z >= MAP_MIN_ZOOM; --z) {
    const double pixel_width = std::abs(lon_to_world_x(trace.max_lon, z) - lon_to_world_x(trace.min_lon, z));
    const double pixel_height = std::abs(lat_to_world_y(trace.min_lat, z) - lat_to_world_y(trace.max_lat, z));
    if (pixel_width <= width * 0.82 && pixel_height <= height * 0.82) {
      return z;
    }
  }
  return MAP_MIN_ZOOM;
}

int minimum_allowed_map_zoom(const GpsTrace &trace, ImVec2 size) {
  if (trace.points.size() <= 1) {
    return MAP_SINGLE_POINT_MIN_ZOOM;
  }
  const int fit_zoom = fit_map_zoom(trace, size.x, size.y);
  return std::clamp(std::max(MAP_REASONABLE_MIN_ZOOM, fit_zoom - 2), MAP_MIN_ZOOM, MAP_MAX_ZOOM);
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

std::optional<DecodedTile> decode_png_rgba(const std::string &png) {
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PNG);
  if (codec == nullptr) {
    return std::nullopt;
  }

  AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
  if (codec_ctx == nullptr) {
    return std::nullopt;
  }

  std::optional<DecodedTile> decoded;
  AVPacket *packet = nullptr;
  AVFrame *frame = nullptr;

  do {
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
      break;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
      break;
    }
    if (av_new_packet(packet, static_cast<int>(png.size())) < 0) {
      break;
    }
    std::memcpy(packet->data, png.data(), png.size());
    if (avcodec_send_packet(codec_ctx, packet) < 0) {
      break;
    }
    if (avcodec_receive_frame(codec_ctx, frame) < 0) {
      break;
    }

    DecodedTile image;
    image.width = frame->width;
    image.height = frame->height;
    image.rgba.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4U);
    const auto *src = frame->data[0];
    const int linesize = frame->linesize[0];
    const auto format = static_cast<AVPixelFormat>(frame->format);
    bool ok = true;
    for (int y = 0; y < frame->height && ok; ++y) {
      const uint8_t *row = src + static_cast<size_t>(y) * static_cast<size_t>(linesize);
      uint8_t *out = image.rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(image.width) * 4U;
      switch (format) {
        case AV_PIX_FMT_RGBA:
          std::memcpy(out, row, static_cast<size_t>(image.width) * 4U);
          break;
        case AV_PIX_FMT_BGRA:
          for (int x = 0; x < image.width; ++x) {
            out[x * 4 + 0] = row[x * 4 + 2];
            out[x * 4 + 1] = row[x * 4 + 1];
            out[x * 4 + 2] = row[x * 4 + 0];
            out[x * 4 + 3] = row[x * 4 + 3];
          }
          break;
        case AV_PIX_FMT_RGB24:
          for (int x = 0; x < image.width; ++x) {
            out[x * 4 + 0] = row[x * 3 + 0];
            out[x * 4 + 1] = row[x * 3 + 1];
            out[x * 4 + 2] = row[x * 3 + 2];
            out[x * 4 + 3] = 255;
          }
          break;
        case AV_PIX_FMT_BGR24:
          for (int x = 0; x < image.width; ++x) {
            out[x * 4 + 0] = row[x * 3 + 2];
            out[x * 4 + 1] = row[x * 3 + 1];
            out[x * 4 + 2] = row[x * 3 + 0];
            out[x * 4 + 3] = 255;
          }
          break;
        case AV_PIX_FMT_GRAY8:
          for (int x = 0; x < image.width; ++x) {
            out[x * 4 + 0] = row[x];
            out[x * 4 + 1] = row[x];
            out[x * 4 + 2] = row[x];
            out[x * 4 + 3] = 255;
          }
          break;
        case AV_PIX_FMT_PAL8: {
          const uint8_t *palette = frame->data[1];
          if (palette == nullptr) {
            ok = false;
            break;
          }
          for (int x = 0; x < image.width; ++x) {
            const uint8_t index = row[x];
            const uint8_t *entry = palette + static_cast<size_t>(index) * 4U;
            out[x * 4 + 0] = entry[2];
            out[x * 4 + 1] = entry[1];
            out[x * 4 + 2] = entry[0];
            out[x * 4 + 3] = entry[3];
          }
          break;
        }
        default:
          ok = false;
          break;
      }
    }
    if (!ok) {
      break;
    }
    decoded = std::move(image);
  } while (false);

  if (frame != nullptr) {
    av_frame_free(&frame);
  }
  if (packet != nullptr) {
    av_packet_free(&packet);
  }
  avcodec_free_context(&codec_ctx);
  return decoded;
}

uint32_t upload_tile_texture(const DecodedTile &tile) {
  uint32_t texture_id = 0;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile.width, tile.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tile.rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture_id;
}

class TileSqliteCache {
public:
  TileSqliteCache() = default;
  ~TileSqliteCache() = default;

  std::string get(int z, int x, int y, std::string_view style) {
    return read_binary_file(cache_path(z, x, y, style));
  }

  void put(int z, int x, int y, std::string_view style, const std::string &data) {
    if (data.empty()) {
      return;
    }
    const fs::path path = cache_path(z, x, y, style);
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) {
      out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
  }

private:
  static fs::path cache_path(int z, int x, int y, std::string_view style) {
    fs::path root = tile_cache_path();
    root.replace_extension("");
    return root / std::string(style) / std::to_string(z) / std::to_string(x) / (std::to_string(y) + ".png");
  }
};

}  // namespace

struct MapTileManager::Impl {
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
    for (auto &entry : textures) {
      if (entry.second->texture_id != 0) {
        glDeleteTextures(1, &entry.second->texture_id);
      }
    }
  }

  uint32_t textureFor(int z, int x, int y) {
    pump();
    if (z < MAP_MIN_ZOOM || z > MAP_MAX_ZOOM) {
      return 0;
    }
    const int limit = 1 << z;
    if (x < 0 || x >= limit || y < 0 || y >= limit) {
      return 0;
    }
    const uint64_t key = tile_key(z, x, y);
    auto it = textures.find(key);
    if (it != textures.end()) {
      lru.splice(lru.begin(), lru, it->second);
      return it->second->texture_id;
    }
    requestTile(z, x, y);
    return 0;
  }

  void pump() {
    std::vector<TileResult> results;
    {
      std::lock_guard<std::mutex> lock(mutex);
      results.swap(completed);
    }
    for (TileResult &result : results) {
      if (result.png.empty()) {
        continue;
      }
      const std::optional<DecodedTile> decoded = decode_png_rgba(result.png);
      if (!decoded.has_value()) {
        continue;
      }
      UploadedTile tile;
      tile.texture_id = upload_tile_texture(*decoded);
      tile.z = result.z;
      tile.x = result.x;
      tile.y = result.y;
      lru.push_front(tile);
      textures[tile_key(tile.z, tile.x, tile.y)] = lru.begin();
      while (lru.size() > MAP_MAX_TEXTURES) {
        UploadedTile &oldest = lru.back();
        if (oldest.texture_id != 0) {
          glDeleteTextures(1, &oldest.texture_id);
        }
        textures.erase(tile_key(oldest.z, oldest.x, oldest.y));
        lru.pop_back();
      }
    }
  }

  void requestTile(int z, int x, int y) {
    const uint64_t key = tile_key(z, x, y);
    std::lock_guard<std::mutex> lock(mutex);
    if (pending_keys.find(key) != pending_keys.end()) {
      return;
    }
    pending_keys.insert(key);
    pending.push_back(TileRequest{z, x, y});
    cv.notify_one();
  }

  void run() {
    while (true) {
      TileRequest request;
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return stopping || !pending.empty(); });
        if (stopping) {
          return;
        }
        request = pending.front();
        pending.pop_front();
      }

      std::string png = cache.get(request.z, request.x, request.y, MAP_TILE_STYLE);
      if (png.empty()) {
        const std::string path = PyDownloader::download(tile_url(request.z, request.x, request.y), true, nullptr);
        if (!path.empty()) {
          png = read_binary_file(path);
          if (!png.empty()) {
            cache.put(request.z, request.x, request.y, MAP_TILE_STYLE, png);
          }
        }
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        pending_keys.erase(tile_key(request.z, request.x, request.y));
        if (!png.empty()) {
          completed.push_back(TileResult{request.z, request.x, request.y, std::move(png)});
        }
      }
    }
  }

  TileSqliteCache cache;
  std::mutex mutex;
  std::condition_variable cv;
  bool stopping = false;
  std::deque<TileRequest> pending;
  std::vector<TileResult> completed;
  std::unordered_set<uint64_t> pending_keys;
  std::list<UploadedTile> lru;
  std::unordered_map<uint64_t, std::list<UploadedTile>::iterator> textures;
  std::thread worker;
};

MapTileManager::MapTileManager() : impl_(std::make_unique<Impl>()) {}
MapTileManager::~MapTileManager() = default;

uint32_t MapTileManager::textureFor(int z, int x, int y) {
  return impl_->textureFor(z, x, y);
}

void MapTileManager::pump() {
  impl_->pump();
}

namespace {

double map_trace_center_lat(const GpsTrace &trace) {
  return (trace.min_lat + trace.max_lat) * 0.5;
}

double map_trace_center_lon(const GpsTrace &trace) {
  return (trace.min_lon + trace.max_lon) * 0.5;
}

void initialize_map_pane_state(TabUiState::MapPaneState *map_state,
                               const GpsTrace &trace,
                               ImVec2 size,
                               SessionDataMode mode,
                               std::optional<GpsPoint> cursor_point) {
  if (trace.points.empty()) {
    return;
  }
  map_state->initialized = true;
  map_state->follow = mode == SessionDataMode::Stream;
  const int min_zoom = minimum_allowed_map_zoom(trace, size);
  if (mode == SessionDataMode::Stream && cursor_point.has_value()) {
    map_state->zoom = std::max(16, min_zoom);
    map_state->center_lat = cursor_point->lat;
    map_state->center_lon = cursor_point->lon;
  } else {
    map_state->zoom = std::max(fit_map_zoom(trace, size.x, size.y), min_zoom);
    map_state->center_lat = map_trace_center_lat(trace);
    map_state->center_lon = map_trace_center_lon(trace);
  }
}

ImVec2 gps_to_screen(double lat, double lon, int zoom, double top_left_x, double top_left_y,
                     const ImVec2 &rect_min) {
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

void draw_car_marker(ImDrawList *draw_list, ImVec2 center, float bearing_deg, ImU32 color) {
  const float rad = bearing_deg * static_cast<float>(M_PI / 180.0);
  const ImVec2 forward(std::sin(rad), -std::cos(rad));
  const ImVec2 perp(-forward.y, forward.x);
  const float size = 10.0f;
  const ImVec2 tip(center.x + forward.x * size, center.y + forward.y * size);
  const ImVec2 base(center.x - forward.x * size * 0.45f, center.y - forward.y * size * 0.45f);
  const ImVec2 left(base.x + perp.x * size * 0.6f, base.y + perp.y * size * 0.6f);
  const ImVec2 right(base.x - perp.x * size * 0.6f, base.y - perp.y * size * 0.6f);
  draw_list->AddTriangleFilled(tip, left, right, color);
  draw_list->AddTriangle(tip, left, right, IM_COL32(255, 255, 255, 210), 2.0f);
}

}  // namespace

void draw_map_pane(AppSession *session, UiState *state, Pane *, int pane_index) {
  TabUiState *tab_state = app_active_tab_state(state);
  if (tab_state == nullptr || pane_index < 0 || pane_index >= static_cast<int>(tab_state->map_panes.size())) {
    ImGui::TextUnformatted("Map unavailable");
    return;
  }
  if (!session->map_tiles) {
    ImGui::TextUnformatted("Map unavailable");
    return;
  }

  session->map_tiles->pump();
  TabUiState::MapPaneState &map_state = tab_state->map_panes[static_cast<size_t>(pane_index)];
  const GpsTrace &trace = session->route_data.gps_trace;
  const ImVec2 rect_min = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  const ImVec2 input_size(std::max(1.0f, size.x - 22.0f), std::max(1.0f, size.y));
  ImGui::InvisibleButton("##map_canvas", input_size);
  const ImVec2 rect_max(rect_min.x + size.x, rect_min.y + size.y);
  const float rect_width = rect_max.x - rect_min.x;
  const float rect_height = rect_max.y - rect_min.y;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  if (trace.points.empty()) {
    const char *label = session->async_route_loading ? "Loading map..." : "No GPS trace";
    const ImVec2 text = ImGui::CalcTextSize(label);
    draw_list->AddText(ImVec2(rect_min.x + (rect_width - text.x) * 0.5f,
                              rect_min.y + (rect_height - text.y) * 0.5f),
                       IM_COL32(110, 118, 128, 255), label);
    return;
  }

  const std::optional<GpsPoint> cursor_point = state->has_tracker_time
    ? interpolate_gps(trace, state->tracker_time)
    : std::optional<GpsPoint>{};
  if (!map_state.initialized) {
    initialize_map_pane_state(&map_state, trace, size, session->data_mode, cursor_point);
  }

  if (map_state.follow && cursor_point.has_value()) {
    const int min_zoom = minimum_allowed_map_zoom(trace, size);
    const int zoom = std::clamp(map_state.zoom, min_zoom, MAP_MAX_ZOOM);
    const double center_x = lon_to_world_x(map_state.center_lon, zoom);
    const double center_y = lat_to_world_y(map_state.center_lat, zoom);
    const double top_left_x = center_x - rect_width * 0.5;
    const double top_left_y = center_y - rect_height * 0.5;
    const ImVec2 car_screen = gps_to_screen(cursor_point->lat, cursor_point->lon, zoom, top_left_x, top_left_y, rect_min);
    if (!point_in_rect_with_margin(car_screen, rect_min, rect_max, 0.15f)) {
      map_state.center_lat = cursor_point->lat;
      map_state.center_lon = cursor_point->lon;
    }
  }

  const int min_zoom = minimum_allowed_map_zoom(trace, size);
  const int zoom = std::clamp(map_state.zoom, min_zoom, MAP_MAX_ZOOM);
  const double center_x = lon_to_world_x(map_state.center_lon, zoom);
  const double center_y = lat_to_world_y(map_state.center_lat, zoom);
  const double top_left_x = center_x - rect_width * 0.5;
  const double top_left_y = center_y - rect_height * 0.5;
  const int tile_min_x = std::max(0, static_cast<int>(std::floor(top_left_x / MAP_TILE_SIZE)));
  const int tile_min_y = std::max(0, static_cast<int>(std::floor(top_left_y / MAP_TILE_SIZE)));
  const int tile_max_x = std::min((1 << zoom) - 1, static_cast<int>(std::floor((top_left_x + rect_width) / MAP_TILE_SIZE)));
  const int tile_max_y = std::min((1 << zoom) - 1, static_cast<int>(std::floor((top_left_y + rect_height) / MAP_TILE_SIZE)));

  draw_list->PushClipRect(rect_min, rect_max, true);
  draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(232, 235, 239, 255));
  for (int ty = tile_min_y; ty <= tile_max_y; ++ty) {
    for (int tx = tile_min_x; tx <= tile_max_x; ++tx) {
      const ImVec2 tile_min(rect_min.x + static_cast<float>(tx * MAP_TILE_SIZE - top_left_x),
                            rect_min.y + static_cast<float>(ty * MAP_TILE_SIZE - top_left_y));
      const ImVec2 tile_max(tile_min.x + MAP_TILE_SIZE, tile_min.y + MAP_TILE_SIZE);
      const uint32_t texture = session->map_tiles->textureFor(zoom, tx, ty);
      if (texture != 0) {
        draw_list->AddImage(static_cast<ImTextureID>(static_cast<uintptr_t>(texture)), tile_min, tile_max);
        draw_list->AddRectFilled(tile_min, tile_max, IM_COL32(255, 255, 255, 30));
      } else {
        draw_list->AddRectFilled(tile_min, tile_max, IM_COL32(226, 229, 234, 255));
        draw_list->AddRect(tile_min, tile_max, IM_COL32(212, 216, 222, 255));
      }
    }
  }

  for (size_t i = 1; i < trace.points.size(); ++i) {
    const GpsPoint &p0 = trace.points[i - 1];
    const GpsPoint &p1 = trace.points[i];
    const ImVec2 s0 = gps_to_screen(p0.lat, p0.lon, zoom, top_left_x, top_left_y, rect_min);
    const ImVec2 s1 = gps_to_screen(p1.lat, p1.lon, zoom, top_left_x, top_left_y, rect_min);
    draw_list->AddLine(s0, s1, IM_COL32(0, 0, 0, 76), 5.0f);
    draw_list->AddLine(s0, s1, map_timeline_color(p1.type, 1.0f), 3.0f);
  }

  if (cursor_point.has_value()) {
    const ImVec2 marker = gps_to_screen(cursor_point->lat, cursor_point->lon, zoom, top_left_x, top_left_y, rect_min);
    draw_car_marker(draw_list, marker, cursor_point->bearing, map_timeline_color(cursor_point->type, 1.0f));
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
    }
  }
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    const double next_center_x = center_x - delta.x;
    const double next_center_y = center_y - delta.y;
    map_state.center_lon = world_x_to_lon(next_center_x, zoom);
    map_state.center_lat = world_y_to_lat(next_center_y, zoom);
    map_state.follow = false;
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
