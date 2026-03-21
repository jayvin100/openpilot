#pragma once

#include "tools/jotpluggler/app.h"
#include "tools/jotpluggler/sketch_layout.h"

#include <memory>
#include <string>

struct GLFWwindow;

namespace jotpluggler {

struct RouteLoadSnapshot {
  bool active = false;
  size_t total_segments = 0;
  size_t segments_downloaded = 0;
  size_t segments_parsed = 0;
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

  void start(const std::string &route_name, const std::string &data_dir);
  RouteLoadSnapshot snapshot() const;
  bool consume(RouteData *route_data, std::string *error_text);

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
