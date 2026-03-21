#include "tools/jotpluggler/app.h"
#include "tools/jotpluggler/bootstrap_icons.h"
#include "imgui_impl_glfw.h"
#include "tools/jotpluggler/sketch_layout.h"
#include "tools/replay/framereader.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "implot.h"
#include "libyuv.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "system/camerad/cameras/nv12_info.h"

namespace jotpluggler {
namespace fs = std::filesystem;

namespace {

constexpr const char *kUntitledPaneTitle = "...";

constexpr float kSidebarWidth = 282.0f;
constexpr float kSidebarMinWidth = 220.0f;
constexpr float kSidebarMaxWidth = 520.0f;
constexpr float kContentGap = 0.0f;
constexpr float kContentRightPadding = 0.0f;
constexpr float kStatusBarHeight = 40.0f;
constexpr double kMinHorizontalZoomSeconds = 2.0;
constexpr double kPlotYPadFraction = 0.28;
constexpr size_t kCursorOverlayValueCount = 3;

struct UiMetrics {
  float width = 0.0f;
  float height = 0.0f;
  float top_offset = 0.0f;
  float sidebar_width = kSidebarWidth;
  float content_x = 0.0f;
  float content_y = 0.0f;
  float content_w = 0.0f;
  float content_h = 0.0f;
  float status_bar_y = 0.0f;
};

struct BrowserNode {
  std::string label;
  std::string full_path;
  std::vector<BrowserNode> children;
};

class AsyncRouteLoader;
class SidebarCameraFeed;

struct AppSession {
  fs::path layout_path;
  fs::path autosave_path;
  std::string route_name;
  std::string data_dir;
  SketchLayout layout;
  RouteData route_data;
  std::unordered_map<std::string, const RouteSeries *> series_by_path;
  std::vector<BrowserNode> browser_nodes;
  std::unique_ptr<AsyncRouteLoader> route_loader;
  std::unique_ptr<SidebarCameraFeed> camera_feed;
  bool async_route_loading = false;
};

struct PlotBounds {
  double x_min = 0.0;
  double x_max = 1.0;
  double y_min = 0.0;
  double y_max = 1.0;
};

struct TabUiState {
  bool dock_needs_build = true;
  int active_pane_index = 0;
  int runtime_id = 0;
};

struct CustomSeriesEditorState {
  bool open = false;
  bool open_help = false;
  bool request_select = false;
  bool selected = false;
  bool focus_name = false;
  int selected_template = 0;
  int selected_additional_source = -1;
  std::string name;
  std::string linked_source;
  std::vector<std::string> additional_sources;
  std::string globals_code;
  std::string function_code = "return value";
  std::string preview_label;
  std::vector<double> preview_xs;
  std::vector<double> preview_ys;
  bool preview_is_result = false;
};

struct UiState {
  bool open_open_route = false;
  bool open_load_layout = false;
  bool open_save_layout = false;
  bool request_close = false;
  bool request_reset_layout = false;
  bool request_save_layout = false;
  bool request_new_tab = false;
  bool request_duplicate_tab = false;
  bool request_close_tab = false;
  bool follow_latest = false;
  bool has_shared_range = false;
  bool has_tracker_time = false;
  bool layout_dirty = false;
  bool playback_loop = false;
  bool playback_playing = false;
  bool show_deprecated_fields = false;
  bool suppress_range_side_effects = false;
  int active_tab_index = 0;
  int next_tab_runtime_id = 1;
  int requested_tab_index = -1;
  int rename_tab_index = -1;
  bool focus_rename_tab_input = false;
  std::vector<TabUiState> tabs;
  std::array<char, 128> route_buffer = {};
  std::array<char, 128> rename_tab_buffer = {};
  std::array<char, 128> browser_filter = {};
  std::array<char, 512> data_dir_buffer = {};
  std::array<char, 512> load_layout_buffer = {};
  std::array<char, 512> save_layout_buffer = {};
  std::string selected_browser_path;
  std::vector<std::string> selected_browser_paths;
  std::string browser_selection_anchor;
  std::string error_text;
  bool open_error_popup = false;
  std::string status_text = "Ready";
  float sidebar_width = kSidebarWidth;
  double route_x_min = 0.0;
  double route_x_max = 1.0;
  double x_view_min = 0.0;
  double x_view_max = 1.0;
  double tracker_time = 0.0;
  double playback_rate = 1.0;
  double playback_step = 0.1;
  CustomSeriesEditorState custom_series;
};

struct PythonEvalResult {
  std::vector<double> xs;
  std::vector<double> ys;
};

struct CustomSeriesTemplate {
  const char *name;
  const char *globals_code;
  const char *function_code;
  const char *preview_text;
};

struct RouteLoadSnapshot {
  bool active = false;
  size_t total_segments = 0;
  size_t segments_downloaded = 0;
  size_t segments_parsed = 0;
};

void glfw_error_callback(int error, const char *description) {
  const std::string_view desc = description != nullptr ? description : "unknown";
  if (error == 65539 && desc.find("Invalid window attribute 0x0002000D") != std::string_view::npos) {
    return;
  }
  std::cerr << "GLFW error " << error << ": " << (description != nullptr ? description : "unknown") << "\n";
}

class GlfwRuntime {
public:
  explicit GlfwRuntime(const Options &options) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
      throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    const bool fixed_size = !options.show;
    glfwWindowHint(GLFW_RESIZABLE, fixed_size ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, options.show ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(options.width, options.height, "jotpluggler", nullptr, nullptr);
    if (window_ == nullptr) {
      glfwTerminate();
      throw std::runtime_error("glfwCreateWindow failed");
    }

    if (fixed_size) {
      glfwSetWindowSizeLimits(window_, options.width, options.height, options.width, options.height);
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(options.show ? 1 : 0);
  }

  ~GlfwRuntime() {
    if (window_ != nullptr) {
      glfwDestroyWindow(window_);
    }
    glfwTerminate();
  }

  GlfwRuntime(const GlfwRuntime &) = delete;
  GlfwRuntime &operator=(const GlfwRuntime &) = delete;

  GLFWwindow *window() const {
    return window_;
  }

private:
  GLFWwindow *window_ = nullptr;
};

class ImGuiRuntime {
public:
  explicit ImGuiRuntime(GLFWwindow *window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
      ImPlot::DestroyContext();
      ImGui::DestroyContext();
      throw std::runtime_error("ImGui_ImplGlfw_InitForOpenGL failed");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
      ImGui_ImplGlfw_Shutdown();
      ImPlot::DestroyContext();
      ImGui::DestroyContext();
      throw std::runtime_error("ImGui_ImplOpenGL3_Init failed");
    }
  }

  ~ImGuiRuntime() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
  }

  ImGuiRuntime(const ImGuiRuntime &) = delete;
  ImGuiRuntime &operator=(const ImGuiRuntime &) = delete;
};

class TerminalRouteProgress {
public:
  explicit TerminalRouteProgress(bool enabled) : enabled_(enabled) {}

  void update(const RouteLoadProgress &progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_) {
      return;
    }

    double overall = 0.0;
    std::string detail = "Resolving route";
    if (progress.stage == RouteLoadStage::Finished) {
      overall = 1.0;
      detail = "Ready";
    } else if (progress.total_segments > 0) {
      const double total_work = static_cast<double>(progress.total_segments) * 2.0;
      const double complete_work = static_cast<double>(progress.segments_downloaded + progress.segments_parsed);
      overall = total_work <= 0.0 ? 0.0 : std::clamp(complete_work / total_work, 0.0, 1.0);
      std::ostringstream desc;
      desc << "Downloaded " << progress.segments_downloaded << "/" << progress.total_segments
           << "  Parsed " << progress.segments_parsed << "/" << progress.total_segments
           << "  " << std::fixed << std::setprecision(0)
           << (static_cast<double>(progress.bytes_downloaded) / (1024.0 * 1024.0)) << " MB";
      detail = desc.str();
    }

    render(overall, detail);
  }

  void finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || !rendered_) {
      return;
    }
    render(1.0, "Ready");
    std::fputc('\n', stderr);
    std::fflush(stderr);
    rendered_ = false;
  }

  ~TerminalRouteProgress() {
    finish();
  }

private:
  void render(double progress, const std::string &detail) {
    const int percent = std::clamp(static_cast<int>(std::round(progress * 100.0)), 0, 100);
    if (percent == last_percent_ && detail == last_detail_) {
      return;
    }

    constexpr int kWidth = 20;
    const int filled = std::clamp(static_cast<int>(std::round(progress * kWidth)), 0, kWidth);
    const std::string bar = std::string(static_cast<size_t>(filled), '=') + std::string(static_cast<size_t>(kWidth - filled), ' ');
    std::ostringstream line;
    line << "\r[" << bar << "] " << percent << "% " << detail;

    const std::string text = line.str();
    std::fprintf(stderr, "%s", text.c_str());
    if (text.size() < last_line_width_) {
      std::fprintf(stderr, "%s", std::string(last_line_width_ - text.size(), ' ').c_str());
    }
    std::fflush(stderr);

    rendered_ = true;
    last_percent_ = percent;
    last_detail_ = detail;
    last_line_width_ = text.size();
  }

  bool enabled_ = false;
  bool rendered_ = false;
  int last_percent_ = -1;
  size_t last_line_width_ = 0;
  std::string last_detail_;
  std::mutex mutex_;
};

class AsyncRouteLoader {
public:
  explicit AsyncRouteLoader(bool enable_terminal_progress)
      : terminal_progress_(enable_terminal_progress) {}

  ~AsyncRouteLoader() {
    join();
  }

  void start(const std::string &route_name, const std::string &data_dir) {
    join();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      route_name_ = route_name;
      data_dir_ = data_dir;
      result_.reset();
      error_text_.clear();
    }
    active_.store(!route_name.empty());
    completed_.store(route_name.empty());
    success_.store(route_name.empty());
    total_segments_.store(0);
    segments_downloaded_.store(0);
    segments_parsed_.store(0);
    if (route_name.empty()) {
      return;
    }

    worker_ = std::thread([this]() {
      try {
        RouteData route_data = load_route_data(route_name_, data_dir_, [this](const RouteLoadProgress &progress) {
          total_segments_.store(progress.total_segments > 0 ? progress.total_segments : progress.segment_count);
          segments_downloaded_.store(progress.segments_downloaded);
          segments_parsed_.store(progress.segments_parsed);
          terminal_progress_.update(progress);
        });
        {
          std::lock_guard<std::mutex> lock(mutex_);
          result_ = std::make_unique<RouteData>(std::move(route_data));
          error_text_.clear();
        }
        success_.store(true);
      } catch (const std::exception &err) {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.reset();
        error_text_ = err.what();
        success_.store(false);
      }
      active_.store(false);
      completed_.store(true);
      terminal_progress_.finish();
    });
  }

  RouteLoadSnapshot snapshot() const {
    RouteLoadSnapshot snapshot;
    snapshot.active = active_.load();
    snapshot.total_segments = total_segments_.load();
    snapshot.segments_downloaded = segments_downloaded_.load();
    snapshot.segments_parsed = segments_parsed_.load();
    return snapshot;
  }

  bool consume(RouteData *route_data, std::string *error_text) {
    if (!completed_.load()) {
      return false;
    }
    join();
    std::lock_guard<std::mutex> lock(mutex_);
    completed_.store(false);
    if (result_) {
      *route_data = std::move(*result_);
      result_.reset();
      if (error_text != nullptr) {
        error_text->clear();
      }
      return true;
    }
    if (error_text != nullptr) {
      *error_text = error_text_;
    }
    return true;
  }

private:
  void join() {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  mutable std::mutex mutex_;
  std::thread worker_;
  std::unique_ptr<RouteData> result_;
  std::string route_name_;
  std::string data_dir_;
  std::string error_text_;
  std::atomic<bool> active_{false};
  std::atomic<bool> completed_{false};
  std::atomic<bool> success_{false};
  std::atomic<size_t> total_segments_{0};
  std::atomic<size_t> segments_downloaded_{0};
  std::atomic<size_t> segments_parsed_{0};
  TerminalRouteProgress terminal_progress_;
};

class SidebarCameraFeed {
public:
  SidebarCameraFeed() {
    worker_ = std::thread([this]() { worker_loop(); });
  }

  ~SidebarCameraFeed() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
    destroy_texture();
  }

  void set_route_data(const RouteData &route_data) {
    destroy_texture();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      route_index_ = route_data.road_camera;
      pending_request_.reset();
      pending_result_.reset();
      ++route_generation_;
      latest_request_serial_ = 0;
    }
    active_request_.reset();
    displayed_request_.reset();
    failed_request_.reset();
    frame_width_ = 0;
    frame_height_ = 0;
    cv_.notify_all();
  }

  void update(double tracker_time) {
    upload_pending_result();
    std::optional<DecodeRequest> request = request_for_time(tracker_time);
    if (!request.has_value()) {
      return;
    }
    if (same_request(active_request_, request->key) || same_request(displayed_request_, request->key) ||
        same_request(failed_request_, request->key)) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      request->serial = ++latest_request_serial_;
      request->generation = route_generation_;
      pending_request_ = request;
    }
    active_request_ = request->key;
    failed_request_.reset();
    cv_.notify_one();
  }

  void draw(float width, bool loading) {
    const float preview_width = std::max(1.0f, width);
    const float preview_height = preview_width * preview_aspect();
    if (texture_ != 0) {
      ImGui::Image(static_cast<ImTextureID>(texture_), ImVec2(preview_width, preview_height));
    } else {
      const ImVec2 top_left = ImGui::GetCursorScreenPos();
      const ImVec2 size(preview_width, preview_height);
      ImGui::InvisibleButton("##camera_feed_placeholder", size);

      ImDrawList *draw_list = ImGui::GetWindowDrawList();
      draw_list->AddRectFilled(top_left, ImVec2(top_left.x + size.x, top_left.y + size.y), IM_COL32(213, 217, 223, 255));
      draw_list->AddRect(top_left, ImVec2(top_left.x + size.x, top_left.y + size.y), IM_COL32(172, 178, 186, 255));

      const char *label = (loading || has_video_source()) ? "loading" : "no video";
      const ImVec2 text_size = ImGui::CalcTextSize(label);
      const ImVec2 text_pos(top_left.x + (size.x - text_size.x) * 0.5f,
                            top_left.y + (size.y - text_size.y) * 0.5f);
      draw_list->AddText(text_pos, IM_COL32(90, 96, 104, 255), label);
    }
    ImGui::Spacing();
  }

private:
  struct RequestKey {
    int segment = -1;
    int decode_index = -1;
  };

  struct DecodeRequest {
    RequestKey key;
    std::string path;
    uint64_t serial = 0;
    uint64_t generation = 0;
  };

  struct DecodeResult {
    RequestKey key;
    bool success = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
  };

  static constexpr float kDefaultAspect = 1208.0f / 1928.0f;

  static bool same_request(const std::optional<RequestKey> &lhs, const RequestKey &rhs) {
    return lhs.has_value() && lhs->segment == rhs.segment && lhs->decode_index == rhs.decode_index;
  }

  bool has_video_source() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !route_index_.entries.empty() && !route_index_.segment_files.empty();
  }

  float preview_aspect() const {
    if (frame_width_ > 0 && frame_height_ > 0) {
      return static_cast<float>(frame_height_) / static_cast<float>(frame_width_);
    }
    return kDefaultAspect;
  }

  std::optional<DecodeRequest> request_for_time(double tracker_time) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (route_index_.entries.empty()) {
      return std::nullopt;
    }

    auto it = std::lower_bound(route_index_.entries.begin(), route_index_.entries.end(), tracker_time,
                               [](const CameraFrameIndexEntry &entry, double tm) {
                                 return entry.timestamp < tm;
                               });
    if (it == route_index_.entries.end()) {
      it = std::prev(route_index_.entries.end());
    } else if (it != route_index_.entries.begin()) {
      const auto prev = std::prev(it);
      if (std::abs(prev->timestamp - tracker_time) <= std::abs(it->timestamp - tracker_time)) {
        it = prev;
      }
    }

    auto path_it = std::find_if(route_index_.segment_files.begin(), route_index_.segment_files.end(),
                                [&](const CameraSegmentFile &segment) {
                                  return segment.segment == it->segment && !segment.path.empty();
                                });
    if (path_it == route_index_.segment_files.end()) {
      return std::nullopt;
    }

    return DecodeRequest{
      .key = RequestKey{.segment = it->segment, .decode_index = it->decode_index},
      .path = path_it->path,
    };
  }

  void upload_pending_result() {
    std::optional<DecodeResult> result;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!pending_result_.has_value()) {
        return;
      }
      result = std::move(pending_result_);
      pending_result_.reset();
    }

    active_request_.reset();
    if (!result->success || result->rgba.empty() || result->width <= 0 || result->height <= 0) {
      failed_request_ = result->key;
      return;
    }

    if (texture_ == 0) {
      glGenTextures(1, &texture_);
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texture_width_ != result->width || texture_height_ != result->height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result->width, result->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, result->rgba.data());
      texture_width_ = result->width;
      texture_height_ = result->height;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, result->width, result->height, GL_RGBA, GL_UNSIGNED_BYTE, result->rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    frame_width_ = result->width;
    frame_height_ = result->height;
    displayed_request_ = result->key;
    failed_request_.reset();
  }

  void destroy_texture() {
    if (texture_ != 0 && glfwGetCurrentContext() != nullptr) {
      glDeleteTextures(1, &texture_);
    }
    texture_ = 0;
    texture_width_ = 0;
    texture_height_ = 0;
    frame_width_ = 0;
    frame_height_ = 0;
  }

  static bool ensure_decode_buffer(FrameReader *reader,
                                   VisionBuf *buffer,
                                   bool *allocated,
                                   int *width,
                                   int *height) {
    if (reader == nullptr || buffer == nullptr || allocated == nullptr || width == nullptr || height == nullptr) {
      return false;
    }
    if (*allocated && *width == reader->width && *height == reader->height) {
      return true;
    }
    if (*allocated) {
      buffer->free();
      *allocated = false;
    }

    const auto [stride, y_height, _uv_height, size] = get_nv12_info(reader->width, reader->height);
    buffer->allocate(size);
    buffer->init_yuv(reader->width, reader->height, stride, stride * y_height);
    *width = reader->width;
    *height = reader->height;
    *allocated = true;
    return true;
  }

  void publish_result(const DecodeRequest &request, DecodeResult result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_request_.has_value() || pending_request_->serial != request.serial ||
        pending_request_->generation != request.generation) {
      return;
    }
    pending_result_ = std::move(result);
  }

  void worker_loop() {
    std::unique_ptr<FrameReader> reader;
    std::string loaded_path;
    uint64_t loaded_generation = 0;
    VisionBuf buffer;
    bool buffer_allocated = false;
    int buffer_width = 0;
    int buffer_height = 0;
    uint64_t handled_serial = 0;

    while (true) {
      DecodeRequest request;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() {
          return stop_.load() || (pending_request_.has_value() && pending_request_->serial != handled_serial);
        });
        if (stop_.load()) {
          break;
        }
        request = *pending_request_;
        handled_serial = request.serial;
      }

      DecodeResult result{.key = request.key};
      if (!reader || loaded_path != request.path || loaded_generation != request.generation) {
        reader = std::make_unique<FrameReader>();
        loaded_path.clear();
        loaded_generation = 0;
        if (buffer_allocated) {
          buffer.free();
          buffer_allocated = false;
          buffer_width = 0;
          buffer_height = 0;
        }
        if (!reader->load(RoadCam, request.path, true, &stop_, true)) {
          publish_result(request, std::move(result));
          continue;
        }
        loaded_path = request.path;
        loaded_generation = request.generation;
      }

      if (!ensure_decode_buffer(reader.get(), &buffer, &buffer_allocated, &buffer_width, &buffer_height) ||
          !reader->get(request.key.decode_index, &buffer)) {
        publish_result(request, std::move(result));
        continue;
      }

      result.width = reader->width;
      result.height = reader->height;
      result.rgba.resize(static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4U, 0);
      libyuv::NV12ToABGR(buffer.y,
                         static_cast<int>(buffer.stride),
                         buffer.uv,
                         static_cast<int>(buffer.stride),
                         result.rgba.data(),
                         result.width * 4,
                         result.width,
                         result.height);
      result.success = true;
      publish_result(request, std::move(result));
    }

    if (buffer_allocated) {
      buffer.free();
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::atomic<bool> stop_{false};
  CameraFeedIndex route_index_;
  std::optional<DecodeRequest> pending_request_;
  std::optional<DecodeResult> pending_result_;
  uint64_t latest_request_serial_ = 0;
  uint64_t route_generation_ = 1;
  std::optional<RequestKey> active_request_;
  std::optional<RequestKey> displayed_request_;
  std::optional<RequestKey> failed_request_;
  GLuint texture_ = 0;
  int texture_width_ = 0;
  int texture_height_ = 0;
  int frame_width_ = 0;
  int frame_height_ = 0;
};

std::string shell_quote(const std::string &value) {
  std::ostringstream quoted;
  quoted << '\'';
  for (const char c : value) {
    if (c == '\'') {
      quoted << "'\\''";
    } else {
      quoted << c;
    }
  }
  quoted << '\'';
  return quoted.str();
}

fs::path repo_root() {
  std::array<char, 4096> buf = {};
  const ssize_t count = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (count <= 0) {
    throw std::runtime_error("Unable to resolve executable path");
  }
  return fs::path(std::string(buf.data(), static_cast<size_t>(count))).parent_path().parent_path().parent_path();
}

std::optional<fs::path> jetbrains_mono_font_path() {
  const char *home = std::getenv("HOME");
  std::vector<fs::path> candidates = {
    fs::path("/home/batman/.local/share/fonts/fonts/ttf/JetBrainsMono-Regular.ttf"),
    fs::path("/home/batman/.local/share/fonts/fonts/variable/JetBrainsMono[wght].ttf"),
    fs::path("/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf"),
  };
  if (home != nullptr) {
    candidates.insert(candidates.begin(), fs::path(home) / ".local/share/fonts/fonts/ttf/JetBrainsMono-Regular.ttf");
    candidates.insert(candidates.begin() + 1, fs::path(home) / ".local/share/fonts/fonts/variable/JetBrainsMono[wght].ttf");
  }
  for (const fs::path &candidate : candidates) {
    if (fs::exists(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::string layout_name_from_arg(const std::string &layout_arg) {
  const fs::path raw(layout_arg);
  if (raw.extension() == ".xml") {
    return raw.stem().string();
  }
  if (raw.filename() != raw) {
    return raw.filename().replace_extension("").string();
  }
  fs::path stem_path = raw;
  return stem_path.replace_extension("").string();
}

fs::path resolve_layout_path(const std::string &layout_arg) {
  const fs::path direct(layout_arg);
  if (fs::exists(direct)) {
    return fs::absolute(direct);
  }
  const fs::path candidate = repo_root() / "tools" / "plotjuggler" / "layouts" / (layout_name_from_arg(layout_arg) + ".xml");
  if (!fs::exists(candidate)) {
    throw std::runtime_error("Unknown layout: " + layout_arg);
  }
  return candidate;
}

fs::path layouts_dir() {
  return repo_root() / "tools" / "plotjuggler" / "layouts";
}

std::string sanitize_layout_stem(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  bool last_was_dash = false;
  for (const char raw : name) {
    const unsigned char c = static_cast<unsigned char>(raw);
    if (std::isalnum(c) != 0) {
      out.push_back(static_cast<char>(std::tolower(c)));
      last_was_dash = false;
    } else if (raw == '-' || raw == '_') {
      out.push_back(raw);
      last_was_dash = false;
    } else if (!last_was_dash && !out.empty()) {
      out.push_back('-');
      last_was_dash = true;
    }
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }
  return out.empty() ? "untitled" : out;
}

fs::path autosave_dir() {
  return layouts_dir() / ".jotpluggler_autosave";
}

fs::path autosave_path_for_layout(const fs::path &layout_path) {
  const std::string stem = layout_path.empty() ? "untitled" : layout_path.stem().string();
  return autosave_dir() / (sanitize_layout_stem(stem) + ".xml");
}

std::vector<std::string> available_layout_names() {
  std::vector<std::string> names;
  const fs::path root = layouts_dir();
  if (!fs::exists(root) || !fs::is_directory(root)) {
    return names;
  }
  for (const auto &entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".xml") {
      continue;
    }
    names.push_back(entry.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string xml_escape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&apos;"; break;
      default: escaped.push_back(c); break;
    }
  }
  return escaped;
}

std::string curve_color_hex(const std::array<uint8_t, 3> &color) {
  char buf[8] = {};
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", color[0], color[1], color[2]);
  return buf;
}

std::string format_xml_double(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

void ensure_parent_dir(const fs::path &path) {
  const fs::path parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

struct ProcessResult {
  int exit_code = 0;
  std::string stderr_text;
};

std::string read_fd_to_string(int fd) {
  std::string text;
  std::array<char, 4096> buffer = {};
  while (true) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count <= 0) {
      break;
    }
    text.append(buffer.data(), static_cast<size_t>(count));
  }
  return text;
}

ProcessResult run_process_capture_stderr(const std::vector<std::string> &args) {
  if (args.empty()) {
    throw std::runtime_error("Process arguments are empty");
  }

  int stderr_pipe[2] = {-1, -1};
  if (::pipe(stderr_pipe) != 0) {
    throw std::runtime_error("pipe() failed");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);
    throw std::runtime_error("fork() failed");
  }

  if (pid == 0) {
    ::dup2(stderr_pipe[1], STDERR_FILENO);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const std::string &arg : args) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv[0], argv.data());
    _exit(127);
  }

  ::close(stderr_pipe[1]);
  ProcessResult result;
  result.stderr_text = read_fd_to_string(stderr_pipe[0]);
  ::close(stderr_pipe[0]);

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("waitpid() failed");
  }
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  } else {
    result.exit_code = 1;
  }
  return result;
}

void run_or_throw(const std::string &command, const std::string &action) {
  const int ret = std::system(command.c_str());
  if (ret != 0) {
    throw std::runtime_error(action + " failed with exit code " + std::to_string(ret));
  }
}

bool reload_layout(AppSession *session, UiState *state, const std::string &layout_arg);
void reset_shared_range(UiState *state, const AppSession &session);
std::string curve_display_name(const Curve &curve);

ImVec4 color_rgb(int r, int g, int b, float alpha = 1.0f) {
  return ImVec4(static_cast<float>(r) / 255.0f,
                static_cast<float>(g) / 255.0f,
                static_cast<float>(b) / 255.0f,
                alpha);
}

ImVec4 color_rgb(const std::array<uint8_t, 3> &color, float alpha = 1.0f) {
  return color_rgb(color[0], color[1], color[2], alpha);
}

void configure_style() {
  ImGui::StyleColorsLight();
  ImPlot::StyleColorsLight();

  ImGuiIO &io = ImGui::GetIO();
  if (std::optional<fs::path> font_path = jetbrains_mono_font_path(); font_path.has_value()) {
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.RasterizerDensity = 1.0f;
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(font_path->c_str(), 16.75f, &font_cfg); font != nullptr) {
      io.FontDefault = font;
    }
  }

  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.FrameRounding = 2.0f;
  style.ScrollbarRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.TabRounding = 0.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.WindowPadding = ImVec2(8.0f, 8.0f);
  style.FramePadding = ImVec2(6.0f, 4.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  style.Colors[ImGuiCol_WindowBg] = color_rgb(250, 250, 251);
  style.Colors[ImGuiCol_ChildBg] = color_rgb(255, 255, 255);
  style.Colors[ImGuiCol_Border] = color_rgb(194, 198, 204);
  style.Colors[ImGuiCol_TitleBg] = color_rgb(252, 252, 253);
  style.Colors[ImGuiCol_TitleBgActive] = color_rgb(252, 252, 253);
  style.Colors[ImGuiCol_TitleBgCollapsed] = color_rgb(252, 252, 253);
  style.Colors[ImGuiCol_Text] = color_rgb(74, 80, 88);
  style.Colors[ImGuiCol_TextDisabled] = color_rgb(108, 118, 128);
  style.Colors[ImGuiCol_Button] = color_rgb(255, 255, 255);
  style.Colors[ImGuiCol_ButtonHovered] = color_rgb(246, 248, 250);
  style.Colors[ImGuiCol_ButtonActive] = color_rgb(238, 240, 244);
  style.Colors[ImGuiCol_FrameBg] = color_rgb(255, 255, 255);
  style.Colors[ImGuiCol_FrameBgHovered] = color_rgb(248, 249, 251);
  style.Colors[ImGuiCol_FrameBgActive] = color_rgb(241, 244, 248);
  style.Colors[ImGuiCol_Header] = color_rgb(243, 245, 248);
  style.Colors[ImGuiCol_HeaderHovered] = color_rgb(237, 240, 244);
  style.Colors[ImGuiCol_HeaderActive] = color_rgb(232, 236, 240);
  style.Colors[ImGuiCol_PopupBg] = color_rgb(248, 249, 251);
  style.Colors[ImGuiCol_MenuBarBg] = color_rgb(232, 236, 241);
  style.Colors[ImGuiCol_Separator] = color_rgb(194, 198, 204);
  style.Colors[ImGuiCol_ScrollbarBg] = color_rgb(240, 242, 245);
  style.Colors[ImGuiCol_ScrollbarGrab] = color_rgb(202, 207, 214);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = color_rgb(180, 186, 194);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = color_rgb(164, 171, 180);
  style.Colors[ImGuiCol_Tab] = color_rgb(219, 224, 230);
  style.Colors[ImGuiCol_TabHovered] = color_rgb(232, 236, 241);
  style.Colors[ImGuiCol_TabSelected] = color_rgb(250, 251, 253);
  style.Colors[ImGuiCol_TabSelectedOverline] = color_rgb(92, 109, 136);
  style.Colors[ImGuiCol_TabDimmed] = color_rgb(213, 219, 226);
  style.Colors[ImGuiCol_TabDimmedSelected] = color_rgb(244, 247, 249);
  style.Colors[ImGuiCol_TabDimmedSelectedOverline] = color_rgb(92, 109, 136);
  style.Colors[ImGuiCol_DockingEmptyBg] = color_rgb(244, 246, 248);
  style.Colors[ImGuiCol_DockingPreview] = color_rgb(69, 115, 184, 0.22f);

  ImPlotStyle &plot_style = ImPlot::GetStyle();
  plot_style.PlotBorderSize = 1.0f;
  plot_style.MinorAlpha = 0.65f;
  plot_style.LegendPadding = ImVec2(6.0f, 6.0f);
  plot_style.LegendInnerPadding = ImVec2(6.0f, 4.0f);
  plot_style.LegendSpacing = ImVec2(8.0f, 3.0f);
  plot_style.PlotPadding = ImVec2(4.0f, 6.0f);

  ImPlot::MapInputDefault();
  ImPlotInputMap &input_map = ImPlot::GetInputMap();
  input_map.Pan = ImGuiMouseButton_Right;
  input_map.PanMod = ImGuiMod_None;
  input_map.Select = ImGuiMouseButton_Left;
  input_map.SelectCancel = ImGuiMouseButton_Right;
  input_map.SelectMod = ImGuiMod_None;
}

UiMetrics compute_ui_metrics(const ImVec2 &size, float top_offset, float sidebar_width) {
  UiMetrics ui;
  ui.width = size.x;
  ui.height = size.y;
  ui.top_offset = top_offset;
  ui.sidebar_width = std::clamp(sidebar_width, kSidebarMinWidth, std::min(kSidebarMaxWidth, size.x * 0.6f));
  ui.content_x = ui.sidebar_width + kContentGap;
  ui.content_y = top_offset;
  ui.content_w = std::max(1.0f, size.x - ui.content_x - kContentRightPadding);
  ui.content_h = std::max(1.0f, size.y - ui.content_y - kStatusBarHeight);
  ui.status_bar_y = std::max(0.0f, size.y - kStatusBarHeight);
  return ui;
}

template <size_t N>
void copy_to_buffer(const std::string &value, std::array<char, N> *buffer) {
  buffer->fill('\0');
  if constexpr (N > 0) {
    const size_t count = std::min(value.size(), N - 1);
    std::copy_n(value.data(), count, buffer->data());
    (*buffer)[count] = '\0';
  }
}

template <size_t N>
std::string string_from_buffer(const std::array<char, N> &buffer) {
  return std::string(buffer.data());
}

int input_text_resize_callback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackResize || data->UserData == nullptr) {
    return 0;
  }
  auto *text = static_cast<std::string *>(data->UserData);
  text->resize(static_cast<size_t>(data->BufTextLen));
  data->Buf = text->data();
  return 0;
}

bool input_text_string(const char *label,
                       std::string *text,
                       ImGuiInputTextFlags flags = 0) {
  flags |= ImGuiInputTextFlags_CallbackResize;
  if (text->capacity() == 0) {
    text->reserve(256);
  }
  return ImGui::InputText(label,
                          text->data(),
                          text->capacity() + 1,
                          flags,
                          input_text_resize_callback,
                          text);
}

bool input_text_multiline_string(const char *label,
                                 std::string *text,
                                 const ImVec2 &size = ImVec2(0.0f, 0.0f),
                                 ImGuiInputTextFlags flags = 0) {
  flags |= ImGuiInputTextFlags_CallbackResize;
  if (text->capacity() == 0) {
    text->reserve(1024);
  }
  return ImGui::InputTextMultiline(label,
                                   text->data(),
                                   text->capacity() + 1,
                                   size,
                                   flags,
                                   input_text_resize_callback,
                                   text);
}

std::string json_escape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7] = {};
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          escaped += buf;
        } else {
          escaped.push_back(c);
        }
        break;
    }
  }
  return escaped;
}

fs::path executable_dir() {
  std::array<char, 4096> buf = {};
  const ssize_t length = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (length <= 0) {
    throw std::runtime_error("Failed to resolve executable path");
  }
  buf[static_cast<size_t>(length)] = '\0';
  return fs::path(buf.data()).parent_path();
}

fs::path math_eval_script_path() {
  return executable_dir() / "math_eval.py";
}

void sync_ui_state(UiState *state, const SketchLayout &layout) {
  const bool initializing = state->tabs.empty();
  state->tabs.resize(layout.tabs.size());
  if (layout.tabs.empty()) {
    state->active_tab_index = 0;
    state->requested_tab_index = -1;
    return;
  }
  if (initializing) {
    state->active_tab_index = std::clamp(layout.current_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
    state->requested_tab_index = state->active_tab_index;
  }
  state->active_tab_index = std::clamp(state->active_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  for (size_t i = 0; i < layout.tabs.size(); ++i) {
    if (state->tabs[i].runtime_id == 0) {
      state->tabs[i].runtime_id = state->next_tab_runtime_id++;
    }
    const int pane_count = static_cast<int>(layout.tabs[i].panes.size());
    state->tabs[i].active_pane_index = pane_count <= 0
      ? 0
      : std::clamp(state->tabs[i].active_pane_index, 0, pane_count - 1);
  }
}

void sync_route_buffers(UiState *state, const AppSession &session) {
  copy_to_buffer(session.route_name, &state->route_buffer);
  copy_to_buffer(session.data_dir, &state->data_dir_buffer);
}

fs::path default_layout_save_path(const AppSession &session) {
  return session.layout_path.empty() ? layouts_dir() / "new-layout.xml" : session.layout_path;
}

void sync_layout_buffers(UiState *state, const AppSession &session) {
  copy_to_buffer(session.layout_path.empty() ? std::string() : session.layout_path.string(), &state->load_layout_buffer);
  copy_to_buffer(default_layout_save_path(session).string(), &state->save_layout_buffer);
}

const WorkspaceTab *active_tab(const SketchLayout &layout, const UiState &state) {
  if (layout.tabs.empty()) {
    return nullptr;
  }
  const int index = std::clamp(state.active_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  return &layout.tabs[static_cast<size_t>(index)];
}

WorkspaceTab *active_tab(SketchLayout *layout, const UiState &state) {
  if (layout->tabs.empty()) {
    return nullptr;
  }
  const int index = std::clamp(state.active_tab_index, 0, static_cast<int>(layout->tabs.size()) - 1);
  return &layout->tabs[static_cast<size_t>(index)];
}

TabUiState *active_tab_state(UiState *state) {
  if (state->tabs.empty()) {
    return nullptr;
  }
  const int index = std::clamp(state->active_tab_index, 0, static_cast<int>(state->tabs.size()) - 1);
  return &state->tabs[static_cast<size_t>(index)];
}

std::string pane_window_name(int tab_runtime_id, int pane_index, const Pane &pane) {
  std::string title = pane.title.empty() ? kUntitledPaneTitle : pane.title;
  return title + "##tab" + std::to_string(tab_runtime_id) + "_pane" + std::to_string(pane_index);
}

std::string tab_item_label(const WorkspaceTab &tab, int tab_runtime_id) {
  return tab.tab_name + "##workspace_tab_" + std::to_string(tab_runtime_id);
}

void request_tab_selection(UiState *state, int tab_index) {
  state->active_tab_index = tab_index;
  state->requested_tab_index = tab_index;
}

void begin_rename_tab(const SketchLayout &layout, UiState *state, int tab_index) {
  if (tab_index < 0 || tab_index >= static_cast<int>(layout.tabs.size())) {
    return;
  }
  copy_to_buffer(layout.tabs[static_cast<size_t>(tab_index)].tab_name, &state->rename_tab_buffer);
  state->rename_tab_index = tab_index;
  state->focus_rename_tab_input = true;
  request_tab_selection(state, tab_index);
}

void cancel_rename_tab(UiState *state) {
  state->rename_tab_index = -1;
  state->focus_rename_tab_input = false;
}

ImGuiID dockspace_id_for_tab(int tab_runtime_id) {
  return ImHashStr(("jotpluggler_dockspace_" + std::to_string(tab_runtime_id)).c_str());
}

enum class PaneDropZone {
  Center,
  Left,
  Right,
  Top,
  Bottom,
};

enum class PaneMenuActionKind {
  None,
  OpenCustomSeries,
  SplitLeft,
  SplitRight,
  SplitTop,
  SplitBottom,
  ResetView,
  Clear,
  Close,
};

struct PaneMenuAction {
  PaneMenuActionKind kind = PaneMenuActionKind::None;
  int pane_index = -1;
};

struct PaneCurveDragPayload {
  int tab_index = -1;
  int pane_index = -1;
  int curve_index = -1;
};

struct PaneDropAction {
  PaneDropZone zone = PaneDropZone::Center;
  int target_pane_index = -1;
  bool from_browser = false;
  std::string browser_path;
  PaneCurveDragPayload curve_ref;
};

std::string curve_display_name(const Curve &curve);
bool add_curve_to_active_pane(AppSession *session, UiState *state, const std::string &path);
bool curve_has_samples(const AppSession &session, const Curve &curve);
void refresh_all_custom_curves(AppSession *session, UiState *state);

bool curve_has_local_samples(const Curve &curve) {
  return curve.xs.size() > 1 && curve.xs.size() == curve.ys.size();
}

void mark_all_docks_dirty(UiState *state) {
  for (TabUiState &tab_state : state->tabs) {
    tab_state.dock_needs_build = true;
  }
}

void mark_tab_dock_dirty(UiState *state, int tab_index) {
  if (tab_index >= 0 && tab_index < static_cast<int>(state->tabs.size())) {
    state->tabs[static_cast<size_t>(tab_index)].dock_needs_build = true;
  }
}

void normalize_split_node(WorkspaceNode *node) {
  if (node->is_pane) {
    return;
  }
  for (WorkspaceNode &child : node->children) {
    normalize_split_node(&child);
  }
  if (node->children.empty()) {
    return;
  }
  if (node->children.size() == 1) {
    *node = node->children.front();
    return;
  }
  if (node->sizes.size() != node->children.size()) {
    node->sizes.assign(node->children.size(), 1.0f / static_cast<float>(node->children.size()));
    return;
  }
  float total = 0.0f;
  for (float &size : node->sizes) {
    size = std::max(size, 0.0f);
    total += size;
  }
  if (total <= 0.0f) {
    node->sizes.assign(node->children.size(), 1.0f / static_cast<float>(node->children.size()));
    return;
  }
  for (float &size : node->sizes) {
    size /= total;
  }
}

void decrement_pane_indices(WorkspaceNode *node, int removed_index) {
  if (node->is_pane) {
    if (node->pane_index > removed_index) {
      node->pane_index -= 1;
    }
    return;
  }
  for (WorkspaceNode &child : node->children) {
    decrement_pane_indices(&child, removed_index);
  }
}

bool remove_pane_node(WorkspaceNode *node, int pane_index) {
  if (node->is_pane) {
    return node->pane_index == pane_index;
  }

  for (size_t i = 0; i < node->children.size();) {
    if (remove_pane_node(&node->children[i], pane_index)) {
      node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(i));
      if (i < node->sizes.size()) {
        node->sizes.erase(node->sizes.begin() + static_cast<std::ptrdiff_t>(i));
      }
    } else {
      ++i;
    }
  }

  normalize_split_node(node);
  return !node->is_pane && node->children.empty();
}

bool split_pane_node(WorkspaceNode *node, int target_pane_index, SplitOrientation orientation,
                     bool new_before, int new_pane_index) {
  if (node->is_pane) {
    if (node->pane_index != target_pane_index) {
      return false;
    }
    WorkspaceNode existing_pane;
    existing_pane.is_pane = true;
    existing_pane.pane_index = target_pane_index;

    WorkspaceNode new_pane;
    new_pane.is_pane = true;
    new_pane.pane_index = new_pane_index;

    node->is_pane = false;
    node->pane_index = -1;
    node->orientation = orientation;
    node->sizes = {0.5f, 0.5f};
    node->children.clear();
    if (new_before) {
      node->children.push_back(std::move(new_pane));
      node->children.push_back(std::move(existing_pane));
    } else {
      node->children.push_back(std::move(existing_pane));
      node->children.push_back(std::move(new_pane));
    }
    return true;
  }

  for (WorkspaceNode &child : node->children) {
    if (split_pane_node(&child, target_pane_index, orientation, new_before, new_pane_index)) {
      return true;
    }
  }
  return false;
}

Pane make_empty_pane(const std::string &title = kUntitledPaneTitle) {
  Pane pane;
  pane.title = title;
  return pane;
}

WorkspaceTab make_empty_tab(const std::string &tab_name) {
  WorkspaceTab tab;
  tab.tab_name = tab_name;
  tab.panes.push_back(make_empty_pane());
  tab.root.is_pane = true;
  tab.root.pane_index = 0;
  return tab;
}

SketchLayout make_empty_layout() {
  SketchLayout layout;
  layout.tabs.push_back(make_empty_tab("tab1"));
  layout.current_tab_index = 0;
  layout.roots.push_back("layout");
  return layout;
}

bool tab_name_exists(const SketchLayout &layout, const std::string &name) {
  return std::any_of(layout.tabs.begin(), layout.tabs.end(), [&](const WorkspaceTab &tab) {
    return tab.tab_name == name;
  });
}

std::string next_tab_name(const SketchLayout &layout, const std::string &base_name) {
  if (base_name == "tab" || base_name == "tab1") {
    int max_suffix = 0;
    for (const WorkspaceTab &tab : layout.tabs) {
      if (tab.tab_name.size() > 3 && tab.tab_name.rfind("tab", 0) == 0) {
        const std::string suffix = tab.tab_name.substr(3);
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit)) {
          max_suffix = std::max(max_suffix, std::stoi(suffix));
        }
      }
    }
    return "tab" + std::to_string(std::max(1, max_suffix + 1));
  }
  std::string base = base_name.empty() ? "tab" : base_name;
  if (!tab_name_exists(layout, base)) {
    return base;
  }
  for (int i = 2; i < 1000; ++i) {
    const std::string candidate = base + " " + std::to_string(i);
    if (!tab_name_exists(layout, candidate)) {
      return candidate;
    }
  }
  return base + " copy";
}

void write_indent(std::ostream &out, int spaces) {
  out << std::string(static_cast<size_t>(std::max(spaces, 0)), ' ');
}

void write_curve_xml(std::ostream &out, const Curve &curve, int indent) {
  if (curve.runtime_only) {
    return;
  }
  write_indent(out, indent);
  out << "<curve name=\"" << xml_escape(curve.name) << "\" color=\"" << curve_color_hex(curve.color) << "\"";
  const bool has_transform = curve.derivative
    || std::abs(curve.value_scale - 1.0) > 1.0e-9
    || std::abs(curve.value_offset) > 1.0e-9;
  if (!has_transform) {
    out << "/>\n";
    return;
  }

  out << ">\n";
  if (curve.derivative) {
    write_indent(out, indent + 1);
    out << "<transform name=\"Derivative\"/>\n";
  }
  if (std::abs(curve.value_scale - 1.0) > 1.0e-9 || std::abs(curve.value_offset) > 1.0e-9) {
    write_indent(out, indent + 1);
    out << "<transform name=\"Scale/Offset\">\n";
    write_indent(out, indent + 2);
    out << "<options value_scale=\"" << format_xml_double(curve.value_scale)
        << "\" value_offset=\"" << format_xml_double(curve.value_offset) << "\"/>\n";
    write_indent(out, indent + 1);
    out << "</transform>\n";
  }
  write_indent(out, indent);
  out << "</curve>\n";
}

void write_workspace_node_xml(std::ostream &out, const WorkspaceNode &node, const WorkspaceTab &tab, int indent) {
  if (node.is_pane) {
    if (node.pane_index < 0 || node.pane_index >= static_cast<int>(tab.panes.size())) {
      return;
    }
    const Pane &pane = tab.panes[static_cast<size_t>(node.pane_index)];
    write_indent(out, indent);
    out << "<DockArea name=\"" << xml_escape(pane.title.empty() ? std::string(kUntitledPaneTitle) : pane.title) << "\">\n";
    write_indent(out, indent + 1);
    out << "<plot flip_y=\"false\" flip_x=\"false\" mode=\"TimeSeries\" style=\"Lines\">\n";
    write_indent(out, indent + 2);
    out << "<range left=\"" << format_xml_double(pane.range.left)
        << "\" top=\"" << format_xml_double(pane.range.top)
        << "\" bottom=\"" << format_xml_double(pane.range.bottom)
        << "\" right=\"" << format_xml_double(pane.range.right) << "\"/>\n";
    write_indent(out, indent + 2);
    out << "<limitY";
    if (pane.range.has_y_limit_min) {
      out << " min=\"" << format_xml_double(pane.range.y_limit_min) << "\"";
    }
    if (pane.range.has_y_limit_max) {
      out << " max=\"" << format_xml_double(pane.range.y_limit_max) << "\"";
    }
    out << "/>\n";
    for (const Curve &curve : pane.curves) {
      write_curve_xml(out, curve, indent + 2);
    }
    write_indent(out, indent + 1);
    out << "</plot>\n";
    write_indent(out, indent);
    out << "</DockArea>\n";
    return;
  }

  if (node.children.empty()) {
    return;
  }
  const char orientation = node.orientation == SplitOrientation::Horizontal ? '|' : '-';
  write_indent(out, indent);
  out << "<DockSplitter orientation=\"" << orientation << "\" sizes=\"";
  for (size_t i = 0; i < node.children.size(); ++i) {
    if (i != 0) {
      out << ';';
    }
    const float size = i < node.sizes.size() ? node.sizes[i] : 1.0f / static_cast<float>(node.children.size());
    out << format_xml_double(size);
  }
  out << "\" count=\"" << node.children.size() << "\">\n";
  for (const WorkspaceNode &child : node.children) {
    write_workspace_node_xml(out, child, tab, indent + 1);
  }
  write_indent(out, indent);
  out << "</DockSplitter>\n";
}

void save_layout_xml(const SketchLayout &layout, const fs::path &path) {
  ensure_parent_dir(path);
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to open layout for writing: " + path.string());
  }

  out << "<?xml version='1.0' encoding='UTF-8'?>\n";
  out << "<root>\n";
  out << " <tabbed_widget name=\"Main Window\" parent=\"main_window\">\n";
  for (const WorkspaceTab &tab : layout.tabs) {
    out << "  <Tab tab_name=\"" << xml_escape(tab.tab_name) << "\" containers=\"1\">\n";
    out << "   <Container>\n";
    write_workspace_node_xml(out, tab.root, tab, 4);
    out << "   </Container>\n";
    out << "  </Tab>\n";
  }
  out << "  <currentTabIndex index=\"" << std::clamp(layout.current_tab_index, 0, std::max(0, static_cast<int>(layout.tabs.size()) - 1)) << "\"/>\n";
  out << " </tabbed_widget>\n";
  out << " <use_relative_time_offset enabled=\"1\"/>\n";
  out << " <Plugins>\n";
  out << "  <plugin ID=\"DataLoad Rlog\"/>\n";
  out << "  <plugin ID=\"Cereal Subscriber\"/>\n";
  out << " </Plugins>\n";
  out << " <customMathEquations/>\n";
  out << " <snippets/>\n";
  out << "</root>\n";
}

void clear_layout_autosave(const AppSession &session) {
  if (!session.autosave_path.empty() && fs::exists(session.autosave_path)) {
    fs::remove(session.autosave_path);
  }
}

bool autosave_layout(AppSession *session, UiState *state) {
  try {
    if (session->autosave_path.empty()) {
      session->autosave_path = autosave_path_for_layout(session->layout_path);
    }
    session->layout.current_tab_index = state->active_tab_index;
    save_layout_xml(session->layout, session->autosave_path);
    state->layout_dirty = true;
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to save layout draft";
    return false;
  }
}

bool mark_layout_dirty(AppSession *session, UiState *state) {
  return autosave_layout(session, state);
}

std::array<uint8_t, 3> next_curve_color(const Pane &pane) {
  static constexpr std::array<std::array<uint8_t, 3>, 10> kPalette = {{
    {35, 107, 180},
    {220, 82, 52},
    {67, 160, 71},
    {243, 156, 18},
    {123, 97, 255},
    {0, 150, 136},
    {214, 48, 49},
    {52, 73, 94},
    {197, 90, 17},
    {96, 125, 139},
  }};
  return kPalette[pane.curves.size() % kPalette.size()];
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool path_matches_filter(const std::string &path, const std::string &filter) {
  if (filter.empty()) {
    return true;
  }
  return lowercase(path).find(lowercase(filter)) != std::string::npos;
}

void insert_browser_path(std::vector<BrowserNode> *nodes, const std::string &path) {
  size_t start = 0;
  while (start < path.size() && path[start] == '/') {
    ++start;
  }
  std::vector<std::string> parts;
  while (start < path.size()) {
    const size_t end = path.find('/', start);
    parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  if (parts.empty()) {
    return;
  }

  std::vector<BrowserNode> *current_nodes = nodes;
  std::string current_path;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!current_path.empty()) {
      current_path += "/";
    }
    current_path += parts[i];
    auto it = std::find_if(current_nodes->begin(), current_nodes->end(),
                           [&](const BrowserNode &node) { return node.label == parts[i]; });
    if (it == current_nodes->end()) {
      current_nodes->push_back(BrowserNode{.label = parts[i]});
      it = std::prev(current_nodes->end());
    }
    if (i + 1 == parts.size()) {
      it->full_path = "/" + current_path;
    }
    current_nodes = &it->children;
  }
}

void sort_browser_nodes(std::vector<BrowserNode> *nodes) {
  std::sort(nodes->begin(), nodes->end(), [](const BrowserNode &a, const BrowserNode &b) {
    if (a.children.empty() != b.children.empty()) {
      return !a.children.empty();
    }
    return a.label < b.label;
  });
  for (BrowserNode &node : *nodes) {
    sort_browser_nodes(&node.children);
  }
}

std::vector<BrowserNode> build_browser_tree(const std::vector<std::string> &paths) {
  std::vector<BrowserNode> nodes;
  for (const std::string &path : paths) {
    insert_browser_path(&nodes, path);
  }
  sort_browser_nodes(&nodes);
  return nodes;
}

const RouteSeries *find_route_series(const AppSession &session, const std::string &path);
bool browser_node_matches(const BrowserNode &node, const std::string &filter);

bool is_deprecated_browser_path(const std::string &path) {
  return path.find("DEPRECATED") != std::string::npos;
}

std::vector<std::string> visible_browser_paths(const RouteData &route_data, bool show_deprecated_fields) {
  if (show_deprecated_fields) {
    return route_data.paths;
  }
  std::vector<std::string> filtered;
  filtered.reserve(route_data.paths.size());
  for (const std::string &path : route_data.paths) {
    if (!is_deprecated_browser_path(path)) {
      filtered.push_back(path);
    }
  }
  return filtered;
}

bool browser_selection_contains(const UiState &state, std::string_view path) {
  return std::find(state.selected_browser_paths.begin(), state.selected_browser_paths.end(), path)
    != state.selected_browser_paths.end();
}

void set_browser_selection_single(UiState *state, const std::string &path) {
  state->selected_browser_paths = {path};
  state->selected_browser_path = path;
  state->browser_selection_anchor = path;
}

void toggle_browser_selection(UiState *state, const std::string &path) {
  auto it = std::find(state->selected_browser_paths.begin(), state->selected_browser_paths.end(), path);
  if (it == state->selected_browser_paths.end()) {
    state->selected_browser_paths.push_back(path);
  } else {
    state->selected_browser_paths.erase(it);
  }
  state->selected_browser_path = path;
  state->browser_selection_anchor = path;
  if (state->selected_browser_paths.empty()) {
    state->selected_browser_path.clear();
  }
}

void select_browser_range(UiState *state, const std::vector<std::string> &visible_paths, const std::string &clicked_path) {
  if (visible_paths.empty()) {
    set_browser_selection_single(state, clicked_path);
    return;
  }

  const std::string anchor = state->browser_selection_anchor.empty() ? clicked_path : state->browser_selection_anchor;
  const auto anchor_it = std::find(visible_paths.begin(), visible_paths.end(), anchor);
  const auto clicked_it = std::find(visible_paths.begin(), visible_paths.end(), clicked_path);
  if (clicked_it == visible_paths.end()) {
    return;
  }
  if (anchor_it == visible_paths.end()) {
    set_browser_selection_single(state, clicked_path);
    return;
  }

  const auto [begin_it, end_it] = std::minmax(anchor_it, clicked_it);
  std::vector<std::string> selected;
  selected.reserve(static_cast<size_t>(std::distance(begin_it, end_it)) + 1);
  for (auto it = begin_it; it != end_it + 1; ++it) {
    selected.push_back(*it);
  }
  state->selected_browser_paths = std::move(selected);
  state->selected_browser_path = clicked_path;
}

void prune_browser_selection(UiState *state, const std::vector<std::string> &visible_paths) {
  auto is_visible = [&](const std::string &path) {
    return std::find(visible_paths.begin(), visible_paths.end(), path) != visible_paths.end();
  };

  state->selected_browser_paths.erase(
    std::remove_if(state->selected_browser_paths.begin(), state->selected_browser_paths.end(),
                   [&](const std::string &path) { return !is_visible(path); }),
    state->selected_browser_paths.end());

  if (!state->selected_browser_path.empty() && !is_visible(state->selected_browser_path)) {
    state->selected_browser_path.clear();
  }
  if (!state->browser_selection_anchor.empty() && !is_visible(state->browser_selection_anchor)) {
    state->browser_selection_anchor.clear();
  }
  if (state->selected_browser_paths.empty()) {
    state->selected_browser_path.clear();
  } else if (state->selected_browser_path.empty()) {
    state->selected_browser_path = state->selected_browser_paths.back();
  }
}

void collect_visible_leaf_paths(const BrowserNode &node,
                                const std::string &filter,
                                std::vector<std::string> *out) {
  if (!browser_node_matches(node, filter)) {
    return;
  }
  if (node.children.empty()) {
    if (!node.full_path.empty()) {
      out->push_back(node.full_path);
    }
    return;
  }
  for (const BrowserNode &child : node.children) {
    collect_visible_leaf_paths(child, filter, out);
  }
}

void rebuild_browser_nodes(AppSession *session, UiState *state) {
  const std::vector<std::string> paths = visible_browser_paths(session->route_data, state->show_deprecated_fields);
  session->browser_nodes = build_browser_tree(paths);
  prune_browser_selection(state, paths);
}

void rebuild_route_index(AppSession *session) {
  session->series_by_path.clear();
  for (const RouteSeries &series : session->route_data.series) {
    session->series_by_path.emplace(series.path, &series);
  }
}

void apply_route_data(AppSession *session, UiState *state, RouteData route_data) {
  session->route_data = std::move(route_data);
  rebuild_route_index(session);
  rebuild_browser_nodes(session, state);
  refresh_all_custom_curves(session, state);
  if (session->camera_feed) {
    session->camera_feed->set_route_data(session->route_data);
  }
  state->has_shared_range = false;
  state->has_tracker_time = false;
  reset_shared_range(state, *session);
}

const RouteSeries *find_route_series(const AppSession &session, const std::string &path) {
  auto it = session.series_by_path.find(path);
  return it == session.series_by_path.end() ? nullptr : it->second;
}

const std::array<CustomSeriesTemplate, 4> &custom_series_templates() {
  static constexpr std::array<CustomSeriesTemplate, 4> kTemplates = {{
    {
      .name = "Derivative",
      .globals_code = "",
      .function_code = "return np.gradient(value, time)",
      .preview_text = "return np.gradient(value, time)",
    },
    {
      .name = "Difference",
      .globals_code = "",
      .function_code = "return value - v1",
      .preview_text = "Requires one additional source timeseries.\n\nreturn value - v1",
    },
    {
      .name = "Smoothing",
      .globals_code = "window = 20\nweights = np.ones(window) / window",
      .function_code = "return np.convolve(value, weights, mode='same')",
      .preview_text = "window = 20\nweights = np.ones(window) / window\n\nreturn np.convolve(value, weights, mode='same')",
    },
    {
      .name = "Integral",
      .globals_code = "",
      .function_code = "dt = np.mean(np.diff(time))\nreturn np.cumsum(value) * dt",
      .preview_text = "dt = np.mean(np.diff(time))\nreturn np.cumsum(value) * dt",
    },
  }};
  return kTemplates;
}

void reset_custom_series_editor(CustomSeriesEditorState *editor) {
  *editor = CustomSeriesEditorState{};
}

void open_custom_series_editor(UiState *state, const std::string &preferred_source = {}) {
  CustomSeriesEditorState &editor = state->custom_series;
  if (!editor.open && editor.name.empty() && editor.linked_source.empty() && editor.function_code == "return value") {
    editor.focus_name = true;
  }
  if (editor.linked_source.empty() && !preferred_source.empty()) {
    editor.linked_source = preferred_source;
  }
  editor.open = true;
  editor.request_select = true;
}

bool add_additional_source(CustomSeriesEditorState *editor, const std::string &path) {
  if (path.empty() || path == editor->linked_source) {
    return false;
  }
  if (std::find(editor->additional_sources.begin(), editor->additional_sources.end(), path) != editor->additional_sources.end()) {
    return false;
  }
  editor->additional_sources.push_back(path);
  return true;
}

std::string trim_copy(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string next_custom_curve_name(const Pane &pane) {
  std::set<std::string> used;
  for (const Curve &curve : pane.curves) {
    if (!curve.label.empty()) {
      used.insert(curve.label);
    }
    if (!curve.name.empty()) {
      used.insert(curve.name);
    }
  }
  for (int i = 1; i < 1000; ++i) {
    const std::string candidate = "series" + std::to_string(i);
    if (used.find(candidate) == used.end()) {
      return candidate;
    }
  }
  return "series";
}

std::string preferred_custom_series_source(const Pane &pane) {
  for (const Curve &curve : pane.curves) {
    if (!curve.name.empty() && curve.name.front() == '/') {
      return curve.name;
    }
    if (curve.custom_python.has_value() && !curve.custom_python->linked_source.empty()) {
      return curve.custom_python->linked_source;
    }
  }
  return {};
}

void write_binary_vector(const fs::path &path, const std::vector<double> &values) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("Failed to open " + path.string() + " for writing");
  }
  if (!values.empty()) {
    out.write(reinterpret_cast<const char *>(values.data()),
              static_cast<std::streamsize>(values.size() * sizeof(double)));
  }
  if (!out) {
    throw std::runtime_error("Failed to write " + path.string());
  }
}

std::vector<double> read_binary_vector(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Failed to open " + path.string());
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  in.seekg(0, std::ios::beg);
  if (size < 0 || size % static_cast<std::streamoff>(sizeof(double)) != 0) {
    throw std::runtime_error("Invalid binary series file: " + path.string());
  }
  std::vector<double> values(static_cast<size_t>(size) / sizeof(double));
  if (!values.empty()) {
    in.read(reinterpret_cast<char *>(values.data()), size);
  }
  if (!in) {
    throw std::runtime_error("Failed to read " + path.string());
  }
  return values;
}

void write_text_file(const fs::path &path, std::string_view text) {
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to open " + path.string() + " for writing");
  }
  out << text;
}

fs::path create_custom_series_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir = fs::temp_directory_path() / ("jotpluggler_math_" + std::to_string(::getpid()) + "_" + std::to_string(stamp));
  fs::create_directories(dir);
  return dir;
}

std::set<std::string> collect_custom_series_paths(const CustomPythonSeries &spec,
                                                  std::string_view globals_code,
                                                  std::string_view function_code) {
  std::set<std::string> paths;
  if (!spec.linked_source.empty()) {
    paths.insert(spec.linked_source);
  }
  paths.insert(spec.additional_sources.begin(), spec.additional_sources.end());

  static const std::regex kPathRegex(R"([tv]\(\s*["']([^"']+)["']\s*\))");
  const auto collect_from = [&](std::string_view code) {
    std::string owned(code);
    for (std::sregex_iterator it(owned.begin(), owned.end(), kPathRegex), end; it != end; ++it) {
      paths.insert((*it)[1].str());
    }
  };
  collect_from(globals_code);
  collect_from(function_code);
  return paths;
}

PythonEvalResult evaluate_custom_python_series(const AppSession &session,
                                               const CustomPythonSeries &spec) {
  const std::set<std::string> referenced_paths =
    collect_custom_series_paths(spec, spec.globals_code, spec.function_code);
  if (referenced_paths.empty()) {
    throw std::runtime_error("No input series referenced. Set an input timeseries or reference route paths in code.");
  }

  const fs::path temp_dir = create_custom_series_temp_dir();
  try {
    const fs::path globals_path = temp_dir / "globals.py";
    const fs::path code_path = temp_dir / "code.py";
    const fs::path manifest_path = temp_dir / "manifest.json";
    const fs::path out_t_path = temp_dir / "result.t.bin";
    const fs::path out_v_path = temp_dir / "result.v.bin";

    write_text_file(globals_path, spec.globals_code);
    write_text_file(code_path, spec.function_code);

    std::ofstream manifest(manifest_path);
    if (!manifest) {
      throw std::runtime_error("Failed to open manifest for writing");
    }
    manifest << "{\n";
    manifest << "  \"paths\": [";
    for (size_t i = 0; i < session.route_data.paths.size(); ++i) {
      if (i != 0) {
        manifest << ", ";
      }
      manifest << "\"" << json_escape(session.route_data.paths[i]) << "\"";
    }
    manifest << "],\n";
    manifest << "  \"linked_source\": \"" << json_escape(spec.linked_source) << "\",\n";
    manifest << "  \"additional_sources\": [";
    for (size_t i = 0; i < spec.additional_sources.size(); ++i) {
      if (i != 0) {
        manifest << ", ";
      }
      manifest << "\"" << json_escape(spec.additional_sources[i]) << "\"";
    }
    manifest << "],\n";
    manifest << "  \"series\": [\n";

    size_t series_index = 0;
    for (const std::string &path : referenced_paths) {
      const RouteSeries *series = find_route_series(session, path);
      if (series == nullptr || series->times.size() < 2 || series->times.size() != series->values.size()) {
        throw std::runtime_error("Missing route series " + path);
      }
      const std::string prefix = "series_" + std::to_string(series_index++);
      const fs::path time_path = temp_dir / (prefix + ".t.bin");
      const fs::path value_path = temp_dir / (prefix + ".v.bin");
      write_binary_vector(time_path, series->times);
      write_binary_vector(value_path, series->values);
      manifest << "    {\"path\": \"" << json_escape(path)
               << "\", \"t\": \"" << json_escape(time_path.string())
               << "\", \"v\": \"" << json_escape(value_path.string()) << "\"}";
      if (series_index < referenced_paths.size()) {
        manifest << ",";
      }
      manifest << "\n";
    }
    manifest << "  ]\n";
    manifest << "}\n";
    manifest.close();

    const ProcessResult process = run_process_capture_stderr({
      "python3",
      math_eval_script_path().string(),
      manifest_path.string(),
      globals_path.string(),
      code_path.string(),
      out_t_path.string(),
      out_v_path.string(),
    });
    if (process.exit_code != 0) {
      throw std::runtime_error(trim_copy(process.stderr_text).empty() ? "Python evaluation failed"
                                                                      : trim_copy(process.stderr_text));
    }

    PythonEvalResult result;
    result.xs = read_binary_vector(out_t_path);
    result.ys = read_binary_vector(out_v_path);
    if (result.xs.size() < 2 || result.xs.size() != result.ys.size()) {
      throw std::runtime_error("Custom series returned invalid output");
    }
    fs::remove_all(temp_dir);
    return result;
  } catch (...) {
    std::error_code ignore_error;
    fs::remove_all(temp_dir, ignore_error);
    throw;
  }
}

void refresh_custom_curve_samples(AppSession *session, UiState *state, Curve *curve) {
  if (!curve->custom_python.has_value()) {
    return;
  }
  if (!session->route_data.has_time_range || session->route_data.series.empty()) {
    curve->xs.clear();
    curve->ys.clear();
    return;
  }
  try {
    PythonEvalResult result = evaluate_custom_python_series(*session, *curve->custom_python);
    curve->xs = std::move(result.xs);
    curve->ys = std::move(result.ys);
  } catch (const std::exception &err) {
    curve->xs.clear();
    curve->ys.clear();
    state->error_text = std::string("Failed to evaluate custom series \"")
      + curve_display_name(*curve) + "\":\n\n" + err.what();
    state->open_error_popup = true;
  }
}

void refresh_all_custom_curves(AppSession *session, UiState *state) {
  for (WorkspaceTab &tab : session->layout.tabs) {
    for (Pane &pane : tab.panes) {
      for (Curve &curve : pane.curves) {
        refresh_custom_curve_samples(session, state, &curve);
      }
    }
  }
}

std::optional<std::pair<double, double>> tab_default_x_range(const WorkspaceTab &tab) {
  bool found = false;
  double min_value = 0.0;
  double max_value = 1.0;
  for (const Pane &pane : tab.panes) {
    if (!pane.range.valid || pane.range.right <= pane.range.left) {
      continue;
    }
    if (!found) {
      min_value = pane.range.left;
      max_value = pane.range.right;
      found = true;
    } else {
      min_value = std::min(min_value, pane.range.left);
      max_value = std::max(max_value, pane.range.right);
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return std::make_pair(min_value, max_value);
}

void ensure_shared_range(UiState *state, const AppSession &session) {
  if (state->has_shared_range) {
    return;
  }
  if (session.route_data.has_time_range) {
    state->route_x_min = session.route_data.x_min;
    state->route_x_max = session.route_data.x_max;
  } else {
    state->route_x_min = 0.0;
    state->route_x_max = 1.0;
  }

  if (const WorkspaceTab *tab = active_tab(session.layout, *state); tab != nullptr) {
    if (std::optional<std::pair<double, double>> tab_range = tab_default_x_range(*tab); tab_range.has_value()) {
      state->x_view_min = tab_range->first;
      state->x_view_max = tab_range->second;
      state->has_shared_range = true;
      if (!state->has_tracker_time || state->tracker_time < state->route_x_min || state->tracker_time > state->route_x_max) {
        state->tracker_time = state->route_x_min;
        state->has_tracker_time = true;
      }
      return;
    }
  }

  state->x_view_min = state->route_x_min;
  state->x_view_max = state->route_x_max;
  if (state->x_view_max <= state->x_view_min) {
    state->x_view_max = state->x_view_min + 1.0;
  }
  state->has_shared_range = true;
  if (!state->has_tracker_time || state->tracker_time < state->route_x_min || state->tracker_time > state->route_x_max) {
    state->tracker_time = state->route_x_min;
    state->has_tracker_time = true;
  }
}

void clamp_shared_range(UiState *state) {
  if (!state->has_shared_range) {
    return;
  }
  const double min_span = kMinHorizontalZoomSeconds;
  double span = state->x_view_max - state->x_view_min;
  if (span < min_span) {
    const double center = 0.5 * (state->x_view_min + state->x_view_max);
    span = min_span;
    state->x_view_min = center - span * 0.5;
    state->x_view_max = center + span * 0.5;
  }
  if (state->route_x_max > state->route_x_min) {
    if (state->x_view_min < state->route_x_min) {
      state->x_view_max += state->route_x_min - state->x_view_min;
      state->x_view_min = state->route_x_min;
    }
    if (state->x_view_max > state->route_x_max) {
      state->x_view_min -= state->x_view_max - state->route_x_max;
      state->x_view_max = state->route_x_max;
    }
    if (state->x_view_min < state->route_x_min) {
      state->x_view_min = state->route_x_min;
    }
    if (state->x_view_max <= state->x_view_min) {
      state->x_view_max = std::min(state->route_x_max, state->x_view_min + min_span);
    }
  }
  if (state->has_tracker_time) {
    state->tracker_time = std::clamp(state->tracker_time, state->route_x_min, state->route_x_max);
  }
}

void reset_shared_range(UiState *state, const AppSession &session) {
  state->has_shared_range = false;
  ensure_shared_range(state, session);
  clamp_shared_range(state);
}

void update_follow_range(UiState *state) {
  if (!state->follow_latest || !state->has_shared_range) {
    return;
  }
  const double span = std::max(kMinHorizontalZoomSeconds, state->x_view_max - state->x_view_min);
  const double route_span = state->route_x_max - state->route_x_min;
  if (route_span <= 0.0) {
    return;
  }
  if (route_span <= span) {
    state->x_view_min = state->route_x_min;
    state->x_view_max = state->route_x_max;
  } else {
    state->x_view_max = state->route_x_max;
    state->x_view_min = state->x_view_max - span;
  }
  clamp_shared_range(state);
}

void advance_playback(UiState *state) {
  if (!state->playback_playing || !state->has_shared_range || state->route_x_max <= state->route_x_min) {
    return;
  }

  state->tracker_time += std::max(0.0, static_cast<double>(ImGui::GetIO().DeltaTime)) * state->playback_rate;
  if (state->tracker_time >= state->route_x_max) {
    if (state->playback_loop) {
      state->tracker_time = state->route_x_min;
    } else {
      state->tracker_time = state->route_x_max;
      state->playback_playing = false;
    }
  }

  const double span = std::max(kMinHorizontalZoomSeconds, state->x_view_max - state->x_view_min);
  if (state->tracker_time < state->x_view_min || state->tracker_time > state->x_view_max) {
    state->x_view_min = state->tracker_time - span * 0.5;
    state->x_view_max = state->tracker_time + span * 0.5;
    clamp_shared_range(state);
  }
}

void step_tracker(UiState *state, double direction) {
  if (!state->has_shared_range) {
    return;
  }
  state->tracker_time += direction * std::max(0.001, state->playback_step);
  state->tracker_time = std::clamp(state->tracker_time, state->route_x_min, state->route_x_max);
}

void show_hover_tooltip(const char *text) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(text);
  ImGui::EndTooltip();
  ImGui::PopStyleVar();
}

std::string layout_combo_label(const AppSession &session, const UiState &state) {
  const std::string base = session.layout_path.empty() ? std::string("untitled") : session.layout_path.stem().string();
  return state.layout_dirty ? base + " *" : base;
}

float draw_main_menu_bar(AppSession *session, UiState *state) {
  float height = ImGui::GetFrameHeight();
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Route...")) {
        state->open_open_route = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("New Layout")) {
        session->layout = make_empty_layout();
        session->layout_path.clear();
        session->autosave_path.clear();
        state->layout_dirty = false;
        state->status_text = "New untitled layout";
        state->tabs.clear();
        cancel_rename_tab(state);
        sync_ui_state(state, session->layout);
        sync_layout_buffers(state, *session);
        mark_all_docks_dirty(state);
        reset_shared_range(state, *session);
      }
      if (ImGui::MenuItem("Load Layout...")) {
        state->open_load_layout = true;
      }
      if (ImGui::MenuItem("Save Layout")) {
        state->request_save_layout = true;
      }
      if (ImGui::MenuItem("Save Layout As...")) {
        state->open_save_layout = true;
      }
      if (ImGui::MenuItem("Reset Layout")) {
        state->request_reset_layout = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Show DEPRECATED Fields", nullptr, state->show_deprecated_fields)) {
        state->show_deprecated_fields = !state->show_deprecated_fields;
        rebuild_browser_nodes(session, state);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Plot View")) {
        reset_shared_range(state, *session);
        state->follow_latest = false;
        state->suppress_range_side_effects = true;
        state->status_text = "Plot view reset";
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Close")) {
        state->request_close = true;
      }
      ImGui::EndMenu();
    }
    height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
  }
  return height;
}

void draw_status_bar(const AppSession &session, const UiMetrics &ui, UiState *state) {
  ImGui::SetNextWindowPos(ImVec2(ui.content_x, ui.status_bar_y));
  ImGui::SetNextWindowSize(ImVec2(ui.content_w, kStatusBarHeight));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(247, 248, 250));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(188, 193, 199));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##status_bar", nullptr, flags)) {
    ImGui::BeginDisabled(!session.route_data.has_time_range);
    if (ImGui::Checkbox("Loop", &state->playback_loop)) {
    }
    ImGui::SameLine();
    if (ImGui::Button(state->playback_playing ? "Pause" : "Play", ImVec2(56.0f, 0.0f))) {
      state->playback_playing = !state->playback_playing;
    }
    ImGui::SameLine();
    const float time_width = 88.0f;
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - time_width - 16.0f));
    if (ImGui::SliderScalar("##time_slider",
                            ImGuiDataType_Double,
                            &state->tracker_time,
                            &state->route_x_min,
                            &state->route_x_max,
                            "%.3f")) {
      const double span = std::max(kMinHorizontalZoomSeconds, state->x_view_max - state->x_view_min);
      if (state->tracker_time < state->x_view_min || state->tracker_time > state->x_view_max) {
        state->x_view_min = state->tracker_time - span * 0.5;
        state->x_view_max = state->tracker_time + span * 0.5;
        clamp_shared_range(state);
      }
    }
    ImGui::SameLine();
    char tracker_text[64] = {};
    std::snprintf(tracker_text, sizeof(tracker_text), "%.3f", state->has_tracker_time ? state->tracker_time : 0.0);
    ImGui::TextUnformatted(tracker_text);
    ImGui::EndDisabled();
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

bool browser_node_matches(const BrowserNode &node, const std::string &filter) {
  if (filter.empty()) {
    return true;
  }
  if (!node.full_path.empty() && path_matches_filter(node.full_path, filter)) {
    return true;
  }
  for (const BrowserNode &child : node.children) {
    if (browser_node_matches(child, filter)) {
      return true;
    }
  }
  return false;
}

void draw_browser_node(AppSession *session,
                       const BrowserNode &node,
                       UiState *state,
                       const std::string &filter,
                       const std::vector<std::string> &visible_paths) {
  if (!browser_node_matches(node, filter)) {
    return;
  }

  if (node.children.empty()) {
    const bool selected = browser_selection_contains(*state, node.full_path);
    if (ImGui::Selectable(node.label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
      const bool shift_down = ImGui::GetIO().KeyShift;
      const bool ctrl_down = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
      if (shift_down) {
        select_browser_range(state, visible_paths, node.full_path);
      } else if (ctrl_down) {
        toggle_browser_selection(state, node.full_path);
      } else {
        set_browser_selection_single(state, node.full_path);
      }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      set_browser_selection_single(state, node.full_path);
      add_curve_to_active_pane(session, state, node.full_path);
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      ImGui::SetDragDropPayload("JOTP_BROWSER_PATH", node.full_path.c_str(), node.full_path.size() + 1);
      ImGui::TextUnformatted(node.full_path.c_str());
      ImGui::EndDragDropSource();
    }
    return;
  }

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
  if (!filter.empty()) {
    flags |= ImGuiTreeNodeFlags_DefaultOpen;
  }
  const bool open = ImGui::TreeNodeEx(node.label.c_str(), flags);
  if (open) {
    for (const BrowserNode &child : node.children) {
      draw_browser_node(session, child, state, filter, visible_paths);
    }
    ImGui::TreePop();
  }
}

void draw_sidebar_resizer(const UiMetrics &ui, UiState *state) {
  constexpr float kHandleWidth = 6.0f;
  ImGui::SetNextWindowPos(ImVec2(ui.sidebar_width - kHandleWidth * 0.5f, ui.top_offset));
  ImGui::SetNextWindowSize(ImVec2(kHandleWidth, std::max(1.0f, ui.height - ui.top_offset)));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground;
  if (ImGui::Begin("##sidebar_resizer", nullptr, flags)) {
    ImGui::InvisibleButton("##sidebar_resizer_button", ImGui::GetContentRegionAvail());
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
      const float max_width = std::min(kSidebarMaxWidth, ui.width * 0.6f);
      state->sidebar_width = std::clamp(ImGui::GetIO().MousePos.x, kSidebarMinWidth, max_width);
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    draw_list->AddLine(ImVec2(origin.x + kHandleWidth * 0.5f, origin.y),
                       ImVec2(origin.x + kHandleWidth * 0.5f, origin.y + std::max(1.0f, ui.height - ui.top_offset)),
                       IM_COL32(194, 198, 204, 255));
  }
  ImGui::End();
}

void draw_sidebar(AppSession *session, const UiMetrics &ui, UiState *state, bool show_camera_feed) {
  ImGui::SetNextWindowPos(ImVec2(0.0f, ui.top_offset));
  ImGui::SetNextWindowSize(ImVec2(ui.sidebar_width, std::max(1.0f, ui.height - ui.top_offset)));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(238, 240, 244));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(190, 197, 205));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##sidebar", nullptr, flags)) {
    const RouteLoadSnapshot load = session->route_loader ? session->route_loader->snapshot() : RouteLoadSnapshot{};
    if (show_camera_feed && session->camera_feed) {
      session->camera_feed->draw(ImGui::GetContentRegionAvail().x, load.active);
    }

    if (session->route_loader) {
      if (load.active || load.total_segments > 0) {
        const float total = static_cast<float>(std::max<size_t>(1, load.total_segments));
        const float progress = load.total_segments == 0
          ? 0.0f
          : std::clamp(static_cast<float>(load.segments_downloaded + load.segments_parsed) / (2.0f * total), 0.0f, 1.0f);
        if (!session->route_name.empty()) {
          ImGui::TextWrapped("%s", session->route_name.c_str());
        }
        ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), nullptr);
        ImGui::Spacing();
      }
    }

    ImGui::SeparatorText("Layout");
    const std::vector<std::string> layouts = available_layout_names();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##layout_combo", layout_combo_label(*session, *state).c_str())) {
      if (ImGui::Selectable("New Layout")) {
        session->layout = make_empty_layout();
        session->layout_path.clear();
        session->autosave_path.clear();
        state->layout_dirty = false;
        state->status_text = "New untitled layout";
        state->tabs.clear();
        cancel_rename_tab(state);
        sync_ui_state(state, session->layout);
        sync_layout_buffers(state, *session);
        mark_all_docks_dirty(state);
        reset_shared_range(state, *session);
      }
      ImGui::Separator();
      const std::string current_layout = session->layout_path.empty() ? std::string("untitled") : session->layout_path.stem().string();
      for (const std::string &layout_name : layouts) {
        const bool selected = layout_name == current_layout;
        if (ImGui::Selectable(layout_name.c_str(), selected) && !selected) {
          reload_layout(session, state, layout_name);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::Button("New", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
      session->layout = make_empty_layout();
      session->layout_path.clear();
      session->autosave_path.clear();
      state->layout_dirty = false;
      state->status_text = "New untitled layout";
      state->tabs.clear();
      cancel_rename_tab(state);
      sync_ui_state(state, session->layout);
      sync_layout_buffers(state, *session);
      mark_all_docks_dirty(state);
      reset_shared_range(state, *session);
    }
    if (ImGui::Button("Save", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
      state->request_save_layout = true;
    }
    ImGui::BeginDisabled(!state->layout_dirty);
    if (ImGui::Button("Reset", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
      state->request_reset_layout = true;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::SeparatorText("Timeseries List");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##browser_filter", "Search...", state->browser_filter.data(), state->browser_filter.size());
    const float footer_height = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 16.0f;
    const float browser_height = std::max(1.0f, ImGui::GetContentRegionAvail().y - footer_height);
    if (ImGui::BeginChild("##timeseries_browser", ImVec2(0.0f, browser_height), true)) {
      const std::string filter = string_from_buffer(state->browser_filter);
      std::vector<std::string> visible_paths;
      for (const BrowserNode &node : session->browser_nodes) {
        collect_visible_leaf_paths(node, filter, &visible_paths);
      }
      for (const BrowserNode &node : session->browser_nodes) {
        draw_browser_node(session, node, state, filter, visible_paths);
      }
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Custom Series");
    if (ImGui::Button("Create...", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
      open_custom_series_editor(state, state->selected_browser_path);
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
}

std::string curve_display_name(const Curve &curve) {
  if (!curve.label.empty()) {
    return curve.label;
  }
  if (!curve.name.empty()) {
    return curve.name;
  }
  return "curve";
}

std::string path_curve_label(std::string_view path) {
  return std::string(path);
}

Curve make_curve_for_path(const Pane &pane, const std::string &path) {
  Curve curve;
  curve.name = path;
  curve.label = path_curve_label(path);
  curve.color = next_curve_color(pane);
  return curve;
}

Curve make_custom_curve(const Pane &pane,
                        const std::string &name,
                        const CustomPythonSeries &spec,
                        PythonEvalResult result) {
  Curve curve;
  curve.name = name;
  curve.label = name;
  curve.color = next_curve_color(pane);
  curve.runtime_only = true;
  curve.custom_python = spec;
  curve.xs = std::move(result.xs);
  curve.ys = std::move(result.ys);
  return curve;
}

bool upsert_custom_curve_in_pane(WorkspaceTab *tab, int pane_index, Curve curve) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  for (Curve &existing : pane.curves) {
    if (existing.runtime_only && existing.name == curve.name) {
      existing.visible = true;
      existing.label = curve.label;
      existing.custom_python = curve.custom_python;
      existing.xs = std::move(curve.xs);
      existing.ys = std::move(curve.ys);
      return false;
    }
  }
  pane.curves.push_back(std::move(curve));
  return true;
}

bool add_curve_to_pane(WorkspaceTab *tab, int pane_index, Curve curve) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  for (Curve &existing : pane.curves) {
    const bool same_named_curve = !curve.name.empty() && existing.name == curve.name;
    const bool same_unnamed_curve = curve.name.empty() && existing.name.empty() && existing.label == curve.label;
    if (same_named_curve || same_unnamed_curve) {
      existing.visible = true;
      return false;
    }
  }
  pane.curves.push_back(std::move(curve));
  return true;
}

bool add_path_curve_to_pane(AppSession *session, UiState *state, int pane_index, const std::string &path) {
  if (find_route_series(*session, path) == nullptr) {
    state->status_text = "Path not found in route";
    return false;
  }
  WorkspaceTab *tab = active_tab(&session->layout, *state);
  if (tab == nullptr || pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    state->status_text = "No active pane";
    return false;
  }
  const bool inserted = add_curve_to_pane(tab, pane_index, make_curve_for_path(tab->panes[static_cast<size_t>(pane_index)], path));
  bool autosave_ok = true;
  if (inserted) {
    autosave_ok = mark_layout_dirty(session, state);
  }
  if (autosave_ok) {
    state->status_text = inserted ? "Added " + path : "Curve already present";
  }
  return true;
}

bool copy_curve_to_pane(WorkspaceTab *tab, int pane_index, const Curve &curve) {
  return add_curve_to_pane(tab, pane_index, curve);
}

bool add_curve_to_active_pane(AppSession *session, UiState *state, const std::string &path) {
  const TabUiState *tab_state = active_tab_state(state);
  if (tab_state == nullptr) {
    state->status_text = "No active pane";
    return false;
  }
  return add_path_curve_to_pane(session, state, tab_state->active_pane_index, path);
}

bool split_pane(WorkspaceTab *tab, int pane_index, PaneDropZone zone, std::optional<Curve> curve = std::nullopt) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  if (zone == PaneDropZone::Center) {
    return false;
  }

  const int new_pane_index = static_cast<int>(tab->panes.size());
  Pane new_pane = make_empty_pane();
  if (curve.has_value()) {
    new_pane.curves.push_back(*curve);
  }
  tab->panes.push_back(std::move(new_pane));

  SplitOrientation orientation = SplitOrientation::Horizontal;
  bool new_before = false;
  switch (zone) {
    case PaneDropZone::Left:
      orientation = SplitOrientation::Horizontal;
      new_before = true;
      break;
    case PaneDropZone::Right:
      orientation = SplitOrientation::Horizontal;
      new_before = false;
      break;
    case PaneDropZone::Top:
      orientation = SplitOrientation::Vertical;
      new_before = true;
      break;
    case PaneDropZone::Bottom:
      orientation = SplitOrientation::Vertical;
      new_before = false;
      break;
    case PaneDropZone::Center:
      break;
  }
  return split_pane_node(&tab->root, pane_index, orientation, new_before, new_pane_index);
}

bool close_pane(WorkspaceTab *tab, int pane_index) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  if (tab->panes.size() <= 1) {
    Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
    pane.curves.clear();
    pane.title = kUntitledPaneTitle;
    return true;
  }
  if (remove_pane_node(&tab->root, pane_index)) {
    return false;
  }
  tab->panes.erase(tab->panes.begin() + static_cast<std::ptrdiff_t>(pane_index));
  decrement_pane_indices(&tab->root, pane_index);
  normalize_split_node(&tab->root);
  return true;
}

void clear_pane(WorkspaceTab *tab, int pane_index) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return;
  }
  Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  pane.curves.clear();
  pane.title = kUntitledPaneTitle;
}

void create_runtime_tab(SketchLayout *layout, UiState *state) {
  const std::string tab_name = next_tab_name(*layout, "tab1");
  layout->tabs.push_back(make_empty_tab(tab_name));
  state->tabs.push_back(TabUiState{.dock_needs_build = true, .active_pane_index = 0, .runtime_id = state->next_tab_runtime_id++});
  request_tab_selection(state, static_cast<int>(layout->tabs.size()) - 1);
  state->status_text = "Created " + tab_name;
}

void duplicate_runtime_tab(SketchLayout *layout, UiState *state) {
  if (layout->tabs.empty()) {
    return;
  }
  const int source_index = std::clamp(state->active_tab_index, 0, static_cast<int>(layout->tabs.size()) - 1);
  WorkspaceTab copy = layout->tabs[static_cast<size_t>(source_index)];
  copy.tab_name = next_tab_name(*layout, copy.tab_name + " copy");
  layout->tabs.push_back(std::move(copy));
  const int active_pane_index = source_index < static_cast<int>(state->tabs.size()) ? state->tabs[static_cast<size_t>(source_index)].active_pane_index : 0;
  state->tabs.push_back(TabUiState{.dock_needs_build = true, .active_pane_index = active_pane_index, .runtime_id = state->next_tab_runtime_id++});
  request_tab_selection(state, static_cast<int>(layout->tabs.size()) - 1);
  state->status_text = "Duplicated tab";
}

void close_runtime_tab(SketchLayout *layout, UiState *state) {
  if (layout->tabs.empty()) {
    return;
  }
  const int tab_index = std::clamp(state->active_tab_index, 0, static_cast<int>(layout->tabs.size()) - 1);
  if (layout->tabs.size() == 1) {
    layout->tabs[0] = make_empty_tab(layout->tabs[0].tab_name.empty() ? "tab1" : layout->tabs[0].tab_name);
    if (state->tabs.empty()) {
      state->tabs.push_back(TabUiState{.dock_needs_build = true, .active_pane_index = 0});
    } else {
      state->tabs.resize(1);
      state->tabs[0] = TabUiState{
        .dock_needs_build = true,
        .active_pane_index = 0,
        .runtime_id = state->tabs[0].runtime_id == 0 ? state->next_tab_runtime_id++ : state->tabs[0].runtime_id,
      };
    }
    state->active_tab_index = 0;
    state->requested_tab_index = 0;
    layout->current_tab_index = 0;
    cancel_rename_tab(state);
    state->status_text = "Closed tab";
    return;
  }
  layout->tabs.erase(layout->tabs.begin() + static_cast<std::ptrdiff_t>(tab_index));
  if (tab_index < static_cast<int>(state->tabs.size())) {
    state->tabs.erase(state->tabs.begin() + static_cast<std::ptrdiff_t>(tab_index));
  }
  if (state->active_tab_index >= static_cast<int>(layout->tabs.size())) {
    state->active_tab_index = static_cast<int>(layout->tabs.size()) - 1;
  }
  sync_ui_state(state, *layout);
  state->requested_tab_index = state->active_tab_index;
  state->status_text = "Closed tab";
}

void rename_runtime_tab(SketchLayout *layout, UiState *state) {
  if (state->rename_tab_index < 0 || state->rename_tab_index >= static_cast<int>(layout->tabs.size())) {
    return;
  }
  layout->tabs[static_cast<size_t>(state->rename_tab_index)].tab_name = string_from_buffer(state->rename_tab_buffer);
  state->status_text = "Renamed tab";
  layout->current_tab_index = state->rename_tab_index;
  cancel_rename_tab(state);
}

void draw_inline_tab_editor(AppSession *session, UiState *state, const ImRect &tab_rect) {
  const int rename_tab_index = state->rename_tab_index;
  if (rename_tab_index < 0 || rename_tab_index >= static_cast<int>(session->layout.tabs.size())) {
    return;
  }

  const float width = std::max(48.0f, tab_rect.Max.x - tab_rect.Min.x - 10.0f);
  const ImVec2 pos = ImVec2(tab_rect.Min.x + 5.0f, tab_rect.Min.y + 2.0f);
  ImGui::SetCursorScreenPos(pos);
  ImGui::PushItemWidth(width);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
  if (state->focus_rename_tab_input) {
    ImGui::SetKeyboardFocusHere();
    state->focus_rename_tab_input = false;
  }
  const bool submitted = ImGui::InputText("##rename_tab_inline",
                                          state->rename_tab_buffer.data(),
                                          state->rename_tab_buffer.size(),
                                          ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
  const bool active = ImGui::IsItemActive();
  const bool escape = active && ImGui::IsKeyPressed(ImGuiKey_Escape);
  const bool deactivated = ImGui::IsItemDeactivated();
  ImGui::PopStyleVar();
  ImGui::PopItemWidth();

  if (escape) {
    cancel_rename_tab(state);
  } else if (submitted || deactivated) {
    rename_runtime_tab(&session->layout, state);
    mark_layout_dirty(session, state);
  }
}

bool curve_has_samples(const AppSession &session, const Curve &curve) {
  if (curve_has_local_samples(curve)) {
    return true;
  }
  if (curve.name.empty() || curve.name.front() != '/') {
    return false;
  }
  const RouteSeries *series = find_route_series(session, curve.name);
  return series != nullptr && series->times.size() > 1 && series->times.size() == series->values.size();
}

void extend_range(const std::vector<double> &values, bool *found, double *min_value, double *max_value) {
  if (values.empty()) {
    return;
  }
  const auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
  if (!*found) {
    *min_value = *min_it;
    *max_value = *max_it;
    *found = true;
    return;
  }
  *min_value = std::min(*min_value, *min_it);
  *max_value = std::max(*max_value, *max_it);
}

void ensure_non_degenerate_range(double *min_value, double *max_value, double pad_fraction, double fallback_pad) {
  if (*max_value <= *min_value) {
    const double pad = std::max(std::abs(*min_value) * 0.1, fallback_pad);
    *min_value -= pad;
    *max_value += pad;
    return;
  }
  const double span = *max_value - *min_value;
  const double pad = std::max(span * pad_fraction, fallback_pad);
  *min_value -= pad;
  *max_value += pad;
}

struct PreparedCurve {
  int pane_curve_index = -1;
  std::string label;
  std::array<uint8_t, 3> color = {160, 170, 180};
  float line_weight = 2.0f;
  bool stairs = false;
  std::vector<double> xs;
  std::vector<double> ys;
};

bool is_digital_series(const std::vector<double> &values) {
  if (values.size() < 2) {
    return false;
  }
  std::vector<int> unique_levels;
  unique_levels.reserve(std::min<size_t>(values.size(), 8));
  for (double value : values) {
    const double rounded = std::round(value);
    if (std::abs(value - rounded) > 1.0e-6) {
      return false;
    }
    const int level = static_cast<int>(rounded);
    if (std::find(unique_levels.begin(), unique_levels.end(), level) == unique_levels.end()) {
      unique_levels.push_back(level);
      if (unique_levels.size() > 8) {
        return false;
      }
    }
  }
  return true;
}

void decimate_samples(const std::vector<double> &xs_in,
                      const std::vector<double> &ys_in,
                      int max_points,
                      std::vector<double> *xs_out,
                      std::vector<double> *ys_out) {
  xs_out->clear();
  ys_out->clear();
  if (xs_in.empty() || xs_in.size() != ys_in.size()) {
    return;
  }
  if (max_points <= 0 || static_cast<int>(xs_in.size()) <= max_points) {
    *xs_out = xs_in;
    *ys_out = ys_in;
    return;
  }

  const size_t step = std::max<size_t>(1, static_cast<size_t>(std::ceil(static_cast<double>(xs_in.size()) / max_points)));
  xs_out->reserve(xs_in.size() / step + 2);
  ys_out->reserve(ys_in.size() / step + 2);
  for (size_t i = 0; i < xs_in.size(); i += step) {
    xs_out->push_back(xs_in[i]);
    ys_out->push_back(ys_in[i]);
  }
  if (xs_out->empty() || xs_out->back() != xs_in.back()) {
    xs_out->push_back(xs_in.back());
    ys_out->push_back(ys_in.back());
  }
}

std::optional<double> sample_curve_value_at_time(const PreparedCurve &curve, double tm) {
  if (curve.xs.size() < 2 || curve.xs.size() != curve.ys.size()) {
    return std::nullopt;
  }
  if (tm < curve.xs.front() || tm > curve.xs.back()) {
    return std::nullopt;
  }

  const auto upper = std::lower_bound(curve.xs.begin(), curve.xs.end(), tm);
  if (upper == curve.xs.begin()) {
    return curve.ys.front();
  }
  if (upper == curve.xs.end()) {
    return curve.ys.back();
  }

  const size_t upper_index = static_cast<size_t>(std::distance(curve.xs.begin(), upper));
  const size_t lower_index = upper_index - 1;
  const double x0 = curve.xs[lower_index];
  const double x1 = curve.xs[upper_index];
  const double y0 = curve.ys[lower_index];
  const double y1 = curve.ys[upper_index];
  if (std::abs(tm - x1) < 1.0e-9) {
    return y1;
  }
  if (curve.stairs || x1 <= x0) {
    return y0;
  }
  const double alpha = (tm - x0) / (x1 - x0);
  return y0 + (y1 - y0) * alpha;
}

void draw_cursor_overlay(const std::vector<PreparedCurve> &prepared_curves, double cursor_time) {
  std::vector<std::string> lines;
  lines.reserve(1 + std::min(prepared_curves.size(), kCursorOverlayValueCount));

  char time_line[64] = {};
  std::snprintf(time_line, sizeof(time_line), "t=%.3f", cursor_time);
  lines.emplace_back(time_line);

  for (const PreparedCurve &curve : prepared_curves) {
    if (lines.size() > kCursorOverlayValueCount) {
      break;
    }
    std::optional<double> value = sample_curve_value_at_time(curve, cursor_time);
    if (!value.has_value()) {
      continue;
    }
    char value_line[256] = {};
    std::snprintf(value_line, sizeof(value_line), "%s %.6g", curve.label.c_str(), *value);
    lines.emplace_back(value_line);
  }

  if (lines.size() <= 1) {
    return;
  }

  const ImVec2 plot_pos = ImPlot::GetPlotPos();
  const float line_height = ImGui::GetTextLineHeight();
  float text_width = 0.0f;
  for (const std::string &line : lines) {
    text_width = std::max(text_width, ImGui::CalcTextSize(line.c_str()).x);
  }

  const ImVec2 box_min(plot_pos.x + 10.0f, plot_pos.y + 10.0f);
  const ImVec2 box_max(box_min.x + text_width + 16.0f,
                       box_min.y + static_cast<float>(lines.size()) * line_height + 12.0f);
  ImDrawList *draw_list = ImPlot::GetPlotDrawList();
  draw_list->AddRectFilled(box_min, box_max, ImGui::GetColorU32(color_rgb(250, 251, 252, 0.92f)), 4.0f);
  draw_list->AddRect(box_min, box_max, ImGui::GetColorU32(color_rgb(186, 190, 196)), 4.0f);

  ImVec2 text_pos(box_min.x + 8.0f, box_min.y + 6.0f);
  for (size_t i = 0; i < lines.size(); ++i) {
    const ImU32 color = i == 0
      ? ImGui::GetColorU32(color_rgb(70, 77, 86))
      : ImGui::GetColorU32(color_rgb(88, 96, 104));
    draw_list->AddText(text_pos, color, lines[i].c_str());
    text_pos.y += line_height;
  }
}

bool build_curve_series(const AppSession &session,
                        const Curve &curve,
                        const UiState &state,
                        int max_points,
                        PreparedCurve *prepared) {
  std::vector<double> xs;
  std::vector<double> ys;
  if (curve_has_local_samples(curve)) {
    xs = curve.xs;
    ys = curve.ys;
  } else {
    const RouteSeries *series = find_route_series(session, curve.name);
    if (series == nullptr || series->times.size() < 2 || series->times.size() != series->values.size()) {
      return false;
    }

    size_t begin_index = 0;
    size_t end_index = series->times.size();
    if (state.has_shared_range && state.x_view_max > state.x_view_min) {
      auto begin_it = std::lower_bound(series->times.begin(), series->times.end(), state.x_view_min);
      auto end_it = std::upper_bound(series->times.begin(), series->times.end(), state.x_view_max);
      begin_index = begin_it == series->times.begin() ? 0 : static_cast<size_t>(std::distance(series->times.begin(), begin_it - 1));
      end_index = end_it == series->times.end() ? series->times.size() : static_cast<size_t>(std::distance(series->times.begin(), end_it + 1));
      end_index = std::min(end_index, series->times.size());
    }
    if (end_index <= begin_index + 1) {
      return false;
    }
    xs.assign(series->times.begin() + begin_index, series->times.begin() + end_index);
    ys.assign(series->values.begin() + begin_index, series->values.begin() + end_index);
  }

  std::vector<double> transformed_xs;
  std::vector<double> transformed_ys;
  if (curve.derivative) {
    if (xs.size() < 2) {
      return false;
    }
    transformed_xs.reserve(xs.size() - 1);
    transformed_ys.reserve(ys.size() - 1);
    for (size_t i = 1; i < xs.size(); ++i) {
      const double dt = xs[i] - xs[i - 1];
      if (dt <= 0.0) {
        continue;
      }
      transformed_xs.push_back(xs[i]);
      transformed_ys.push_back((ys[i] - ys[i - 1]) / dt);
    }
  } else {
    transformed_xs = std::move(xs);
    transformed_ys = std::move(ys);
  }

  if (transformed_xs.size() < 2 || transformed_xs.size() != transformed_ys.size()) {
    return false;
  }

  for (double &value : transformed_ys) {
    value = value * curve.value_scale + curve.value_offset;
  }

  prepared->label = curve_display_name(curve);
  prepared->color = curve.color;
  prepared->line_weight = curve.derivative ? 1.8f : 2.25f;
  decimate_samples(transformed_xs, transformed_ys, max_points, &prepared->xs, &prepared->ys);
  prepared->stairs = !curve.derivative && is_digital_series(prepared->ys);
  return prepared->xs.size() > 1 && prepared->xs.size() == prepared->ys.size();
}

bool draw_close_icon_button(const char *id, bool draw_icon, ImVec2 size = ImVec2(16.0f, 16.0f));

bool draw_close_icon_button(const char *id, bool draw_icon, ImVec2 size) {
  const bool clicked = ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  if (draw_icon || hovered || held) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const float pad = 4.5f;
    const ImU32 color = hovered || held
      ? ImGui::GetColorU32(color_rgb(72, 79, 88))
      : ImGui::GetColorU32(color_rgb(138, 146, 156));
    draw_list->AddLine(ImVec2(rect.Min.x + pad, rect.Min.y + pad),
                       ImVec2(rect.Max.x - pad, rect.Max.y - pad),
                       color,
                       1.5f);
    draw_list->AddLine(ImVec2(rect.Min.x + pad, rect.Max.y - pad),
                       ImVec2(rect.Max.x - pad, rect.Min.y + pad),
                       color,
                       1.5f);
  }
  return clicked;
}

bool draw_pane_close_button_overlay() {
  const ImVec2 window_pos = ImGui::GetWindowPos();
  const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
  const ImVec2 button_pos(window_pos.x + content_max.x - 18.0f, window_pos.y + 4.0f);
  ImGui::SetCursorScreenPos(button_pos);
  ImGui::PushID("pane_close_overlay");
  const bool clicked = draw_close_icon_button("##pane_close", true, ImVec2(16.0f, 16.0f));
  ImGui::PopID();
  return clicked;
}

PlotBounds compute_plot_bounds(const Pane &pane,
                               const std::vector<PreparedCurve> &prepared_curves,
                               const UiState &state) {
  PlotBounds bounds;
  bounds.x_min = state.has_shared_range ? state.x_view_min : 0.0;
  bounds.x_max = state.has_shared_range ? state.x_view_max : 1.0;
  if (bounds.x_max <= bounds.x_min) {
    bounds.x_max = bounds.x_min + 1.0;
  }

  bool found = false;
  double min_value = 0.0;
  double max_value = 1.0;
  for (const PreparedCurve &curve : prepared_curves) {
    extend_range(curve.ys, &found, &min_value, &max_value);
  }
  if (!found) {
    min_value = 0.0;
    max_value = 1.0;
  }
  ensure_non_degenerate_range(&min_value, &max_value, kPlotYPadFraction, 0.1);
  if (pane.range.has_y_limit_min) {
    min_value = pane.range.y_limit_min;
  }
  if (pane.range.has_y_limit_max) {
    max_value = pane.range.y_limit_max;
  }
  ensure_non_degenerate_range(&min_value, &max_value, 0.0, 0.1);
  bounds.y_min = min_value;
  bounds.y_max = max_value;
  return bounds;
}

void draw_plot(const AppSession &session, Pane *pane, UiState *state) {
  std::vector<PreparedCurve> prepared_curves;
  prepared_curves.reserve(pane->curves.size());
  const int max_points = std::max(256, static_cast<int>(ImGui::GetContentRegionAvail().x) * 2);
  for (size_t curve_index = 0; curve_index < pane->curves.size(); ++curve_index) {
    const Curve &curve = pane->curves[curve_index];
    if (!curve.visible || !curve_has_samples(session, curve)) {
      continue;
    }
    PreparedCurve prepared;
    if (build_curve_series(session, curve, *state, max_points, &prepared)) {
      prepared.pane_curve_index = static_cast<int>(curve_index);
      prepared_curves.push_back(std::move(prepared));
    }
  }

  const PlotBounds bounds = compute_plot_bounds(*pane, prepared_curves, *state);
  const int supported_count = static_cast<int>(prepared_curves.size());
  const ImVec2 plot_size = ImGui::GetContentRegionAvail();
  const bool has_cursor_time = state->has_tracker_time;
  const double cursor_time = state->tracker_time;

  ImPlot::PushStyleColor(ImPlotCol_PlotBg, color_rgb(255, 255, 255));
  ImPlot::PushStyleColor(ImPlotCol_PlotBorder, color_rgb(186, 190, 196));
  ImPlot::PushStyleColor(ImPlotCol_LegendBg, color_rgb(248, 249, 251, 0.92f));
  ImPlot::PushStyleColor(ImPlotCol_LegendBorder, color_rgb(168, 175, 184));
  ImPlot::PushStyleColor(ImPlotCol_AxisGrid, color_rgb(188, 196, 206));
  ImPlot::PushStyleColor(ImPlotCol_AxisText, color_rgb(95, 103, 112));

  ImPlotFlags plot_flags = ImPlotFlags_NoTitle | ImPlotFlags_NoMenus;
  if (supported_count == 0) {
    plot_flags |= ImPlotFlags_NoLegend;
  }

  const ImPlotAxisFlags x_axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  ImPlotAxisFlags y_axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  const bool explicit_y = pane->range.has_y_limit_min || pane->range.has_y_limit_max;
  if (!explicit_y && supported_count > 0) {
    y_axis_flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
  }

  const double previous_x_min = state->x_view_min;
  const double previous_x_max = state->x_view_max;
  if (ImPlot::BeginPlot("##plot", plot_size, plot_flags)) {
    ImPlot::SetupAxes(nullptr, nullptr, x_axis_flags, y_axis_flags);
    ImPlot::SetupAxisFormat(ImAxis_X1, "%.1f");
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.6g");
    ImPlot::SetupAxisLinks(ImAxis_X1, &state->x_view_min, &state->x_view_max);
    if (state->route_x_max > state->route_x_min) {
      ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, state->route_x_min, state->route_x_max);
    }
    ImPlot::SetupMouseText(ImPlotLocation_SouthEast, ImPlotMouseTextFlags_NoAuxAxes);
    if (explicit_y || supported_count == 0) {
      ImPlot::SetupAxisLimits(ImAxis_Y1, bounds.y_min, bounds.y_max, ImPlotCond_Always);
    }
    if (supported_count > 0) {
      ImPlot::SetupLegend(ImPlotLocation_NorthEast);
    }

    for (size_t i = 0; i < prepared_curves.size(); ++i) {
      const PreparedCurve &curve = prepared_curves[i];
      std::string series_id = curve.label + "##curve" + std::to_string(i);
      ImPlotSpec spec;
      spec.LineColor = color_rgb(curve.color);
      spec.LineWeight = curve.line_weight;
      spec.Flags = ImPlotLineFlags_SkipNaN;
      if (!curve.xs.empty() && curve.xs.size() == curve.ys.size()) {
        if (curve.stairs) {
          spec.Flags = ImPlotStairsFlags_PreStep;
          ImPlot::PlotStairs(series_id.c_str(), curve.xs.data(), curve.ys.data(), static_cast<int>(curve.xs.size()), spec);
        } else {
          ImPlot::PlotLine(series_id.c_str(), curve.xs.data(), curve.ys.data(), static_cast<int>(curve.xs.size()), spec);
        }
      }
    }
    if (has_cursor_time) {
      const double clamped_cursor_time = std::clamp(cursor_time, state->route_x_min, state->route_x_max);
      ImPlotSpec cursor_spec;
      cursor_spec.LineColor = color_rgb(108, 118, 128, 0.7f);
      cursor_spec.LineWeight = 1.0f;
      cursor_spec.Flags = ImPlotItemFlags_NoLegend;
      ImPlot::PlotInfLines("##tracker_cursor", &clamped_cursor_time, 1, cursor_spec);
      draw_cursor_overlay(prepared_curves, clamped_cursor_time);
    }
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      state->tracker_time = std::clamp(ImPlot::GetPlotMousePos().x, state->route_x_min, state->route_x_max);
      state->has_tracker_time = true;
    }
    ImPlot::EndPlot();
  }
  clamp_shared_range(state);
  if (std::abs(state->x_view_min - previous_x_min) > 1.0e-6
      || std::abs(state->x_view_max - previous_x_max) > 1.0e-6) {
    if (!state->suppress_range_side_effects) {
      state->follow_latest = false;
    }
  }
  ImPlot::PopStyleColor(6);
}

void draw_custom_series_help_popup(CustomSeriesEditorState *editor) {
  if (editor->open_help) {
    ImGui::OpenPopup("Custom Series Help");
    editor->open_help = false;
  }
  if (!ImGui::BeginPopupModal("Custom Series Help", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextUnformatted("Available variables");
  ImGui::Separator();
  ImGui::BulletText("np: numpy");
  ImGui::BulletText("t(path), v(path): timestamps and values for a route series");
  ImGui::BulletText("paths: all available route series paths");
  ImGui::BulletText("time, value: linked input timeseries");
  ImGui::BulletText("t1, v1, t2, v2, ...: additional source timeseries");
  ImGui::Spacing();
  ImGui::TextWrapped("Write either a single expression like \"return np.gradient(value, time)\" "
                     "or a multi-line Python body that returns an array or a (times, values) tuple.");
  ImGui::Spacing();
  if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void draw_custom_series_preview(const AppSession &session, CustomSeriesEditorState *editor) {
  std::vector<double> preview_xs;
  std::vector<double> preview_ys;
  std::string preview_label = editor->preview_label;
  if (editor->preview_is_result && editor->preview_xs.size() > 1 && editor->preview_xs.size() == editor->preview_ys.size()) {
    preview_xs = editor->preview_xs;
    preview_ys = editor->preview_ys;
    if (preview_label.empty()) {
      preview_label = "Result preview";
    }
  } else if (!editor->linked_source.empty()) {
    if (const RouteSeries *series = find_route_series(session, editor->linked_source); series != nullptr
        && series->times.size() > 1 && series->times.size() == series->values.size()) {
      preview_xs = series->times;
      preview_ys = series->values;
      preview_label = "Input preview";
    }
  }

  if (!preview_xs.empty() && preview_xs.size() == preview_ys.size()) {
    std::vector<double> plot_xs;
    std::vector<double> plot_ys;
    decimate_samples(preview_xs, preview_ys, 1200, &plot_xs, &plot_ys);
    ImGui::TextUnformatted(preview_label.c_str());
    if (!editor->linked_source.empty() && !editor->preview_is_result) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", editor->linked_source.c_str());
    }
    if (ImPlot::BeginPlot("##custom_series_preview",
                          ImVec2(-1.0f, std::max(180.0f, ImGui::GetContentRegionAvail().y - 6.0f)),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight,
                        ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
      ImPlot::SetupAxisFormat(ImAxis_X1, "%.1f");
      ImPlot::SetupAxisFormat(ImAxis_Y1, "%.6g");
      ImPlotSpec spec;
      spec.LineColor = color_rgb(35, 107, 180);
      spec.LineWeight = 2.0f;
      ImPlot::PlotLine("##custom_preview_line", plot_xs.data(), plot_ys.data(), static_cast<int>(plot_xs.size()), spec);
      ImPlot::EndPlot();
    }
  } else {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 72.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(116, 124, 133));
    ImGui::TextWrapped("Choose an input timeseries or click Apply to preview the custom result.");
    ImGui::PopStyleColor();
  }
}

std::string custom_series_name_status(const Pane &pane, std::string_view name) {
  const std::string trimmed = trim_copy(name);
  if (trimmed.empty()) {
    return "name required";
  }
  if (!trimmed.empty() && trimmed.front() == '/') {
    return "cannot start with /";
  }
  for (const Curve &curve : pane.curves) {
    if (curve.runtime_only && curve.name == trimmed) {
      return "updates existing curve";
    }
  }
  return "new curve";
}

bool apply_custom_series_editor(AppSession *session, UiState *state) {
  WorkspaceTab *tab = active_tab(&session->layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) {
    state->status_text = "No active pane";
    return false;
  }
  if (tab_state->active_pane_index < 0 || tab_state->active_pane_index >= static_cast<int>(tab->panes.size())) {
    state->status_text = "No active pane";
    return false;
  }

  CustomSeriesEditorState &editor = state->custom_series;
  editor.name = trim_copy(editor.name);
  editor.linked_source = trim_copy(editor.linked_source);
  for (std::string &path : editor.additional_sources) {
    path = trim_copy(path);
  }
  editor.additional_sources.erase(
    std::remove_if(editor.additional_sources.begin(), editor.additional_sources.end(),
                   [&](const std::string &path) { return path.empty() || path == editor.linked_source; }),
    editor.additional_sources.end());

  if (editor.name.empty()) {
    state->error_text = "Custom series name is required.";
    state->open_error_popup = true;
    return false;
  }
  if (!editor.name.empty() && editor.name.front() == '/') {
    state->error_text = "Custom series names may not start with '/'.";
    state->open_error_popup = true;
    return false;
  }

  CustomPythonSeries spec = {
    .linked_source = editor.linked_source,
    .additional_sources = editor.additional_sources,
    .globals_code = editor.globals_code,
    .function_code = editor.function_code,
  };

  try {
    PythonEvalResult result = evaluate_custom_python_series(*session, spec);
    Pane &pane = tab->panes[static_cast<size_t>(tab_state->active_pane_index)];
    editor.preview_label = editor.name;
    editor.preview_xs = result.xs;
    editor.preview_ys = result.ys;
    editor.preview_is_result = true;
    const bool inserted = upsert_custom_curve_in_pane(tab,
                                                      tab_state->active_pane_index,
                                                      make_custom_curve(pane, editor.name, spec, std::move(result)));
    state->status_text = inserted ? "Created custom series " + editor.name
                                  : "Updated custom series " + editor.name;
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Custom series failed";
    return false;
  }
}

void draw_custom_series_editor(AppSession *session, UiState *state) {
  CustomSeriesEditorState &editor = state->custom_series;
  if (!editor.open) {
    return;
  }

  WorkspaceTab *tab = active_tab(&session->layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  Pane *active_pane = (tab != nullptr && tab_state != nullptr
                       && tab_state->active_pane_index >= 0
                       && tab_state->active_pane_index < static_cast<int>(tab->panes.size()))
    ? &tab->panes[static_cast<size_t>(tab_state->active_pane_index)]
    : nullptr;
  if (editor.focus_name && active_pane != nullptr && editor.name.empty()) {
    editor.name = next_custom_curve_name(*active_pane);
  }

  draw_custom_series_help_popup(&editor);

  if (ImGui::BeginTabBar("##custom_series_tabs")) {
    if (ImGui::BeginTabItem("Single Function")) {
      const float footer_height = ImGui::GetFrameHeightWithSpacing() + 18.0f;
      const float preview_height = std::max(200.0f, ImGui::GetContentRegionAvail().y * 0.28f);
      if (ImGui::BeginChild("##custom_series_preview_child", ImVec2(0.0f, preview_height), true)) {
        draw_custom_series_preview(*session, &editor);
      }
      ImGui::EndChild();
      ImGui::Spacing();

      const float editor_height = std::max(1.0f, ImGui::GetContentRegionAvail().y - footer_height);
      if (ImGui::BeginTable("##custom_series_editor_table",
                            2,
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
                            ImVec2(0.0f, editor_height))) {
        ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, 320.0f);
        ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##custom_series_left", ImVec2(0.0f, 0.0f), false)) {
          ImGui::TextWrapped("Input timeseries. Provides arguments time and value:");
          ImGui::SetNextItemWidth(-FLT_MIN);
          input_text_string("##custom_linked_source", &editor.linked_source, ImGuiInputTextFlags_ReadOnly);
          if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JOTP_BROWSER_PATH")) {
              editor.linked_source = static_cast<const char *>(payload->Data);
              editor.additional_sources.erase(
                std::remove(editor.additional_sources.begin(), editor.additional_sources.end(), editor.linked_source),
                editor.additional_sources.end());
              editor.preview_is_result = false;
            }
            ImGui::EndDragDropTarget();
          }
          if (ImGui::Button("Use Selected", ImVec2(120.0f, 0.0f)) && !state->selected_browser_path.empty()) {
            editor.linked_source = state->selected_browser_path;
            editor.additional_sources.erase(
              std::remove(editor.additional_sources.begin(), editor.additional_sources.end(), editor.linked_source),
              editor.additional_sources.end());
            editor.preview_is_result = false;
          }
          ImGui::SameLine();
          if (ImGui::Button("Clear", ImVec2(120.0f, 0.0f))) {
            editor.linked_source.clear();
            editor.preview_is_result = false;
          }

          ImGui::Spacing();
          ImGui::TextUnformatted("Additional source timeseries:");
          ImGui::SameLine();
          ImGui::BeginDisabled(editor.selected_additional_source < 0
                               || editor.selected_additional_source >= static_cast<int>(editor.additional_sources.size()));
          if (ImGui::Button("Remove Selected", ImVec2(140.0f, 0.0f))
              && editor.selected_additional_source >= 0
              && editor.selected_additional_source < static_cast<int>(editor.additional_sources.size())) {
            editor.additional_sources.erase(editor.additional_sources.begin()
              + static_cast<std::ptrdiff_t>(editor.selected_additional_source));
            editor.selected_additional_source = editor.additional_sources.empty()
              ? -1
              : std::clamp(editor.selected_additional_source, 0,
                           static_cast<int>(editor.additional_sources.size()) - 1);
            editor.preview_is_result = false;
          }
          ImGui::EndDisabled();

          const float additional_height = 156.0f;
          if (ImGui::BeginChild("##custom_additional_sources", ImVec2(0.0f, additional_height), true)) {
            if (ImGui::BeginTable("##custom_additional_table",
                                  2,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 42.0f);
              ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch);
              for (size_t i = 0; i < editor.additional_sources.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("v%zu", i + 1);
                ImGui::TableNextColumn();
                const bool selected = editor.selected_additional_source == static_cast<int>(i);
                if (ImGui::Selectable(editor.additional_sources[i].c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                  editor.selected_additional_source = static_cast<int>(i);
                }
              }
              ImGui::EndTable();
            }
            if (ImGui::BeginDragDropTarget()) {
              if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JOTP_BROWSER_PATH")) {
                if (add_additional_source(&editor, static_cast<const char *>(payload->Data))) {
                  editor.preview_is_result = false;
                }
              }
              ImGui::EndDragDropTarget();
            }
          }
          ImGui::EndChild();
          if (ImGui::Button("Add Selected", ImVec2(120.0f, 0.0f))) {
            for (const std::string &path : state->selected_browser_paths) {
              if (add_additional_source(&editor, path)) {
                editor.preview_is_result = false;
              }
            }
          }

          ImGui::Spacing();
          ImGui::SeparatorText("Function library");
          const auto &templates = custom_series_templates();
          if (ImGui::BeginChild("##custom_series_template_list", ImVec2(0.0f, 132.0f), true)) {
            for (size_t i = 0; i < templates.size(); ++i) {
              const bool selected = editor.selected_template == static_cast<int>(i);
              if (ImGui::Selectable(templates[i].name, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                editor.selected_template = static_cast<int>(i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                  editor.globals_code = templates[i].globals_code;
                  editor.function_code = templates[i].function_code;
                  editor.preview_is_result = false;
                }
              }
            }
          }
          ImGui::EndChild();
          if (ImGui::Button("Use Selected Example", ImVec2(0.0f, 0.0f))) {
            const CustomSeriesTemplate &selected = templates[static_cast<size_t>(std::clamp(editor.selected_template, 0,
              static_cast<int>(templates.size()) - 1))];
            editor.globals_code = selected.globals_code;
            editor.function_code = selected.function_code;
            editor.preview_is_result = false;
          }
          ImGui::Spacing();
          ImGui::TextDisabled("Preview");
          ImGui::BeginChild("##custom_series_template_preview", ImVec2(0.0f, 0.0f), true);
          const CustomSeriesTemplate &selected = templates[static_cast<size_t>(std::clamp(editor.selected_template, 0,
            static_cast<int>(templates.size()) - 1))];
          ImGui::TextUnformatted(selected.preview_text);
          ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##custom_series_right", ImVec2(0.0f, 0.0f), false)) {
          const std::string name_status = active_pane != nullptr ? custom_series_name_status(*active_pane, editor.name)
                                                                 : std::string("no active pane");
          ImGui::TextUnformatted("New name:");
          ImGui::SameLine();
          if (name_status == "name required" || name_status == "cannot start with /") {
            ImGui::TextColored(color_rgb(200, 72, 64), "%s", name_status.c_str());
          } else {
            ImGui::TextColored(color_rgb(58, 126, 73), "%s", name_status.c_str());
          }
          if (editor.focus_name) {
            ImGui::SetKeyboardFocusHere();
            editor.focus_name = false;
          }
          ImGui::SetNextItemWidth(-FLT_MIN);
          input_text_string("##custom_series_name", &editor.name, ImGuiInputTextFlags_AutoSelectAll);

          ImGui::Spacing();
          ImGui::SeparatorText("Global variables");
          ImGui::SameLine();
          if (ImGui::SmallButton("Help")) {
            editor.open_help = true;
          }
          const float globals_height = std::max(96.0f, ImGui::GetContentRegionAvail().y * 0.28f);
          if (input_text_multiline_string("##custom_series_globals",
                                          &editor.globals_code,
                                          ImVec2(-FLT_MIN, globals_height),
                                          ImGuiInputTextFlags_AllowTabInput)) {
            editor.preview_is_result = false;
          }

          ImGui::Spacing();
          ImGui::TextUnformatted("def calc(time, value):");
          const float function_height = std::max(180.0f, ImGui::GetContentRegionAvail().y - 16.0f);
          if (input_text_multiline_string("##custom_series_function",
                                          &editor.function_code,
                                          ImVec2(-FLT_MIN, function_height),
                                          ImGuiInputTextFlags_AllowTabInput)) {
            editor.preview_is_result = false;
          }
        }
        ImGui::EndChild();
        ImGui::EndTable();
      }

      ImGui::Spacing();
      if (ImGui::Button("New", ImVec2(120.0f, 0.0f))) {
        reset_custom_series_editor(&editor);
        if (!state->selected_browser_path.empty()) {
          editor.linked_source = state->selected_browser_path;
        }
        editor.open = true;
        editor.focus_name = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f))) {
        apply_custom_series_editor(session, state);
      }
      ImGui::SameLine();
      if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
        editor.open = false;
        editor.request_select = false;
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Batch Functions")) {
      ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(110, 118, 128));
      ImGui::TextWrapped("Batch functions are not implemented yet. The single-function editor uses Python and "
                         "matches the current scope from NOTES.md.");
      ImGui::PopStyleColor();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

std::optional<PaneMenuAction> draw_pane_context_menu(const WorkspaceTab &tab, int pane_index) {
  if (!ImGui::BeginPopupContextWindow("##pane_context")) {
    return std::nullopt;
  }

  PaneMenuAction action;
  action.pane_index = pane_index;
  const bool has_curves = pane_index >= 0
    && pane_index < static_cast<int>(tab.panes.size())
    && !tab.panes[static_cast<size_t>(pane_index)].curves.empty();
  bootstrap_icons::menu_item("sliders", "Edit Axis Limits...", nullptr, false, false);
  bootstrap_icons::menu_item("palette", "Edit Curve Style...", nullptr, false, false);
  if (bootstrap_icons::menu_item("plus-slash-minus", "Apply filter to data...", nullptr, false, has_curves)) {
    action.kind = PaneMenuActionKind::OpenCustomSeries;
  }
  ImGui::Separator();
  if (action.kind == PaneMenuActionKind::None && bootstrap_icons::menu_item("distribute-vertical", "Split Left / Right")) {
    action.kind = PaneMenuActionKind::SplitLeft;
  } else if (action.kind == PaneMenuActionKind::None
             && bootstrap_icons::menu_item("distribute-horizontal", "Split Top / Bottom")) {
    action.kind = PaneMenuActionKind::SplitTop;
  }
  ImGui::Separator();
  if (bootstrap_icons::menu_item("zoom-out", "Zoom Out")) {
    action.kind = PaneMenuActionKind::ResetView;
  } else if (bootstrap_icons::menu_item("arrow-left-right", "Zoom Out Horizontally")) {
    action.kind = PaneMenuActionKind::ResetView;
  }
  bootstrap_icons::menu_item("arrow-down-up", "Zoom Out Vertically", nullptr, false, false);
  ImGui::Separator();
  if (bootstrap_icons::menu_item("trash", "Remove ALL curves")) {
    action.kind = PaneMenuActionKind::Clear;
  }
  ImGui::Separator();
  bootstrap_icons::menu_item("arrow-left-right", "Flip Horizontal Axis", nullptr, false, false);
  bootstrap_icons::menu_item("arrow-down-up", "Flip Vertical Axis", nullptr, false, false);
  ImGui::Separator();
  bootstrap_icons::menu_item("files", "Copy", nullptr, false, false);
  bootstrap_icons::menu_item("clipboard2", "Paste", nullptr, false, false);
  bootstrap_icons::menu_item("file-earmark-image", "Copy image to clipboard", nullptr, false, false);
  bootstrap_icons::menu_item("save", "Save plot to file", nullptr, false, false);
  bootstrap_icons::menu_item("bar-chart", "Show data statistics", nullptr, false, false);
  ImGui::Separator();
  if (bootstrap_icons::menu_item("x-square", "Close Pane")) {
    action.kind = PaneMenuActionKind::Close;
  }
  ImGui::EndPopup();
  if (action.kind == PaneMenuActionKind::None) {
    return std::nullopt;
  }
  return action;
}

std::optional<PaneDropAction> draw_pane_drop_target(int tab_index, int pane_index) {
  if (ImGui::GetDragDropPayload() == nullptr) {
    return std::nullopt;
  }

  const ImVec2 window_pos = ImGui::GetWindowPos();
  const ImVec2 content_min = ImGui::GetWindowContentRegionMin();
  const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
  ImRect content_rect(ImVec2(window_pos.x + content_min.x, window_pos.y + content_min.y),
                      ImVec2(window_pos.x + content_max.x, window_pos.y + content_max.y));
  content_rect.Expand(ImVec2(-6.0f, -6.0f));
  if (content_rect.GetWidth() < 60.0f || content_rect.GetHeight() < 60.0f) {
    return std::nullopt;
  }

  const float edge_w = std::min(90.0f, content_rect.GetWidth() * 0.24f);
  const float edge_h = std::min(72.0f, content_rect.GetHeight() * 0.24f);
  struct ZoneRect {
    PaneDropZone zone;
    ImRect rect;
  };
  const std::array<ZoneRect, 5> zones = {{
    {PaneDropZone::Left, ImRect(content_rect.Min, ImVec2(content_rect.Min.x + edge_w, content_rect.Max.y))},
    {PaneDropZone::Right, ImRect(ImVec2(content_rect.Max.x - edge_w, content_rect.Min.y), content_rect.Max)},
    {PaneDropZone::Top, ImRect(content_rect.Min, ImVec2(content_rect.Max.x, content_rect.Min.y + edge_h))},
    {PaneDropZone::Bottom, ImRect(ImVec2(content_rect.Min.x, content_rect.Max.y - edge_h), content_rect.Max)},
    {PaneDropZone::Center, ImRect(ImVec2(content_rect.Min.x + edge_w, content_rect.Min.y + edge_h),
                                  ImVec2(content_rect.Max.x - edge_w, content_rect.Max.y - edge_h))},
  }};

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  for (const ZoneRect &zone : zones) {
    if (zone.rect.GetWidth() <= 0.0f || zone.rect.GetHeight() <= 0.0f) {
      continue;
    }

    ImGui::PushID(static_cast<int>(zone.zone) * 1000 + pane_index + tab_index * 100);
    ImGui::SetCursorScreenPos(zone.rect.Min);
    ImGui::InvisibleButton("##drop_zone", zone.rect.GetSize());
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("JOTP_BROWSER_PATH", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        if (payload->Preview) {
          draw_list->AddRectFilled(zone.rect.Min, zone.rect.Max, IM_COL32(70, 130, 220, 55));
          draw_list->AddRect(zone.rect.Min, zone.rect.Max, IM_COL32(45, 95, 175, 220), 0.0f, 0, 2.0f);
        }
        if (payload->Delivery) {
          PaneDropAction action;
          action.zone = zone.zone;
          action.target_pane_index = pane_index;
          action.from_browser = true;
          action.browser_path = static_cast<const char *>(payload->Data);
          ImGui::EndDragDropTarget();
          ImGui::PopID();
          return action;
        }
      }
      if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("JOTP_PANE_CURVE", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        if (payload->Preview) {
          draw_list->AddRectFilled(zone.rect.Min, zone.rect.Max, IM_COL32(70, 130, 220, 55));
          draw_list->AddRect(zone.rect.Min, zone.rect.Max, IM_COL32(45, 95, 175, 220), 0.0f, 0, 2.0f);
        }
        if (payload->Delivery) {
          PaneDropAction action;
          action.zone = zone.zone;
          action.target_pane_index = pane_index;
          action.curve_ref = *static_cast<const PaneCurveDragPayload *>(payload->Data);
          ImGui::EndDragDropTarget();
          ImGui::PopID();
          return action;
        }
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
  }
  return std::nullopt;
}

bool apply_pane_menu_action(AppSession *session, UiState *state, int pane_index,
                            const PaneMenuAction &action) {
  WorkspaceTab *tab = active_tab(&session->layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) {
    return false;
  }

  const int original_pane_count = static_cast<int>(tab->panes.size());
  bool dock_changed = false;
  bool layout_changed = false;
  switch (action.kind) {
    case PaneMenuActionKind::OpenCustomSeries:
      tab_state->active_pane_index = pane_index;
      open_custom_series_editor(state, preferred_custom_series_source(tab->panes[static_cast<size_t>(pane_index)]));
      state->status_text = "Custom series editor opened";
      return true;
    case PaneMenuActionKind::SplitLeft:
      if (split_pane(tab, pane_index, PaneDropZone::Left)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
        dock_changed = true;
        layout_changed = true;
      }
      break;
    case PaneMenuActionKind::SplitRight:
      if (split_pane(tab, pane_index, PaneDropZone::Right)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
        dock_changed = true;
        layout_changed = true;
      }
      break;
    case PaneMenuActionKind::SplitTop:
      if (split_pane(tab, pane_index, PaneDropZone::Top)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
        dock_changed = true;
        layout_changed = true;
      }
      break;
    case PaneMenuActionKind::SplitBottom:
      if (split_pane(tab, pane_index, PaneDropZone::Bottom)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
        dock_changed = true;
        layout_changed = true;
      }
      break;
    case PaneMenuActionKind::ResetView:
      reset_shared_range(state, *session);
      state->follow_latest = false;
      state->suppress_range_side_effects = true;
      state->status_text = "Plot view reset";
      break;
    case PaneMenuActionKind::Clear:
      clear_pane(tab, pane_index);
      tab_state->active_pane_index = pane_index;
      layout_changed = true;
      break;
    case PaneMenuActionKind::Close:
      if (close_pane(tab, pane_index)) {
        tab_state->active_pane_index = std::clamp(pane_index, 0, static_cast<int>(tab->panes.size()) - 1);
        layout_changed = true;
        dock_changed = static_cast<int>(tab->panes.size()) != original_pane_count;
      }
      break;
    case PaneMenuActionKind::None:
      return false;
  }

  if (dock_changed) {
    mark_tab_dock_dirty(state, state->active_tab_index);
  }
  bool autosave_ok = true;
  if (layout_changed) {
    autosave_ok = mark_layout_dirty(session, state);
  }
  if (autosave_ok) {
    state->status_text = "Workspace updated";
  }
  return true;
}

bool apply_pane_drop_action(AppSession *session, UiState *state, const PaneDropAction &action) {
  WorkspaceTab *tab = active_tab(&session->layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) {
    return false;
  }

  if (action.from_browser) {
    if (action.zone == PaneDropZone::Center) {
      const bool ok = add_path_curve_to_pane(session, state, action.target_pane_index, action.browser_path);
      if (ok) {
        tab_state->active_pane_index = action.target_pane_index;
      }
      return ok;
    }
    Pane &target = tab->panes[static_cast<size_t>(action.target_pane_index)];
    Curve curve = make_curve_for_path(target, action.browser_path);
    if (split_pane(tab, action.target_pane_index, action.zone, curve)) {
      tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      mark_tab_dock_dirty(state, state->active_tab_index);
      if (mark_layout_dirty(session, state)) {
        state->status_text = "Split pane and added " + action.browser_path;
      }
      return true;
    }
    return false;
  }

  if (action.curve_ref.tab_index < 0
      || action.curve_ref.tab_index >= static_cast<int>(session->layout.tabs.size())) {
    return false;
  }
  WorkspaceTab &source_tab = session->layout.tabs[static_cast<size_t>(action.curve_ref.tab_index)];
  if (action.curve_ref.pane_index < 0
      || action.curve_ref.pane_index >= static_cast<int>(source_tab.panes.size())) {
    return false;
  }
  const Pane &source_pane = source_tab.panes[static_cast<size_t>(action.curve_ref.pane_index)];
  if (action.curve_ref.curve_index < 0
      || action.curve_ref.curve_index >= static_cast<int>(source_pane.curves.size())) {
    return false;
  }
  const Curve curve = source_pane.curves[static_cast<size_t>(action.curve_ref.curve_index)];

  if (action.zone == PaneDropZone::Center) {
    const bool inserted = copy_curve_to_pane(tab, action.target_pane_index, curve);
    tab_state->active_pane_index = action.target_pane_index;
    if (inserted) {
      if (mark_layout_dirty(session, state)) {
        state->status_text = "Added " + curve_display_name(curve);
      }
    } else {
      state->status_text = "Curve already present";
    }
    return true;
  }
  if (split_pane(tab, action.target_pane_index, action.zone, curve)) {
    tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
    mark_tab_dock_dirty(state, state->active_tab_index);
    if (mark_layout_dirty(session, state)) {
      state->status_text = "Split pane and added " + curve_display_name(curve);
    }
    return true;
  }
  return false;
}

ImGuiDir dock_direction(SplitOrientation orientation) {
  return orientation == SplitOrientation::Horizontal ? ImGuiDir_Left : ImGuiDir_Up;
}

void build_dock_tree(const WorkspaceNode &node, const WorkspaceTab &tab, int tab_runtime_id, ImGuiID dock_id) {
  if (node.is_pane) {
    if (node.pane_index >= 0 && node.pane_index < static_cast<int>(tab.panes.size())) {
      ImGui::DockBuilderDockWindow(
        pane_window_name(tab_runtime_id, node.pane_index, tab.panes[static_cast<size_t>(node.pane_index)]).c_str(),
        dock_id);
      if (ImGuiDockNode *dock_node = ImGui::DockBuilderGetNode(dock_id); dock_node != nullptr) {
        dock_node->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar |
                                 ImGuiDockNodeFlags_NoWindowMenuButton |
                                 ImGuiDockNodeFlags_NoCloseButton;
      }
    }
    return;
  }
  if (node.children.empty()) {
    return;
  }
  if (node.children.size() == 1) {
    build_dock_tree(node.children.front(), tab, tab_runtime_id, dock_id);
    return;
  }

  float remaining = 1.0f;
  ImGuiID current = dock_id;
  for (size_t i = 0; i + 1 < node.children.size(); ++i) {
    const float child_size = i < node.sizes.size() ? node.sizes[i] : 0.0f;
    const float ratio = remaining <= 0.0f ? 0.5f : std::clamp(child_size / remaining, 0.05f, 0.95f);
    ImGuiID child_id = 0;
    ImGuiID remainder_id = 0;
    ImGui::DockBuilderSplitNode(current, dock_direction(node.orientation), ratio, &child_id, &remainder_id);
    build_dock_tree(node.children[i], tab, tab_runtime_id, child_id);
    current = remainder_id;
    remaining = std::max(0.0f, remaining - child_size);
  }
  build_dock_tree(node.children.back(), tab, tab_runtime_id, current);
}

void ensure_dockspace(const WorkspaceTab &tab, TabUiState *tab_state, ImVec2 dockspace_size) {
  if (dockspace_size.x <= 0.0f || dockspace_size.y <= 0.0f || tab_state == nullptr) {
    return;
  }
  if (!tab_state->dock_needs_build) {
    return;
  }

  const ImGuiID dockspace_id = dockspace_id_for_tab(tab_state->runtime_id);
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
  ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);
  build_dock_tree(tab.root, tab, tab_state->runtime_id, dockspace_id);
  ImGui::DockBuilderFinish(dockspace_id);
  tab_state->dock_needs_build = false;
}

void draw_pane_windows(AppSession *session, UiState *state) {
  WorkspaceTab *tab = active_tab(&session->layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) {
    return;
  }

  for (size_t i = 0; i < tab->panes.size(); ++i) {
    Pane &pane = tab->panes[i];
    std::optional<PaneMenuAction> menu_action;
    std::optional<PaneDropAction> drop_action;
    bool close_pane_requested = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(250, 250, 251));
    ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(194, 198, 204));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, color_rgb(252, 252, 253));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, color_rgb(252, 252, 253));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, color_rgb(252, 252, 253));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    const std::string window_name = pane_window_name(tab_state->runtime_id, static_cast<int>(i), pane);
    const bool opened = ImGui::Begin(window_name.c_str(), nullptr, flags);
    if (opened) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
          || (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(0))) {
        tab_state->active_pane_index = static_cast<int>(i);
      }
      close_pane_requested = draw_pane_close_button_overlay();
      draw_plot(*session, &pane, state);
      menu_action = draw_pane_context_menu(*tab, static_cast<int>(i));
      drop_action = draw_pane_drop_target(state->active_tab_index, static_cast<int>(i));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    if (menu_action.has_value() && apply_pane_menu_action(session, state, static_cast<int>(i), *menu_action)) {
      return;
    }
    if (close_pane_requested) {
      PaneMenuAction action;
      action.kind = PaneMenuActionKind::Close;
      action.pane_index = static_cast<int>(i);
      if (apply_pane_menu_action(session, state, static_cast<int>(i), action)) {
        return;
      }
    }
    if (drop_action.has_value() && apply_pane_drop_action(session, state, *drop_action)) {
      return;
    }
  }
}

void draw_workspace(AppSession *session, const UiMetrics &ui, UiState *state) {
  state->custom_series.selected = false;
  ImGui::SetNextWindowPos(ImVec2(ui.content_x, ui.content_y));
  ImGui::SetNextWindowSize(ImVec2(ui.content_w, ui.content_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(244, 246, 248));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(186, 191, 198));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse;
  if (ImGui::Begin("##workspace_host", nullptr, flags)) {
    const int selection_request = state->requested_tab_index;
    std::optional<ImRect> rename_tab_rect;
    if (ImGui::BeginTabBar("##layout_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
      enum class TabActionKind {
        None,
        New,
        Rename,
        Duplicate,
        Close,
      };
      TabActionKind pending_action = TabActionKind::None;
      int pending_tab_index = -1;
      bool custom_series_tab_open = state->custom_series.open;
      for (size_t i = 0; i < session->layout.tabs.size(); ++i) {
        const WorkspaceTab &tab = session->layout.tabs[i];
        const TabUiState &tab_ui = state->tabs[i];
        ImGuiTabItemFlags tab_flags = ImGuiTabItemFlags_None;
        if (static_cast<int>(i) == selection_request) {
          tab_flags |= ImGuiTabItemFlags_SetSelected;
        }
        bool tab_open = true;
        const bool opened = ImGui::BeginTabItem(tab_item_label(tab, tab_ui.runtime_id).c_str(), &tab_open, tab_flags);
        if (state->rename_tab_index == static_cast<int>(i)) {
          rename_tab_rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          pending_action = TabActionKind::Rename;
          pending_tab_index = static_cast<int>(i);
        }
        if (!tab_open) {
          pending_action = TabActionKind::Close;
          pending_tab_index = static_cast<int>(i);
        }
        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::MenuItem("New Tab")) {
            pending_action = TabActionKind::New;
          }
          if (ImGui::MenuItem("Rename Tab...")) {
            pending_action = TabActionKind::Rename;
            pending_tab_index = static_cast<int>(i);
          }
          if (ImGui::MenuItem("Duplicate Tab")) {
            pending_action = TabActionKind::Duplicate;
            pending_tab_index = static_cast<int>(i);
          }
          if (ImGui::MenuItem("Close Tab")) {
            pending_action = TabActionKind::Close;
            pending_tab_index = static_cast<int>(i);
          }
          ImGui::EndPopup();
        }
        if (opened) {
          state->active_tab_index = static_cast<int>(i);
          session->layout.current_tab_index = state->active_tab_index;
          if (i < state->tabs.size()) {
            ensure_dockspace(tab, &state->tabs[i], ImGui::GetContentRegionAvail());
          }
          ImGui::DockSpace(dockspace_id_for_tab(tab_ui.runtime_id),
                           ImVec2(0.0f, 0.0f),
                           ImGuiDockNodeFlags_AutoHideTabBar |
                             ImGuiDockNodeFlags_NoWindowMenuButton |
                             ImGuiDockNodeFlags_NoCloseButton);
          ImGui::EndTabItem();
        }
      }
      if (custom_series_tab_open) {
        ImGuiTabItemFlags custom_flags = ImGuiTabItemFlags_None;
        if (state->custom_series.request_select) {
          custom_flags |= ImGuiTabItemFlags_SetSelected;
        }
        if (ImGui::BeginTabItem("Custom Series##workspace_custom_series", &custom_series_tab_open, custom_flags)) {
          state->custom_series.request_select = false;
          state->custom_series.selected = true;
          draw_custom_series_editor(session, state);
          ImGui::EndTabItem();
        }
      }
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 5.0f));
      ImGui::PushStyleColor(ImGuiCol_Tab, color_rgb(210, 217, 225));
      ImGui::PushStyleColor(ImGuiCol_TabHovered, color_rgb(224, 230, 237));
      ImGui::PushStyleColor(ImGuiCol_TabSelected, color_rgb(242, 245, 248));
      if (ImGui::TabItemButton("   ##new_tab_button", ImGuiTabItemFlags_Trailing)) {
        pending_action = TabActionKind::New;
      }
      {
        const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const ImU32 color = ImGui::GetColorU32(color_rgb(72, 79, 88));
        const ImVec2 center((rect.Min.x + rect.Max.x) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f);
        constexpr float half_extent = 6.25f;
        constexpr float thickness = 2.0f;
        draw_list->AddLine(ImVec2(center.x - half_extent, center.y),
                           ImVec2(center.x + half_extent, center.y),
                           color,
                           thickness);
        draw_list->AddLine(ImVec2(center.x, center.y - half_extent),
                           ImVec2(center.x, center.y + half_extent),
                           color,
                           thickness);
      }
      show_hover_tooltip("New Tab");
      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar();
      ImGui::EndTabBar();

      if (!custom_series_tab_open) {
        state->custom_series.open = false;
        state->custom_series.request_select = false;
      }

      if (rename_tab_rect.has_value()) {
        draw_inline_tab_editor(session, state, *rename_tab_rect);
      }

      if (state->request_new_tab || pending_action == TabActionKind::New) {
        create_runtime_tab(&session->layout, state);
        mark_layout_dirty(session, state);
        state->request_new_tab = false;
      } else if (pending_action == TabActionKind::Rename) {
        begin_rename_tab(session->layout, state, pending_tab_index);
      } else if (state->request_duplicate_tab || pending_action == TabActionKind::Duplicate) {
        if (pending_tab_index >= 0) {
          request_tab_selection(state, pending_tab_index);
        }
        duplicate_runtime_tab(&session->layout, state);
        mark_layout_dirty(session, state);
        state->request_duplicate_tab = false;
      } else if (state->request_close_tab || pending_action == TabActionKind::Close) {
        if (pending_tab_index >= 0) {
          request_tab_selection(state, pending_tab_index);
        }
        close_runtime_tab(&session->layout, state);
        mark_layout_dirty(session, state);
        state->request_close_tab = false;
      }
      if (state->requested_tab_index == selection_request) {
        state->requested_tab_index = -1;
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

bool reset_layout(AppSession *session, UiState *state) {
  try {
    if (session->layout_path.empty()) {
      session->layout = make_empty_layout();
      session->autosave_path.clear();
      state->layout_dirty = false;
      state->tabs.clear();
      cancel_rename_tab(state);
      sync_ui_state(state, session->layout);
      sync_layout_buffers(state, *session);
      reset_shared_range(state, *session);
      state->status_text = "Reset layout";
      return true;
    }
    clear_layout_autosave(*session);
    session->layout = load_sketch_layout(session->layout_path);
    state->layout_dirty = false;
    session->autosave_path = autosave_path_for_layout(session->layout_path);
    state->tabs.clear();
    cancel_rename_tab(state);
    sync_ui_state(state, session->layout);
    sync_layout_buffers(state, *session);
    reset_shared_range(state, *session);
    state->status_text = "Reset layout";
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to reset layout";
    return false;
  }
}

bool reload_layout(AppSession *session, UiState *state, const std::string &layout_arg) {
  try {
    const fs::path layout_path = resolve_layout_path(layout_arg);
    session->autosave_path = autosave_path_for_layout(layout_path);
    const bool load_draft = fs::exists(session->autosave_path);
    session->layout = load_sketch_layout(load_draft ? session->autosave_path : layout_path);
    session->layout_path = layout_path;
    state->layout_dirty = load_draft;
    cancel_rename_tab(state);
    state->tabs.clear();
    sync_ui_state(state, session->layout);
    sync_layout_buffers(state, *session);
    mark_all_docks_dirty(state);
    reset_shared_range(state, *session);
    state->status_text = std::string(load_draft ? "Loaded layout draft " : "Loaded layout ")
      + layout_path.filename().string();
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to load layout";
    return false;
  }
}

bool save_layout(AppSession *session, UiState *state, const std::string &layout_path) {
  try {
    if (layout_path.empty()) {
      throw std::runtime_error("Layout path is empty");
    }
    session->layout.current_tab_index = state->active_tab_index;
    const fs::path previous_autosave = session->autosave_path;
    const fs::path output = fs::absolute(fs::path(layout_path));
    save_layout_xml(session->layout, output);
    session->layout_path = output;
    session->autosave_path = autosave_path_for_layout(output);
    if (!previous_autosave.empty() && previous_autosave != session->autosave_path && fs::exists(previous_autosave)) {
      fs::remove(previous_autosave);
    }
    clear_layout_autosave(*session);
    state->layout_dirty = false;
    sync_layout_buffers(state, *session);
    state->status_text = "Saved layout " + output.filename().string();
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to save layout";
    return false;
  }
}

void rebuild_session_route_data(AppSession *session, UiState *state,
                                const RouteLoadProgressCallback &progress = {}) {
  apply_route_data(session, state, load_route_data(session->route_name, session->data_dir, progress));
}

void start_async_route_load(AppSession *session, UiState *state) {
  if (!session->route_loader) {
    return;
  }
  apply_route_data(session, state, RouteData{});
  session->route_loader->start(session->route_name, session->data_dir);
  state->status_text = session->route_name.empty() ? "Ready" : "Loading route " + session->route_name;
}

void poll_async_route_load(AppSession *session, UiState *state) {
  if (!session->route_loader) {
    return;
  }
  RouteData loaded_route;
  std::string error_text;
  if (!session->route_loader->consume(&loaded_route, &error_text)) {
    return;
  }
  if (!error_text.empty()) {
    state->error_text = error_text;
    state->open_error_popup = true;
    state->status_text = "Failed to load route";
    return;
  }
  apply_route_data(session, state, std::move(loaded_route));
  state->status_text = session->route_name.empty() ? "Ready" : "Loaded route " + session->route_name;
}

bool reload_session(AppSession *session, UiState *state, const std::string &route_name, const std::string &data_dir) {
  try {
    session->route_name = route_name;
    session->data_dir = data_dir;
    if (session->async_route_loading) {
      start_async_route_load(session, state);
    } else {
      rebuild_session_route_data(session, state);
      state->status_text = "Loaded route " + route_name;
    }
    sync_route_buffers(state, *session);
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to load route";
    return false;
  }
}

void draw_popups(AppSession *session, UiState *state) {
  if (state->open_open_route) {
    ImGui::OpenPopup("Open Route");
    state->open_open_route = false;
  }
  if (state->open_load_layout) {
    sync_layout_buffers(state, *session);
    ImGui::OpenPopup("Load Layout");
    state->open_load_layout = false;
  }
  if (state->open_save_layout) {
    sync_layout_buffers(state, *session);
    ImGui::OpenPopup("Save Layout");
    state->open_save_layout = false;
  }

  if (ImGui::BeginPopupModal("Open Route", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Load a route into the current layout.");
    ImGui::Separator();
    ImGui::InputText("Route", state->route_buffer.data(), state->route_buffer.size());
    ImGui::InputText("Data Dir", state->data_dir_buffer.data(), state->data_dir_buffer.size());
    ImGui::Spacing();
    if (ImGui::Button("Load", ImVec2(120.0f, 0.0f))) {
      reload_session(session, state, string_from_buffer(state->route_buffer), string_from_buffer(state->data_dir_buffer));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_route_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("Load Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Load a PlotJuggler XML layout.");
    ImGui::Separator();
    ImGui::InputText("Layout", state->load_layout_buffer.data(), state->load_layout_buffer.size());
    ImGui::Spacing();
    if (ImGui::Button("Load", ImVec2(120.0f, 0.0f))) {
      if (reload_layout(session, state, string_from_buffer(state->load_layout_buffer))) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_layout_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("Save Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save the current workspace as a PlotJuggler XML layout.");
    ImGui::Separator();
    ImGui::InputText("Layout", state->save_layout_buffer.data(), state->save_layout_buffer.size());
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
      if (save_layout(session, state, string_from_buffer(state->save_layout_buffer))) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_layout_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (state->open_error_popup) {
    ImGui::OpenPopup("Error");
    state->open_error_popup = false;
  }
  if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", state->error_text.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void render_layout(AppSession *session, UiState *state, bool show_camera_feed) {
  ensure_shared_range(state, *session);
  if (state->follow_latest) {
    update_follow_range(state);
    state->suppress_range_side_effects = true;
  } else {
    clamp_shared_range(state);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
    step_tracker(state, -1.0);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
    step_tracker(state, 1.0);
  }
  if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
    state->playback_playing = !state->playback_playing;
  }
  advance_playback(state);
  if (show_camera_feed && session->camera_feed && state->has_tracker_time) {
    session->camera_feed->update(state->tracker_time);
  }
  const float menu_height = draw_main_menu_bar(session, state);
  const UiMetrics ui = compute_ui_metrics(ImGui::GetMainViewport()->Size, menu_height, state->sidebar_width);
  state->sidebar_width = ui.sidebar_width;
  draw_sidebar(session, ui, state, show_camera_feed);
  draw_sidebar_resizer(ui, state);
  draw_workspace(session, ui, state);
  if (!state->custom_series.selected) {
    draw_pane_windows(session, state);
  }
  draw_status_bar(*session, ui, state);
  draw_popups(session, state);
}

void save_framebuffer_png(const fs::path &output_path, int width, int height) {
  ensure_parent_dir(output_path);
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("Invalid framebuffer size");
  }

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  const fs::path ppm_path = output_path.parent_path() / (output_path.stem().string() + ".ppm");
  {
    std::ofstream out(ppm_path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("Failed to open " + ppm_path.string());
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
      for (int x = 0; x < width; ++x) {
        const size_t index = static_cast<size_t>((y * width + x) * 4);
        out.write(reinterpret_cast<const char *>(&pixels[index]), 3);
      }
    }
  }

  const std::string command = "convert " + shell_quote(ppm_path.string()) + " " + shell_quote(output_path.string());
  run_or_throw(command, "image conversion");
  fs::remove(ppm_path);
}

void render_frame(GLFWwindow *window, AppSession *session, UiState *state, const fs::path *capture_path) {
  glfwPollEvents();

  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (state->request_save_layout) {
    if (session->layout_path.empty()) {
      state->open_save_layout = true;
    } else {
      save_layout(session, state, session->layout_path.string());
    }
    state->request_save_layout = false;
  }
  if (state->request_reset_layout) {
    reset_layout(session, state);
    state->request_reset_layout = false;
  }
  poll_async_route_load(session, state);

  render_layout(session, state, capture_path == nullptr);
  ImGui::Render();
  if (state->request_close) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    state->request_close = false;
  }

  glViewport(0, 0, framebuffer_width, framebuffer_height);
  glClearColor(227.0f / 255.0f, 229.0f / 255.0f, 233.0f / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if (capture_path != nullptr) {
    save_framebuffer_png(*capture_path, framebuffer_width, framebuffer_height);
  }
  glfwSwapBuffers(window);
  state->suppress_range_side_effects = false;
}

int run_app(const Options &options) {
  const fs::path layout_path = options.layout.empty() ? fs::path() : resolve_layout_path(options.layout);
  AppSession session = {
    .layout_path = layout_path,
    .autosave_path = layout_path.empty() ? fs::path() : autosave_path_for_layout(layout_path),
    .route_name = options.route_name,
    .data_dir = options.data_dir,
    .layout = options.layout.empty() ? make_empty_layout() : load_sketch_layout(layout_path),
  };
  UiState ui_state;
  if (!layout_path.empty() && !session.autosave_path.empty() && fs::exists(session.autosave_path)) {
    session.layout = load_sketch_layout(session.autosave_path);
    ui_state.layout_dirty = true;
  }
  sync_ui_state(&ui_state, session.layout);
  sync_route_buffers(&ui_state, session);
  sync_layout_buffers(&ui_state, session);

  session.async_route_loading = options.show && options.output_path.empty() && !options.sync_load;
  if (!session.async_route_loading) {
    TerminalRouteProgress route_progress(::isatty(STDERR_FILENO) != 0);
    rebuild_session_route_data(&session, &ui_state, [&](const RouteLoadProgress &update) {
      route_progress.update(update);
    });
    route_progress.finish();
  }

  GlfwRuntime glfw_runtime(options);
  ImGuiRuntime imgui_runtime(glfw_runtime.window());
  configure_style();
  session.camera_feed = std::make_unique<SidebarCameraFeed>();
  session.camera_feed->set_route_data(session.route_data);

  if (session.async_route_loading) {
    session.route_loader = std::make_unique<AsyncRouteLoader>(::isatty(STDERR_FILENO) != 0);
    start_async_route_load(&session, &ui_state);
  }

  const bool should_capture = !options.output_path.empty();
  const fs::path output_path = should_capture ? fs::path(options.output_path) : fs::path();
  int exit_code = 0;
  if (options.show) {
    bool captured = false;
    while (!glfwWindowShouldClose(glfw_runtime.window())) {
      const fs::path *capture_path = (!captured && should_capture) ? &output_path : nullptr;
      render_frame(glfw_runtime.window(), &session, &ui_state, capture_path);
      captured = captured || should_capture;
    }
  } else {
    render_frame(glfw_runtime.window(), &session, &ui_state, nullptr);
    if (should_capture) {
      render_frame(glfw_runtime.window(), &session, &ui_state, &output_path);
    }
  }
  session.camera_feed.reset();
  return exit_code;
}

}  // namespace

int run(const Options &options) {
  try {
    return run_app(options);
  } catch (const std::exception &err) {
    std::cerr << err.what() << "\n";
    return 1;
  }
}

}  // namespace jotpluggler
