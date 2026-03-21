#pragma once

#include "tools/jotpluggler/app.h"
#include "tools/jotpluggler/sketch_layout.h"

#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

namespace jotpluggler {

struct RouteLoadSnapshot {
  bool active = false;
  size_t total_segments = 0;
  size_t segments_downloaded = 0;
  size_t segments_parsed = 0;
};

struct StreamPollSnapshot {
  bool active = false;
  bool connected = false;
  bool paused = false;
  bool remote = false;
  std::string address;
  std::string dbc_name;
  std::string car_fingerprint;
  double buffer_seconds = 30.0;
  uint64_t received_messages = 0;
};

class GlfwRuntime {
public:
  explicit GlfwRuntime(const Options &options);
  ~GlfwRuntime();

  GlfwRuntime(const GlfwRuntime &) = delete;
  GlfwRuntime &operator=(const GlfwRuntime &) = delete;

  GLFWwindow *window() const;

private:
  GLFWwindow *window_ = nullptr;
};

class ImGuiRuntime {
public:
  explicit ImGuiRuntime(GLFWwindow *window);
  ~ImGuiRuntime();

  ImGuiRuntime(const ImGuiRuntime &) = delete;
  ImGuiRuntime &operator=(const ImGuiRuntime &) = delete;
};

class TerminalRouteProgress {
public:
  explicit TerminalRouteProgress(bool enabled);
  ~TerminalRouteProgress();

  TerminalRouteProgress(const TerminalRouteProgress &) = delete;
  TerminalRouteProgress &operator=(const TerminalRouteProgress &) = delete;

  void update(const RouteLoadProgress &progress);
  void finish();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class AsyncRouteLoader {
public:
  explicit AsyncRouteLoader(bool enable_terminal_progress);
  ~AsyncRouteLoader();

  AsyncRouteLoader(const AsyncRouteLoader &) = delete;
  AsyncRouteLoader &operator=(const AsyncRouteLoader &) = delete;

  void start(const std::string &route_name, const std::string &data_dir, const std::string &dbc_name);
  RouteLoadSnapshot snapshot() const;
  bool consume(RouteData *route_data, std::string *error_text);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class StreamPoller {
public:
  StreamPoller();
  ~StreamPoller();

  StreamPoller(const StreamPoller &) = delete;
  StreamPoller &operator=(const StreamPoller &) = delete;

  void start(const std::string &address,
             double buffer_seconds,
             const std::string &dbc_name,
             std::optional<double> time_offset = std::nullopt);
  void set_paused(bool paused);
  void stop();
  StreamPollSnapshot snapshot() const;
  bool consume(StreamExtractBatch *batch, std::string *error_text);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class SidebarCameraFeed {
public:
  SidebarCameraFeed();
  ~SidebarCameraFeed();

  SidebarCameraFeed(const SidebarCameraFeed &) = delete;
  SidebarCameraFeed &operator=(const SidebarCameraFeed &) = delete;

  void set_route_data(const RouteData &route_data);
  void update(double tracker_time);
  void draw(float width, bool loading);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace jotpluggler
