#include "app/application.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include "core/app_state.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"
#include "ui/shell.h"
#include "ui/theme.h"
#include "tools/replay/replay.h"

Application::~Application() { shutdown(); }

static Application *s_app = nullptr;
Application *app() { return s_app; }

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
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

  if (!parseArgs(argc, argv)) return false;

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  const char *title = route_.empty() ? "Cabana" : route_.c_str();
  window = glfwCreateWindow(1600, 900, title, nullptr, nullptr);
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

  // Load route if specified
  if (!route_.empty()) {
    source_ = std::make_unique<cabana::ReplaySource>();
    if (source_->load(route_, data_dir_, replay_flags_, auto_source_)) {
      source_->start();
      auto &st = cabana::app_state();
      st.route_name = source_->routeName();
      st.car_fingerprint = source_->carFingerprint();

      // Auto-load DBC from fingerprint or CLI arg
      if (!dbc_file_.empty()) {
        cabana::dbc::dbc_manager().loadFromFile(dbc_file_);
      }
      // Fingerprint may not be available yet — will be loaded on first poll
    } else {
      source_.reset();
    }
  }

  return true;
}

int Application::run() {
  auto &st = cabana::app_state();
  constexpr double target_frame_time = 1.0 / 60.0;

  while (!glfwWindowShouldClose(window) && !st.quit_requested) {
    double frame_start = glfwGetTime();
    glfwPollEvents();

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
          cabana::dbc::dbc_manager().loadFromFingerprint(fp);
        }
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    cabana::shell::render();

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

void Application::shutdown() {
  source_.reset();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}
