#include "app/application.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <mutex>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include "core/app_state.h"
#include "core/settings.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"
#include "ui/shell.h"
#include "ui/theme.h"
#include "tools/replay/replay.h"

Application::~Application() { shutdown(); }

static Application *s_app = nullptr;
Application *app() { return s_app; }

struct ReplayLoadState {
  std::mutex lock;
  std::atomic<bool> done = false;
  std::unique_ptr<cabana::ReplaySource> source;
  std::string error;
};

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static std::string window_title(const std::string &route) {
  return route.empty() ? "Cabana" : ("Cabana - " + route);
}

bool Application::videoEnabled() const {
  return (replay_flags_ & REPLAY_FLAG_NO_VIPC) == 0;
}

bool Application::parseArgs(int argc, char *argv[]) {
  static struct option long_opts[] = {
    {"demo", no_argument, nullptr, 'd'},
    {"no-vipc", no_argument, nullptr, 'V'},
    {"qcam", no_argument, nullptr, 'q'},
    {"ecam", no_argument, nullptr, 'e'},
    {"dcam", no_argument, nullptr, 'c'},
    {"auto", no_argument, nullptr, 'a'},
    {"dbc", required_argument, nullptr, 'D'},
    {"data_dir", required_argument, nullptr, 'P'},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "h", long_opts, nullptr)) != -1) {
    switch (opt) {
      case 'd': route_ = DEMO_ROUTE; break;
      case 'V': replay_flags_ |= REPLAY_FLAG_NO_VIPC; break;
      case 'q': replay_flags_ |= REPLAY_FLAG_QCAMERA; break;
      case 'e': replay_flags_ |= REPLAY_FLAG_ECAM; break;
      case 'c': replay_flags_ |= REPLAY_FLAG_DCAM; break;
      case 'a': auto_source_ = true; break;
      case 'D': dbc_file_ = optarg; break;
      case 'P': data_dir_ = optarg; break;
      case 'h':
        fprintf(stderr, "Usage: cabana_imgui [route] [--demo] [--no-vipc] [--dbc file]\n");
        return false;
    }
  }
  if (optind < argc && route_.empty()) {
    route_ = argv[optind];
  }
  return true;
}

bool Application::init(int argc, char *argv[]) {
  s_app = this;
  shutdown_done_ = false;

  if (!parseArgs(argc, argv)) return false;

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  std::string title = window_title(route_);
  window = glfwCreateWindow(1600, 900, title.c_str(), nullptr, nullptr);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  imgui_ini_path_ = cabana::settings::imguiIniPath();
  io.IniFilename = imgui_ini_path_.c_str();

  // HiDPI
  float dpi_scale = 1.0f;
  GLFWmonitor *monitor = glfwGetPrimaryMonitor();
  if (monitor) {
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    int width_mm = 0;
    glfwGetMonitorPhysicalSize(monitor, &width_mm, nullptr);
    if (mode && width_mm > 0) {
      float actual_dpi = (float)mode->width / ((float)width_mm / 25.4f);
      dpi_scale = roundf(actual_dpi / 96.0f * 10.0f) / 10.0f;
      if (dpi_scale < 1.0f) dpi_scale = 1.0f;
    }
  }
  {
    float xs = 1, ys = 1;
    glfwGetWindowContentScale(window, &xs, &ys);
    float cs = xs > ys ? xs : ys;
    if (cs > dpi_scale) dpi_scale = cs;
  }

  cabana::theme::apply();
  cabana::theme::load_fonts(dpi_scale);
  if (dpi_scale > 1.0f) cabana::theme::apply();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  auto &st = cabana::app_state();
  cabana::settings::load(st);
  if (route_.empty() && !st.recent_routes.empty()) {
    route_ = st.recent_routes.front();
  }
  if (dbc_file_.empty() && !st.active_dbc_file.empty()) {
    dbc_file_ = st.active_dbc_file;
  }
  st.route_name.clear();
  st.car_fingerprint.clear();
  st.route_loading = false;
  st.route_load_error.clear();
  st.show_help_overlay = false;

  if (route_.empty() && !dbc_file_.empty()) {
    openDbcFile(dbc_file_);
  }

  // Kick route loading to a background thread so the first frame appears immediately.
  if (!route_.empty()) {
    beginReplayLoad();
  }

  return true;
}

int Application::run() {
  auto &st = cabana::app_state();
  constexpr double target_frame_time = 1.0 / 60.0;

  while (!glfwWindowShouldClose(window) && !st.quit_requested) {
    double frame_start = glfwGetTime();
    glfwPollEvents();
    pollReplayLoad();

    // Keyboard shortcuts
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) {
      st.quit_requested = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput) {
      if (source_) {
        source_->pause(!source_->isPaused());
      }
    }

    // Poll replay events on main thread
    if (source_) {
      source_->pollEvents();
      st.current_sec = source_->currentSec();
      st.min_sec = source_->minSec();
      st.max_sec = source_->maxSec();
      st.speed = source_->speed();
      st.paused = source_->isPaused();

      // Auto-load DBC when fingerprint becomes available
      const auto &fp = source_->carFingerprint();
      if (!fp.empty() && fp != st.car_fingerprint) {
        st.car_fingerprint = fp;
        if (!cabana::dbc::dbc_manager().dbc()) {
          if (cabana::dbc::dbc_manager().loadFromFingerprint(fp)) {
            st.rememberRecentDbc(cabana::dbc::dbc_manager().loadedName());
            dbc_file_ = cabana::dbc::dbc_manager().loadedName();
          }
        }
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    cabana::shell::render();

    if (st.settings_dirty && cabana::settings::save(st)) {
      st.settings_dirty = false;
    }

    ImGui::Render();

    int dw, dh;
    glfwGetFramebufferSize(window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);

    // Frame rate limiter — ensures ~60fps even if vsync doesn't work
    double elapsed = glfwGetTime() - frame_start;
    if (elapsed < target_frame_time) {
      std::this_thread::sleep_for(std::chrono::duration<double>(target_frame_time - elapsed));
    }
  }

  shutdown();
  return 0;
}

void Application::beginReplayLoad() {
  auto &st = cabana::app_state();
  st.route_name = route_;
  st.car_fingerprint.clear();
  st.route_loading = true;
  st.route_load_error.clear();

  auto load_state = std::make_shared<ReplayLoadState>();
  replay_load_state_ = load_state;

  const std::string route = route_;
  const std::string data_dir = data_dir_;
  const uint32_t replay_flags = replay_flags_;
  const bool auto_source = auto_source_;

  std::thread([load_state, route, data_dir, replay_flags, auto_source]() {
    auto source = std::make_unique<cabana::ReplaySource>();
    const bool ok = source->load(route, data_dir, replay_flags, auto_source);

    std::lock_guard lk(load_state->lock);
    if (ok) {
      load_state->source = std::move(source);
    } else {
      load_state->error = "Failed to load route";
    }
    load_state->done.store(true);
  }).detach();
}

void Application::pollReplayLoad() {
  if (!replay_load_state_ || !replay_load_state_->done.load()) {
    return;
  }

  auto &st = cabana::app_state();

  {
    std::lock_guard lk(replay_load_state_->lock);
    if (replay_load_state_->source) {
      source_ = std::move(replay_load_state_->source);
      source_->start();

      st.route_name = source_->routeName();
      st.car_fingerprint = source_->carFingerprint();
      st.route_load_error.clear();
      st.rememberRecentRoute(st.route_name);

      if (!dbc_file_.empty()) {
        openDbcFile(dbc_file_);
      }

      std::string title = window_title(st.route_name);
      glfwSetWindowTitle(window, title.c_str());
    } else if (!replay_load_state_->error.empty()) {
      st.route_load_error = replay_load_state_->error;
    }
  }

  st.route_loading = false;
  replay_load_state_.reset();
}

void Application::openRoute(const std::string &route) {
  if (route.empty()) return;

  auto &st = cabana::app_state();
  source_.reset();
  replay_load_state_.reset();

  route_ = route;
  st.route_name.clear();
  st.car_fingerprint.clear();
  st.route_loading = false;
  st.route_load_error.clear();
  st.current_sec = 0;
  st.min_sec = 0;
  st.max_sec = 0;
  st.paused = false;

  glfwSetWindowTitle(window, window_title(route_).c_str());
  beginReplayLoad();
}

void Application::closeRoute() {
  auto &st = cabana::app_state();
  replay_load_state_.reset();
  source_.reset();
  route_.clear();
  st.route_name.clear();
  st.car_fingerprint.clear();
  st.route_loading = false;
  st.route_load_error.clear();
  st.current_sec = 0;
  st.min_sec = 0;
  st.max_sec = 0;
  st.paused = false;
  glfwSetWindowTitle(window, window_title(std::string()).c_str());
}

bool Application::openDbcFile(const std::string &path) {
  if (path.empty()) return false;
  if (!cabana::dbc::dbc_manager().loadFromFile(path)) {
    return false;
  }

  dbc_file_ = cabana::dbc::dbc_manager().loadedName();
  auto &st = cabana::app_state();
  st.rememberRecentDbc(dbc_file_);
  return true;
}

bool Application::saveDbc() {
  auto &dbc = cabana::dbc::dbc_manager();
  if (!dbc.save()) {
    return false;
  }

  dbc_file_ = dbc.loadedName();
  cabana::app_state().rememberRecentDbc(dbc_file_);
  return true;
}

bool Application::saveDbcAs(const std::string &path) {
  if (path.empty()) return false;

  auto &dbc = cabana::dbc::dbc_manager();
  if (!dbc.saveAs(path)) {
    return false;
  }

  dbc_file_ = dbc.loadedName();
  cabana::app_state().rememberRecentDbc(dbc_file_);
  return true;
}

void Application::shutdown() {
  if (shutdown_done_) {
    return;
  }
  shutdown_done_ = true;

  replay_load_state_.reset();
  source_.reset();

  if (ImGui::GetCurrentContext()) {
    if (!imgui_ini_path_.empty()) {
      ImGui::SaveIniSettingsToDisk(imgui_ini_path_.c_str());
    }
    cabana::settings::save(cabana::app_state());
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
  }

  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}
