#include "tools/jotpluggler/jotpluggler.h"

#include "cereal/services.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "implot.h"
#include "libyuv.h"
#include "msgq_repo/msgq/ipc.h"
#include "tools/replay/framereader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "system/camerad/cameras/nv12_info.h"


namespace {

std::string normalize_stream_address(std::string address) {
  return is_local_stream_address(address) ? "127.0.0.1" : address;
}

bool should_subscribe_stream_service(const std::string &name) {
  static const std::array<std::string_view, 13> kSkippedServices = {{
    "roadEncodeIdx",
    "driverEncodeIdx",
    "wideRoadEncodeIdx",
    "qRoadEncodeIdx",
    "roadEncodeData",
    "driverEncodeData",
    "wideRoadEncodeData",
    "qRoadEncodeData",
    "livestreamWideRoadEncodeIdx",
    "livestreamRoadEncodeIdx",
    "livestreamDriverEncodeIdx",
    "thumbnail",
    "navThumbnail",
  }};
  if (name == "rawAudioData") {
    return false;
  }
  for (std::string_view skipped : kSkippedServices) {
    if (name == skipped) {
      return false;
    }
  }
  return true;
}

void glfw_error_callback(int error, const char *description) {
  const std::string_view desc = description != nullptr ? description : "unknown";
  if (error == 65539 && desc.find("Invalid window attribute 0x0002000D") != std::string_view::npos) {
    return;
  }
  std::cerr << "GLFW error " << error << ": " << desc << "\n";
}

}  // namespace

GlfwRuntime::GlfwRuntime(const Options &options) {
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

GlfwRuntime::~GlfwRuntime() {
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
  }
  glfwTerminate();
}

GLFWwindow *GlfwRuntime::window() const {
  return window_;
}

ImGuiRuntime::ImGuiRuntime(GLFWwindow *window) {
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

ImGuiRuntime::~ImGuiRuntime() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
}

struct TerminalRouteProgress::Impl {
  explicit Impl(bool enabled) : enabled_(enabled) {}

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
           << "  Parsed " << progress.segments_parsed << "/" << progress.total_segments;
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

TerminalRouteProgress::TerminalRouteProgress(bool enabled)
  : impl_(std::make_unique<Impl>(enabled)) {}

TerminalRouteProgress::~TerminalRouteProgress() {
  finish();
}

void TerminalRouteProgress::update(const RouteLoadProgress &progress) {
  impl_->update(progress);
}

void TerminalRouteProgress::finish() {
  impl_->finish();
}

struct AsyncRouteLoader::Impl {
  explicit Impl(bool enable_terminal_progress)
      : terminal_progress(enable_terminal_progress) {}

  ~Impl() {
    join();
  }

  void start(const std::string &route_name_value, const std::string &data_dir_value, const std::string &dbc_name_value) {
    join();
    {
      std::lock_guard<std::mutex> lock(mutex);
      route_name = route_name_value;
      data_dir = data_dir_value;
      dbc_name = dbc_name_value;
      result.reset();
      error_text.clear();
    }
    active.store(!route_name_value.empty());
    completed.store(route_name_value.empty());
    success.store(route_name_value.empty());
    total_segments.store(0);
    segments_downloaded.store(0);
    segments_parsed.store(0);
    if (route_name_value.empty()) {
      return;
    }

    worker = std::thread([this]() {
      try {
        RouteData route_data = load_route_data(route_name, data_dir, dbc_name, [this](const RouteLoadProgress &progress) {
          total_segments.store(progress.total_segments > 0 ? progress.total_segments : progress.segment_count);
          segments_downloaded.store(progress.segments_downloaded);
          segments_parsed.store(progress.segments_parsed);
          terminal_progress.update(progress);
        });
        {
          std::lock_guard<std::mutex> lock(mutex);
          result = std::make_unique<RouteData>(std::move(route_data));
          error_text.clear();
        }
        success.store(true);
      } catch (const std::exception &err) {
        std::lock_guard<std::mutex> lock(mutex);
        result.reset();
        error_text = err.what();
        success.store(false);
      }
      active.store(false);
      completed.store(true);
      terminal_progress.finish();
    });
  }

  RouteLoadSnapshot snapshot() const {
    RouteLoadSnapshot snapshot;
    snapshot.active = active.load();
    snapshot.total_segments = total_segments.load();
    snapshot.segments_downloaded = segments_downloaded.load();
    snapshot.segments_parsed = segments_parsed.load();
    return snapshot;
  }

  bool consume(RouteData *route_data, std::string *error_text_out) {
    if (!completed.load()) {
      return false;
    }
    join();
    std::lock_guard<std::mutex> lock(mutex);
    completed.store(false);
    if (result) {
      *route_data = std::move(*result);
      result.reset();
      if (error_text_out != nullptr) {
        error_text_out->clear();
      }
      return true;
    }
    if (error_text_out != nullptr) {
      *error_text_out = error_text;
    }
    return true;
  }

  void join() {
    if (worker.joinable()) {
      worker.join();
    }
  }

  mutable std::mutex mutex;
  std::thread worker;
  std::unique_ptr<RouteData> result;
  std::string route_name;
  std::string data_dir;
  std::string dbc_name;
  std::string error_text;
  std::atomic<bool> active{false};
  std::atomic<bool> completed{false};
  std::atomic<bool> success{false};
  std::atomic<size_t> total_segments{0};
  std::atomic<size_t> segments_downloaded{0};
  std::atomic<size_t> segments_parsed{0};
  TerminalRouteProgress terminal_progress;
};

AsyncRouteLoader::AsyncRouteLoader(bool enable_terminal_progress)
  : impl_(std::make_unique<Impl>(enable_terminal_progress)) {}

AsyncRouteLoader::~AsyncRouteLoader() = default;

void AsyncRouteLoader::start(const std::string &route_name, const std::string &data_dir, const std::string &dbc_name) {
  impl_->start(route_name, data_dir, dbc_name);
}

RouteLoadSnapshot AsyncRouteLoader::snapshot() const {
  return impl_->snapshot();
}

bool AsyncRouteLoader::consume(RouteData *route_data, std::string *error_text) {
  return impl_->consume(route_data, error_text);
}

struct StreamPoller::Impl {
  ~Impl() {
    stop();
  }

  void start(const std::string &requested_address,
             double requested_buffer_seconds,
             const std::string &dbc_name,
             std::optional<double> time_offset) {
    stop();
    {
      std::lock_guard<std::mutex> lock(mutex);
      pending = {};
      pending_series_slots.clear();
      error_text.clear();
      address = normalize_stream_address(requested_address);
      remote = !is_local_stream_address(requested_address);
      buffer_seconds = std::max(1.0, requested_buffer_seconds);
      latest_dbc_name = dbc_name;
      latest_car_fingerprint.clear();
    }
    received_messages.store(0);
    connected.store(false);
    paused.store(false);
    running.store(true);
    worker = std::thread([this, dbc_name, time_offset]() {
      try {
        if (remote) {
          setenv("ZMQ", "1", 1);
        } else {
          unsetenv("ZMQ");
        }

        std::unique_ptr<Context> context(Context::create());
        std::unique_ptr<Poller> poller(Poller::create());
        std::vector<std::unique_ptr<SubSocket>> sockets;
        sockets.reserve(services.size());
        for (const auto &[name, info] : services) {
          if (!should_subscribe_stream_service(name)) {
            continue;
          }
          std::unique_ptr<SubSocket> socket(
            SubSocket::create(context.get(), name.c_str(), address.c_str(), false, true, info.queue_size));
          if (socket == nullptr) {
            continue;
          }
          socket->setTimeout(0);
          poller->registerSocket(socket.get());
          sockets.push_back(std::move(socket));
        }
        if (sockets.empty()) {
          throw std::runtime_error("Failed to connect to any cereal service");
        }
        connected.store(true);

        StreamAccumulator accumulator(dbc_name, time_offset);
        while (running.load()) {
          std::vector<SubSocket *> ready = poller->poll(1);
          for (SubSocket *socket : ready) {
            while (running.load()) {
              std::unique_ptr<Message> msg(socket->receive(true));
              if (!msg) {
                break;
              }
              const size_t size = msg->getSize();
              if (size < sizeof(capnp::word) || (size % sizeof(capnp::word)) != 0) {
                continue;
              }
              if (paused.load()) {
                received_messages.fetch_add(1);
                continue;
              }
              kj::ArrayPtr<const capnp::word> data(reinterpret_cast<const capnp::word *>(msg->getData()),
                                                   size / sizeof(capnp::word));
              capnp::FlatArrayMessageReader event_reader(data);
              const cereal::Event::Reader event = event_reader.getRoot<cereal::Event>();
              accumulator.append_event(event.which(), data);
              received_messages.fetch_add(1);
            }
          }

          StreamExtractBatch batch = accumulator.take_batch();
          if (!batch.series.empty() || !batch.logs.empty() || !batch.enum_info.empty()
              || !batch.car_fingerprint.empty() || !batch.dbc_name.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            merge_batch(&pending, &pending_series_slots, &batch);
            latest_dbc_name = pending.dbc_name;
            latest_car_fingerprint = pending.car_fingerprint;
          }
        }
      } catch (const std::exception &err) {
        std::lock_guard<std::mutex> lock(mutex);
        error_text = err.what();
      }
      connected.store(false);
      running.store(false);
    });
  }

  void set_paused(bool paused_value) {
    paused.store(paused_value);
    if (paused_value) {
      std::lock_guard<std::mutex> lock(mutex);
      pending = {};
      pending_series_slots.clear();
      error_text.clear();
    }
  }

  void stop() {
    running.store(false);
    paused.store(false);
    if (worker.joinable()) {
      worker.join();
    }
    connected.store(false);
  }

  StreamPollSnapshot snapshot() const {
    StreamPollSnapshot out;
    out.active = running.load();
    out.connected = connected.load();
    out.paused = paused.load();
    out.remote = remote;
    out.address = address;
    out.buffer_seconds = buffer_seconds;
    out.received_messages = received_messages.load();
    std::lock_guard<std::mutex> lock(mutex);
    out.dbc_name = latest_dbc_name;
    out.car_fingerprint = latest_car_fingerprint;
    return out;
  }

  bool consume(StreamExtractBatch *batch, std::string *out_error_text) {
    std::lock_guard<std::mutex> lock(mutex);
    const bool has_error = !error_text.empty();
    const bool has_batch = !pending.series.empty() || !pending.logs.empty() || !pending.enum_info.empty()
      || !pending.car_fingerprint.empty() || !pending.dbc_name.empty();
    if (!has_error && !has_batch) {
      return false;
    }
    if (batch != nullptr) {
      *batch = std::move(pending);
      pending = {};
      pending_series_slots.clear();
    }
    if (out_error_text != nullptr) {
      *out_error_text = error_text;
      error_text.clear();
    }
    return true;
  }

  static void merge_route_series(RouteSeries *dst, RouteSeries *src) {
    if (src->times.empty()) {
      return;
    }
    if (dst->path.empty()) {
      dst->path = src->path;
    }
    dst->times.insert(dst->times.end(), src->times.begin(), src->times.end());
    dst->values.insert(dst->values.end(), src->values.begin(), src->values.end());
  }

  static void merge_batch(StreamExtractBatch *dst,
                          std::unordered_map<std::string, size_t> *slots,
                          StreamExtractBatch *src) {
    for (RouteSeries &series : src->series) {
      auto [it, inserted] = slots->try_emplace(series.path, dst->series.size());
      if (inserted) {
        dst->series.push_back(RouteSeries{.path = series.path});
      }
      merge_route_series(&dst->series[it->second], &series);
    }
    if (!src->logs.empty()) {
      dst->logs.insert(dst->logs.end(),
                       std::make_move_iterator(src->logs.begin()),
                       std::make_move_iterator(src->logs.end()));
    }
    for (auto &[path, info] : src->enum_info) {
      dst->enum_info[path] = std::move(info);
    }
    if (!src->car_fingerprint.empty()) {
      dst->car_fingerprint = src->car_fingerprint;
    }
    if (!src->dbc_name.empty()) {
      dst->dbc_name = src->dbc_name;
    }
  }

  mutable std::mutex mutex;
  std::thread worker;
  std::atomic<bool> running{false};
  std::atomic<bool> connected{false};
  std::atomic<bool> paused{false};
  std::atomic<uint64_t> received_messages{0};
  StreamExtractBatch pending;
  std::unordered_map<std::string, size_t> pending_series_slots;
  std::string error_text;
  std::string address = "127.0.0.1";
  std::string latest_dbc_name;
  std::string latest_car_fingerprint;
  double buffer_seconds = 30.0;
  bool remote = false;
};

StreamPoller::StreamPoller()
  : impl_(std::make_unique<Impl>()) {}

StreamPoller::~StreamPoller() = default;

void StreamPoller::start(const std::string &address,
                         double buffer_seconds,
                         const std::string &dbc_name,
                         std::optional<double> time_offset) {
  impl_->start(address, buffer_seconds, dbc_name, time_offset);
}

void StreamPoller::set_paused(bool paused) {
  impl_->set_paused(paused);
}

void StreamPoller::stop() {
  impl_->stop();
}

StreamPollSnapshot StreamPoller::snapshot() const {
  return impl_->snapshot();
}

bool StreamPoller::consume(StreamExtractBatch *batch, std::string *error_text) {
  return impl_->consume(batch, error_text);
}

struct SidebarCameraFeed::Impl {
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

  Impl() {
    worker = std::thread([this]() { worker_loop(); });
  }

  ~Impl() {
    stop.store(true);
    cv.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
    destroy_texture();
  }

  void set_route_data(const RouteData &route_data) {
    destroy_texture();
    {
      std::lock_guard<std::mutex> lock(mutex);
      route_index = route_data.road_camera;
      pending_request.reset();
      pending_result.reset();
      ++route_generation;
      latest_request_serial = 0;
    }
    active_request.reset();
    displayed_request.reset();
    failed_request.reset();
    frame_width = 0;
    frame_height = 0;
    cv.notify_all();
  }

  void update(double tracker_time) {
    upload_pending_result();
    std::optional<DecodeRequest> request = request_for_time(tracker_time);
    if (!request.has_value()) {
      return;
    }
    if (same_request(active_request, request->key) || same_request(displayed_request, request->key) ||
        same_request(failed_request, request->key)) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      request->serial = ++latest_request_serial;
      request->generation = route_generation;
      pending_request = request;
    }
    active_request = request->key;
    failed_request.reset();
    cv.notify_one();
  }

  void draw(float width, bool loading) {
    const float preview_width = std::max(1.0f, width);
    const float preview_height = preview_width * preview_aspect();
    if (texture != 0) {
      ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(preview_width, preview_height));
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

  static bool same_request(const std::optional<RequestKey> &lhs, const RequestKey &rhs) {
    return lhs.has_value() && lhs->segment == rhs.segment && lhs->decode_index == rhs.decode_index;
  }

  bool has_video_source() const {
    std::lock_guard<std::mutex> lock(mutex);
    return !route_index.entries.empty() && !route_index.segment_files.empty();
  }

  float preview_aspect() const {
    if (frame_width > 0 && frame_height > 0) {
      return static_cast<float>(frame_height) / static_cast<float>(frame_width);
    }
    return kDefaultAspect;
  }

  std::optional<DecodeRequest> request_for_time(double tracker_time) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (route_index.entries.empty()) {
      return std::nullopt;
    }

    auto it = std::lower_bound(route_index.entries.begin(), route_index.entries.end(), tracker_time,
                               [](const CameraFrameIndexEntry &entry, double tm) {
                                 return entry.timestamp < tm;
                               });
    if (it == route_index.entries.end()) {
      it = std::prev(route_index.entries.end());
    } else if (it != route_index.entries.begin()) {
      const auto prev = std::prev(it);
      if (std::abs(prev->timestamp - tracker_time) <= std::abs(it->timestamp - tracker_time)) {
        it = prev;
      }
    }

    auto path_it = std::find_if(route_index.segment_files.begin(), route_index.segment_files.end(),
                                [&](const CameraSegmentFile &segment) {
                                  return segment.segment == it->segment && !segment.path.empty();
                                });
    if (path_it == route_index.segment_files.end()) {
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
      std::lock_guard<std::mutex> lock(mutex);
      if (!pending_result.has_value()) {
        return;
      }
      result = std::move(pending_result);
      pending_result.reset();
    }

    active_request.reset();
    if (!result->success || result->rgba.empty() || result->width <= 0 || result->height <= 0) {
      failed_request = result->key;
      return;
    }

    if (texture == 0) {
      glGenTextures(1, &texture);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texture_width != result->width || texture_height != result->height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result->width, result->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, result->rgba.data());
      texture_width = result->width;
      texture_height = result->height;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, result->width, result->height, GL_RGBA, GL_UNSIGNED_BYTE, result->rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    frame_width = result->width;
    frame_height = result->height;
    displayed_request = result->key;
    failed_request.reset();
  }

  void destroy_texture() {
    if (texture != 0 && glfwGetCurrentContext() != nullptr) {
      glDeleteTextures(1, &texture);
    }
    texture = 0;
    texture_width = 0;
    texture_height = 0;
    frame_width = 0;
    frame_height = 0;
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
    std::lock_guard<std::mutex> lock(mutex);
    if (!pending_request.has_value() || pending_request->serial != request.serial ||
        pending_request->generation != request.generation) {
      return;
    }
    pending_result = std::move(result);
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
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() {
          return stop.load() || (pending_request.has_value() && pending_request->serial != handled_serial);
        });
        if (stop.load()) {
          break;
        }
        request = *pending_request;
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
        if (!reader->load(RoadCam, request.path, true, &stop, true)) {
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

  mutable std::mutex mutex;
  std::condition_variable cv;
  std::thread worker;
  std::atomic<bool> stop{false};
  CameraFeedIndex route_index;
  std::optional<DecodeRequest> pending_request;
  std::optional<DecodeResult> pending_result;
  uint64_t latest_request_serial = 0;
  uint64_t route_generation = 1;
  std::optional<RequestKey> active_request;
  std::optional<RequestKey> displayed_request;
  std::optional<RequestKey> failed_request;
  GLuint texture = 0;
  int texture_width = 0;
  int texture_height = 0;
  int frame_width = 0;
  int frame_height = 0;
};

SidebarCameraFeed::SidebarCameraFeed()
  : impl_(std::make_unique<Impl>()) {}

SidebarCameraFeed::~SidebarCameraFeed() = default;

void SidebarCameraFeed::set_route_data(const RouteData &route_data) {
  impl_->set_route_data(route_data);
}

void SidebarCameraFeed::update(double tracker_time) {
  impl_->update(tracker_time);
}

void SidebarCameraFeed::draw(float width, bool loading) {
  impl_->draw(width, loading);
}

