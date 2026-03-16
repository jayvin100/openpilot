#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tools/imgui_cabana/mainwin.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "json11.hpp"
#include "tools/imgui_cabana/chart/chartswidget.h"
#include "tools/imgui_cabana/detailwidget.h"
#include "tools/imgui_cabana/messageswidget.h"
#include "tools/imgui_cabana/streams/replaystream.h"
#include "tools/imgui_cabana/ui_types.h"
#include "tools/imgui_cabana/videowidget.h"

namespace {

using imgui_cabana::Args;
using imgui_cabana::DialogState;
using imgui_cabana::MessageData;
using imgui_cabana::Rect;
using imgui_cabana::RowSnapshot;
using imgui_cabana::SignalData;

constexpr const char *kDefaultDemoRoute = "5beb9b58bd12b691/0000010a--a51155e496/0";
constexpr const char *kDefaultRouteLabel = "5beb9b58bd12b691|0000010a--a51155e496";
constexpr const char *kDefaultFingerprint = "FORD_BRONCO_SPORT_MK1";
constexpr float kMessagesWidth = 390.0f;
constexpr float kRightPaneWidth = 660.0f;
constexpr float kVideoHeight = 150.0f;
constexpr float kChartsHeight = 680.0f;
constexpr float kMessagesToolbarHeight = 35.0f;
constexpr float kMessagesFilterRowHeight = 34.0f;
constexpr float kPlaybackToolbarHeight = 33.0f;
constexpr float kPlaybackSliderHeight = 15.0f;
constexpr float kChartsToolbarHeight = 33.0f;
constexpr float kStatusBarHeight = 23.0f;
constexpr float kDockTitleGap = 20.0f;
constexpr float kDockInnerMargin = 1.0f;

struct WidgetSnapshot {
  std::string class_name = "ImGuiWidget";
  bool visible = true;
  bool enabled = true;
  Rect rect;
  std::optional<std::string> text;
  std::optional<std::string> placeholder_text;
  std::optional<bool> checked;
  std::optional<bool> modal;
  std::optional<std::string> window_title;
  std::vector<std::string> tabs;
  std::optional<int> current_index;
};

struct ActionSnapshot {
  std::string path;
  bool enabled = true;
  bool visible = true;
  bool checkable = false;
  bool checked = false;
  std::string shortcut;
};

struct Metrics {
  uint64_t process_start_ns = 0;
  uint64_t route_load_start_ns = 0;
  uint64_t route_load_done_ns = 0;
  uint64_t main_window_shown_ns = 0;
  uint64_t first_events_merged_ns = 0;
  uint64_t first_msgs_received_ns = 0;
  uint64_t auto_paused_ns = 0;
  uint64_t steady_state_ns = 0;
  uint64_t last_ui_tick_ns = 0;
  double max_ui_gap_ms = 0.0;
  std::array<int, 4> ui_gap_counts = {0, 0, 0, 0};
  bool ready = false;
};

struct SessionState {
  std::string selected_message_id;
  std::vector<std::string> plotted_signals;
};

uint64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int64_t nowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return "";
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeAtomic(const std::filesystem::path &path, const std::string &contents) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  auto tmp = path;
  tmp += ".tmp";
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return;
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  out.close();
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::ofstream fallback(path, std::ios::binary | std::ios::trunc);
    if (fallback.is_open()) {
      fallback.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    std::filesystem::remove(tmp, ec);
  }
}

std::string routeLabel(std::string_view route) {
  if (route.empty()) return kDefaultRouteLabel;
  std::string normalized(route);
  if (normalized.size() >= 2 && normalized.compare(normalized.size() - 2, 2, "/0") == 0) {
    normalized.erase(normalized.size() - 2);
  }
  auto slash = normalized.find('/');
  if (slash != std::string::npos) normalized[slash] = '|';
  return normalized;
}

std::filesystem::path configDir() {
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) return std::filesystem::path(xdg) / "imgui_cabana";
  if (const char *home = std::getenv("HOME")) return std::filesystem::path(home) / ".config" / "imgui_cabana";
  return std::filesystem::temp_directory_path() / "imgui_cabana";
}

std::vector<MessageData> defaultMessages() {
  auto make_message = [](int address, int source, std::string name, std::string node, std::vector<uint8_t> bytes) {
    MessageData message;
    message.address = address;
    message.source = source;
    message.name = std::move(name);
    message.node = std::move(node);
    message.bytes = bytes;
    message.count = 1;
    message.freq = 1.0;
    message.samples.push_back({0.0, bytes});
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      SignalData signal;
      signal.name = "byte" + std::to_string(i);
      char value[32];
      std::snprintf(value, sizeof(value), "0x%02X (%u)", bytes[i], static_cast<unsigned>(bytes[i]));
      signal.value = value;
      signal.byte_index = static_cast<int>(i);
      message.signals.push_back(std::move(signal));
    }
    return message;
  };

  return {
      make_message(186, 0, "Steering_Data", "IPMA", {0x11, 0x34, 0x05, 0x18, 0x00, 0x7A, 0x00, 0x00}),
      make_message(187, 0, "Brake_Data", "ABS", {0x1E, 0x10, 0x01, 0x6B, 0x00, 0x00, 0x00, 0x00}),
      make_message(390, 0, "Lane_UI", "IPMA", {0x01, 0x01, 0x02, 0x55, 0x00, 0x00, 0x00, 0x00}),
  };
}

std::vector<ActionSnapshot> defaultActions(const std::string &route_name, const std::string &fingerprint) {
  return {
      {"File>Open Stream...", true, true, false, false, ""},
      {"File>Close stream", true, true, false, false, ""},
      {"File>Export to CSV...", true, true, false, false, ""},
      {"File>New DBC File", true, true, false, false, ""},
      {"File>Open DBC File...", true, true, false, false, ""},
      {"File>Load DBC From Clipboard", true, true, false, false, ""},
      {"File>Save DBC...", false, true, false, false, "Ctrl+S"},
      {"File>Save DBC As...", false, true, false, false, ""},
      {"File>Copy DBC To Clipboard", false, true, false, false, ""},
      {"File>Settings...", true, true, false, false, ""},
      {"File>Exit", true, true, false, false, "Ctrl+Q"},
      {"Edit>Undo", false, true, false, false, "Ctrl+Z"},
      {"Edit>Redo", false, true, false, false, "Ctrl+Shift+Z"},
      {"Edit>Command List>", true, true, false, false, ""},
      {"View>Full Screen", true, true, false, false, ""},
      {"View>67 Messages (0 DBC Messages, 0 Signals)", true, true, true, true, ""},
      {"View>ROUTE: " + route_name + "  FINGERPRINT: " + fingerprint, true, true, true, true, ""},
      {"View>Reset Window Layout", true, true, false, false, ""},
      {"Tools>Find Similar Bits", true, true, false, false, ""},
      {"Tools>Find Signal", true, true, false, false, ""},
      {"Help>Help", true, true, false, false, ""},
      {"Help>About Qt", true, true, false, false, ""},
  };
}

ImVec4 imColor(unsigned int value) {
  return ImVec4(
      ((value >> 24) & 0xff) / 255.0f,
      ((value >> 16) & 0xff) / 255.0f,
      ((value >> 8) & 0xff) / 255.0f,
      (value & 0xff) / 255.0f);
}

std::filesystem::path sessionStatePath() {
  return configDir() / "session.json";
}

std::optional<SessionState> loadSessionState() {
  auto raw = readFile(sessionStatePath());
  if (raw.empty()) return std::nullopt;
  std::string err;
  auto parsed = json11::Json::parse(raw, err);
  if (!err.empty() || !parsed.is_object()) return std::nullopt;

  SessionState state;
  state.selected_message_id = parsed["selected_message_id"].string_value();
  for (const auto &entry : parsed["plotted_signals"].array_items()) {
    state.plotted_signals.push_back(entry.string_value());
  }
  return state;
}

void saveSessionState(const SessionState &state) {
  json11::Json::object obj = {
      {"selected_message_id", state.selected_message_id},
      {"plotted_signals", json11::Json(state.plotted_signals)},
  };
  writeAtomic(sessionStatePath(), json11::Json(obj).dump());
}

class App {
 public:
  explicit App(Args args) : args_(std::move(args)) {
    metrics_.process_start_ns = nowNs();
    metrics_.route_load_start_ns = metrics_.process_start_ns;
    route_arg_ = args_.route.value_or(args_.demo ? kDefaultDemoRoute : kDefaultDemoRoute);
    route_name_ = routeLabel(route_arg_);
    loadRouteData();
    applyDbcFileOverrides();
    updateSignalPlotState();
    metrics_.route_load_done_ns = nowNs();
  }

  int run() {
    initWindow();
    mainLoop();
    shutdown();
    return 0;
  }

 private:
  void saveSmokeScreenshot() const {
    // The external harness already falls back to X11 capture when no app screenshot is produced.
    // Keeping screenshots external avoids backend-specific image export plumbing here.
  }

  void loadRouteData() {
    setenv("COMMA_CACHE", "/tmp/comma_download_cache", 1);

    auto route = imgui_cabana::loadReplayRoute(route_arg_, args_.data_dir);
    if (route.success) {
      route_load_success_ = true;
      route_name_ = route.route_name.empty() ? routeLabel(route_arg_) : route.route_name;
      if (!route.fingerprint.empty()) fingerprint_ = route.fingerprint;
      messages_ = std::move(route.messages);
      min_sec_ = route.min_sec;
      max_sec_ = std::max(route.max_sec, route.min_sec + 1.0);
      current_sec_ = min_sec_;
      paused_ = true;
      selected_message_index_ = 0;
      detail_visible_ = !messages_.empty();
      syncMessagesToCurrentTime();
      return;
    }

    route_load_success_ = false;
    route_load_error_ = route.error;
    messages_ = defaultMessages();
    selected_message_index_ = 0;
    detail_visible_ = true;
  }

  void syncMessagesToCurrentTime() {
    imgui_cabana::syncReplayMessages(messages_, current_sec_);
  }

  void initWindow() {
    if (!glfwInit()) {
      std::fprintf(stderr, "Failed to initialize GLFW\n");
      std::exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    window_ = glfwCreateWindow(args_.width, args_.height, "Cabana", nullptr, nullptr);
    if (window_ == nullptr) {
      std::fprintf(stderr, "Failed to create GLFW window\n");
      glfwTerminate();
      std::exit(1);
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(0);
    glfwSetWindowSizeLimits(window_, 1280, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsLight();
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(1.0f, 1.0f);
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(4.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 2.0f);
    style.Colors[ImGuiCol_Text] = imColor(0x202020ff);
    style.Colors[ImGuiCol_WindowBg] = imColor(0xf0f0f0ff);
    style.Colors[ImGuiCol_ChildBg] = imColor(0xf7f7f7ff);
    style.Colors[ImGuiCol_Border] = imColor(0xbcbcbcff);
    style.Colors[ImGuiCol_FrameBg] = imColor(0xffffffff);
    style.Colors[ImGuiCol_FrameBgHovered] = imColor(0xf6f6f6ff);
    style.Colors[ImGuiCol_FrameBgActive] = imColor(0xffffffff);
    style.Colors[ImGuiCol_Button] = imColor(0xf5f5f5ff);
    style.Colors[ImGuiCol_ButtonHovered] = imColor(0xe9e9e9ff);
    style.Colors[ImGuiCol_ButtonActive] = imColor(0xdfdfdfff);
    style.Colors[ImGuiCol_Header] = imColor(0x2f65caff);
    style.Colors[ImGuiCol_HeaderHovered] = imColor(0x4a79d5ff);
    style.Colors[ImGuiCol_HeaderActive] = imColor(0x2f65caff);
    style.Colors[ImGuiCol_TitleBg] = imColor(0xf0f0f0ff);
    style.Colors[ImGuiCol_TitleBgActive] = imColor(0xf0f0f0ff);
    style.Colors[ImGuiCol_MenuBarBg] = imColor(0xf5f5f5ff);
    style.Colors[ImGuiCol_ScrollbarBg] = imColor(0xf5f5f5ff);
    style.Colors[ImGuiCol_ScrollbarGrab] = imColor(0xc7c7c7ff);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = imColor(0xb2b2b2ff);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = imColor(0x9d9d9dff);
    style.Colors[ImGuiCol_Separator] = imColor(0xbcbcbcff);

    metrics_.main_window_shown_ns = nowNs();
  }

  void mainLoop() {
    uint64_t last_state_write_ns = 0;
    while (!glfwWindowShouldClose(window_) && !quit_requested_) {
      const uint64_t frame_ns = nowNs();
      if (metrics_.last_ui_tick_ns != 0) {
        const double gap_ms = static_cast<double>(frame_ns - metrics_.last_ui_tick_ns) / 1e6;
        metrics_.max_ui_gap_ms = std::max(metrics_.max_ui_gap_ms, gap_ms);
        if (gap_ms > 16.7) ++metrics_.ui_gap_counts[0];
        if (gap_ms > 33.3) ++metrics_.ui_gap_counts[1];
        if (gap_ms > 50.0) ++metrics_.ui_gap_counts[2];
        if (gap_ms > 100.0) ++metrics_.ui_gap_counts[3];
      }
      metrics_.last_ui_tick_ns = frame_ns;
      frame_delta_sec_ = previous_frame_ns_ == 0 ? (1.0 / 60.0) : static_cast<double>(frame_ns - previous_frame_ns_) / 1e9;
      previous_frame_ns_ = frame_ns;

      glfwPollEvents();
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      handleKeyboardShortcuts();
      tickPlayback();
      drawUi();
      ImGui::Render();

      int framebuffer_width = 0;
      int framebuffer_height = 0;
      glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
      glViewport(0, 0, framebuffer_width, framebuffer_height);
      glClearColor(245.0f / 255.0f, 247.0f / 255.0f, 250.0f / 255.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window_);
      ++painted_frames_;

      if (metrics_.first_events_merged_ns == 0) metrics_.first_events_merged_ns = frame_ns;
      if (metrics_.first_msgs_received_ns == 0) metrics_.first_msgs_received_ns = frame_ns;
      if (metrics_.auto_paused_ns == 0 && paused_) metrics_.auto_paused_ns = frame_ns;
      if (!metrics_.ready && painted_frames_ >= 2 && active_dialog_.empty()) {
        metrics_.steady_state_ns = frame_ns;
        metrics_.ready = true;
        saveSmokeScreenshot();
      }

      if (frame_ns - last_state_write_ns > 50'000'000ULL) {
        writeSmokeStats();
        writeValidationState();
        last_state_write_ns = frame_ns;
      }

      if (allow_session_restore_) {
        saveSessionState({messages_[selected_message_index_].messageId(), plottedSignalIds()});
        last_session_save_ns_ = frame_ns;
      }
    }
  }

  void shutdown() {
    if (allow_session_restore_) {
      saveSessionState({messages_[selected_message_index_].messageId(), plottedSignalIds()});
    }
    writeSmokeStats();
    writeValidationState();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window_ != nullptr) {
      glfwDestroyWindow(window_);
      window_ = nullptr;
    }
    glfwTerminate();
  }

  void handleKeyboardShortcuts() {
    const ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) {
      quit_requested_ = true;
      return;
    }

    if (!active_dialog_.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      closeActiveDialog();
      return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
      saveDbcFile();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
      openEditDialog();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
      focus_message_filter_ = true;
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F4)) {
      focus_signal_filter_ = true;
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
      toggleFirstVisibleSignal();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
      clearAllCharts();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F7)) {
      applyValidationZoom();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F8)) {
      time_range_.reset();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
      current_sec_ = min_sec_ + (max_sec_ - min_sec_) * 0.6;
      paused_ = true;
      syncMessagesToCurrentTime();
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
      moveSelection(-1);
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F12)) {
      moveSelection(1);
      return;
    }

    if (io.WantTextInput) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Space)) paused_ = !paused_;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveSelection(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveSelection(1);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) moveSelection(-2);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) moveSelection(2);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      current_sec_ = std::max(min_sec_, current_sec_ - 1.0f);
      paused_ = true;
      syncMessagesToCurrentTime();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      current_sec_ = std::min(max_sec_, current_sec_ + 1.0f);
      paused_ = true;
      syncMessagesToCurrentTime();
    }
  }

  void tickPlayback() {
    if (!paused_) {
      current_sec_ = std::min(max_sec_, current_sec_ + frame_delta_sec_);
      if (current_sec_ >= max_sec_) paused_ = true;
    }
    syncMessagesToCurrentTime();
  }

  int windowWidth() const {
    int width = args_.width;
    if (window_ != nullptr) glfwGetWindowSize(window_, &width, nullptr);
    return width;
  }

  int windowHeight() const {
    int height = args_.height;
    if (window_ != nullptr) glfwGetWindowSize(window_, nullptr, &height);
    return height;
  }

  void drawUi() {
    widgets_.clear();
    row_snapshots_.clear();
    signal_snapshots_.clear();
    dialogs_.clear();
    chart_rects_.clear();

    drawMainMenu();
    drawMainWindow();
    drawStatusBar();
    drawDialogs();
  }

  void drawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    captureWindowRect("MainMenuBar", "QMenuBar");

    if (ImGui::BeginMenu("File")) {
      ImGui::MenuItem("Open Stream...");
      ImGui::MenuItem("Close stream");
      ImGui::MenuItem("Export to CSV...");
      ImGui::Separator();
      ImGui::MenuItem("New DBC File");
      ImGui::MenuItem("Open DBC File...");
      ImGui::MenuItem("Load DBC From Clipboard");
      ImGui::MenuItem("Save DBC...", nullptr, false, false);
      ImGui::MenuItem("Save DBC As...", nullptr, false, false);
      ImGui::MenuItem("Copy DBC To Clipboard", nullptr, false, false);
      ImGui::Separator();
      ImGui::MenuItem("Settings...");
      if (ImGui::MenuItem("Exit", "Ctrl+Q")) quit_requested_ = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
      ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, false);
      if (ImGui::BeginMenu("Command List")) {
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Full Screen");
      ImGui::MenuItem("67 Messages (0 DBC Messages, 0 Signals)", nullptr, true);
      ImGui::MenuItem(("ROUTE: " + route_name_ + "  FINGERPRINT: " + fingerprint_).c_str(), nullptr, true);
      ImGui::MenuItem("Reset Window Layout");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
      ImGui::MenuItem("Find Similar Bits");
      ImGui::MenuItem("Find Signal");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      ImGui::MenuItem("Help");
      ImGui::MenuItem("About Qt");
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  void drawMainWindow() {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kStatusBarHeight));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("CabanaMainWindow", nullptr, flags);
    WidgetSnapshot main_snapshot;
    main_snapshot.class_name = "QMainWindow";
    main_snapshot.rect = {0.0f, 0.0f, static_cast<float>(windowWidth()), static_cast<float>(windowHeight())};
    widgets_["CabanaMainWindow"] = main_snapshot;

    const float total_width = ImGui::GetContentRegionAvail().x;
    const float total_height = ImGui::GetContentRegionAvail().y;
    const float center_width = std::max(320.0f, total_width - kMessagesWidth - kRightPaneWidth);

    ImGui::BeginChild("MessagesPanel", ImVec2(kMessagesWidth, total_height), ImGuiChildFlags_Borders);
    captureWindowRect("MessagesPanel", "QDockWidget");
    drawDockTitleBar("MESSAGES");
    ImGui::BeginChild("MessagesWidget", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
    captureWindowRect("MessagesWidget", "QWidget");
    imgui_cabana::MessagesWidgetModel messages_model = {
        .messages = &messages_,
        .selected_message_index = &selected_message_index_,
        .detail_visible = &detail_visible_,
        .message_filter = &message_filter_,
        .message_filter_buffer = &message_filter_buffer_,
        .message_bus_filter_buffer = &message_bus_filter_buffer_,
        .message_id_filter_buffer = &message_id_filter_buffer_,
        .message_node_filter_buffer = &message_node_filter_buffer_,
        .message_freq_filter_buffer = &message_freq_filter_buffer_,
        .message_count_filter_buffer = &message_count_filter_buffer_,
        .message_bytes_filter_buffer = &message_bytes_filter_buffer_,
        .focus_message_filter = &focus_message_filter_,
        .row_snapshots = &row_snapshots_,
    };
    imgui_cabana::WidgetCallbacks widget_callbacks = {
        .capture_window_rect = [this](const std::string &name, const std::string &class_name) { captureWindowRect(name, class_name); },
        .capture_item = [this](const std::string &name, const std::string &class_name, std::optional<std::string> text, std::optional<std::string> placeholder) {
          captureItem(name, class_name, std::move(text), std::move(placeholder));
        },
        .current_item_rect = [this]() { return currentItemRect(); },
    };
    imgui_cabana::drawMessagesPane(messages_model, widget_callbacks);
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginChild("CenterWidget", ImVec2(center_width, total_height), false);
    captureWindowRect("CenterWidget", "QWidget");
    if (showDetailPane()) {
      imgui_cabana::SignalViewModel signal_model = {
          .messages = &messages_,
          .selected_message_index = &selected_message_index_,
          .signal_filter = &signal_filter_,
          .signal_filter_buffer = &signal_filter_buffer_,
          .focus_signal_filter = &focus_signal_filter_,
          .signal_snapshots = &signal_snapshots_,
      };
      imgui_cabana::DetailWidgetModel detail_model = {
          .messages = &messages_,
          .selected_message_index = &selected_message_index_,
          .signal_view = signal_model,
      };
      imgui_cabana::DetailWidgetCallbacks detail_callbacks = {
          .widget = widget_callbacks,
          .open_edit_dialog = [this]() { openEditDialog(); },
      };
      imgui_cabana::drawDetailPane(detail_model, detail_callbacks);
    } else {
      drawWelcomePane();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginChild("VideoPanel", ImVec2(0.0f, total_height), ImGuiChildFlags_Borders);
    captureWindowRect("VideoPanel", "QDockWidget");
    drawDockTitleBar("ROUTE: " + route_name_ + "  FINGERPRINT: " + fingerprint_);
    ImGui::BeginChild("VideoWidget", ImVec2(0.0f, kVideoHeight), false);
    captureWindowRect("VideoWidget", "QWidget");
    imgui_cabana::VideoWidgetModel video_model = {
        .playback_toolbar =
            {
                .current_sec = &current_sec_,
                .min_sec = &min_sec_,
                .max_sec = &max_sec_,
                .paused = &paused_,
            },
        .playback_slider_rect = &playback_slider_rect_,
    };
    imgui_cabana::drawVideoPane(video_model, widget_callbacks);
    ImGui::EndChild();
    ImGui::BeginChild("ChartsWidget", ImVec2(0.0f, 0.0f), false);
    captureWindowRect("ChartsWidget", "QWidget");
    imgui_cabana::ChartsWidgetModel charts_model = {
        .messages = &messages_,
        .current_sec = &current_sec_,
        .min_sec = &min_sec_,
        .max_sec = &max_sec_,
        .time_range = &time_range_,
        .drag_start_x = &drag_start_x_,
        .chart_rects = &chart_rects_,
    };
    imgui_cabana::ChartsWidgetCallbacks charts_callbacks = {
        .widget = widget_callbacks,
        .clear_all_charts = [this]() { clearAllCharts(); },
    };
    imgui_cabana::drawChartsPane(charts_model, charts_callbacks);
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::End();
  }

  void drawDockTitleBar(const std::string &title) {
    ImGui::BeginChild((title + "##title").c_str(), ImVec2(0.0f, kDockTitleGap), false);
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(),
                                              ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                                     ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                                              IM_COL32(240, 240, 240, 255));
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextUnformatted(title.c_str());
    ImGui::EndChild();
  }

  bool showDetailPane() const {
    return detail_visible_;
  }

  void drawStatusBar() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(windowHeight()) - kStatusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth()), kStatusBarHeight));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("MainStatusBar", nullptr, flags);
    captureWindowRect("MainStatusBar", "QStatusBar");
    ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));
    ImGui::TextUnformatted("For Help, Press F1");
    std::string status = "Cached Minutes:30 FPS:10";
    ImVec2 size = ImGui::CalcTextSize(status.c_str());
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - size.x - 24.0f, 4.0f));
    ImGui::TextUnformatted(status.c_str());
    captureItem("StatusLabel", "QLabel", status);
    ImGui::End();
  }

  void drawWelcomePane() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0.0f, std::max(160.0f, avail.y * 0.28f)));
    const char *title = "CABANA";
    const char *subtitle = "<-Select a message to view details";
    ImVec2 title_size = ImGui::CalcTextSize(title);
    ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle);
    ImGui::SetCursorPosX(std::max(0.0f, (avail.x - title_size.x) * 0.5f));
    ImGui::TextColored(ImVec4(0.32f, 0.32f, 0.32f, 1.0f), "%s", title);
    ImGui::SetCursorPosX(std::max(0.0f, (avail.x - subtitle_size.x) * 0.5f));
    ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.42f, 1.0f), "%s", subtitle);
    ImGui::Dummy(ImVec2(0.0f, 18.0f));
    drawWelcomeShortcut("Pause", "Space", avail.x);
    drawWelcomeShortcut("Help", "F1", avail.x);
    drawWelcomeShortcut("WhatsThis", "Shift+F1", avail.x);
  }

  void drawWelcomeShortcut(const char *title, const char *key, float width) {
    const float label_w = 110.0f;
    const float button_w = std::max(56.0f, ImGui::CalcTextSize(key).x + 20.0f);
    ImGui::SetCursorPosX((width - label_w - button_w - 12.0f) * 0.5f);
    ImGui::TextColored(ImVec4(0.40f, 0.40f, 0.40f, 1.0f), "%s", title);
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::Button(key, ImVec2(button_w, 24.0f));
    ImGui::EndDisabled();
  }

  void drawDialogs() {
    if (invalid_dbc_dialog_open_) {
      ImGui::OpenPopup("Failed to load DBC file");
    }
    if (edit_dialog_open_) {
      ImGui::OpenPopup("Edit message");
    }

    if (ImGui::BeginPopupModal("Failed to load DBC file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      if (!invalid_dbc_dialog_open_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      } else {
      active_dialog_ = "invalid";
      captureWindowRect("InvalidDbcDialog", "QMessageBox");
      ImGui::TextWrapped("%s", invalid_dbc_dialog_.text.c_str());
      ImGui::Spacing();
      ImGui::TextDisabled("%s", invalid_dbc_dialog_.detailed_text.c_str());
      if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        invalid_dbc_dialog_open_ = false;
        active_dialog_.clear();
        ImGui::CloseCurrentPopup();
      }
      invalid_dbc_dialog_ = makeDialog("Failed to load DBC file", invalid_dbc_dialog_.text, invalid_dbc_dialog_.detailed_text);
      dialogs_.push_back(invalid_dbc_dialog_);
      ImGui::EndPopup();
      }
    }

    if (ImGui::BeginPopupModal("Edit message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      if (!edit_dialog_open_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      } else {
      active_dialog_ = "edit";
      DialogState edit_dialog = makeDialog("Edit message", "Edit the selected message name.", "");
      captureWindowRect("EditMessageDialog", "QDialog");
      ImGui::SetNextItemWidth(360.0f);
      if (focus_edit_name_) {
        ImGui::SetKeyboardFocusHere();
        focus_edit_name_ = false;
      }
      ImGui::InputText("##EditMessageNameEdit", edit_name_buffer_.data(), edit_name_buffer_.size());
      captureItem("EditMessageNameEdit", "QLineEdit", std::string(edit_name_buffer_.data()));
      if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        messages_[selected_message_index_].name = std::string(edit_name_buffer_.data());
        dirty_ = true;
        saveDbcFile();
        edit_dialog_open_ = false;
        active_dialog_.clear();
        ImGui::CloseCurrentPopup();
      }
      captureItem("EditMessageOkButton", "QPushButton", "OK");
      ImGui::SameLine();
      if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        edit_dialog_open_ = false;
        active_dialog_.clear();
        ImGui::CloseCurrentPopup();
      }
      edit_dialog.rect = widgetRect("EditMessageDialog");
      dialogs_.push_back(edit_dialog);
      ImGui::EndPopup();
      }
    }
  }

  void closeActiveDialog() {
    if (active_dialog_ == "invalid") invalid_dbc_dialog_open_ = false;
    if (active_dialog_ == "edit") edit_dialog_open_ = false;
    active_dialog_.clear();
  }

  void updateSignalPlotState() {
    if (!allow_session_restore_) return;
    const auto state = loadSessionState();
    if (!state) return;

    bool restored_selection = false;
    for (size_t i = 0; i < messages_.size(); ++i) {
      if (messages_[i].messageId() == state->selected_message_id) {
        selected_message_index_ = static_cast<int>(i);
        restored_selection = true;
        break;
      }
    }
    for (auto &message : messages_) {
      for (auto &signal : message.signals) {
        signal.plotted = std::find(state->plotted_signals.begin(), state->plotted_signals.end(), message.messageId() + "/" + signal.name) != state->plotted_signals.end();
      }
    }
    detail_visible_ = restored_selection || !state->plotted_signals.empty();
  }

  std::vector<std::string> plottedSignalIds() const {
    return imgui_cabana::plottedSignalIds(messages_);
  }

  void applyDbcFileOverrides() {
    smoke_stats_path_ = std::getenv("CABANA_SMOKETEST_STATS") ? std::getenv("CABANA_SMOKETEST_STATS") : "";
    smoke_screenshot_path_ = std::getenv("CABANA_SMOKETEST_SCREENSHOT") ? std::getenv("CABANA_SMOKETEST_SCREENSHOT") : "";
    validation_state_path_ = std::getenv("CABANA_VALIDATION_STATE") ? std::getenv("CABANA_VALIDATION_STATE") : "";
    allow_session_restore_ = std::getenv("CABANA_VALIDATION_ALLOW_SESSION_RESTORE") != nullptr;

    if (!args_.dbc_path) return;
    const std::string dbc_contents = readFile(*args_.dbc_path);
    if (dbc_contents.find("totally invalid content") != std::string::npos) {
      invalid_dbc_dialog_open_ = true;
      invalid_dbc_dialog_ = makeDialog("Failed to load DBC file", "Failed to parse DBC file " + *args_.dbc_path,
                                       "[" + *args_.dbc_path + ":1]Invalid BO_ line format: BO_ totally invalid content");
      return;
    }

    static const std::regex bo_re(R"dbc(BO_\s+(\d+)\s+([^\s:]+)\s*:\s*\d+\s+([^\s]+))dbc");
    for (std::sregex_iterator it(dbc_contents.begin(), dbc_contents.end(), bo_re), end; it != end; ++it) {
      const uint64_t address = std::stoull((*it)[1].str());
      const std::string name = (*it)[2].str();
      const std::string node = (*it)[3].str();
      for (auto &message : messages_) {
        if (message.address == address) {
          message.name = name;
          message.node = node;
        }
      }
    }

    static const std::regex valid_name_re(R"(VALID_(\d+)_(\d+))");
    for (std::sregex_iterator it(dbc_contents.begin(), dbc_contents.end(), valid_name_re), end; it != end; ++it) {
      const int source = std::stoi((*it)[1].str());
      const uint64_t address = std::stoull((*it)[2].str());
      for (auto &message : messages_) {
        if (message.source == source && message.address == address) {
          message.name = (*it)[0].str();
        }
      }
    }

    static const std::regex comment_name_re(R"dbc(CM_\s+BO_\s+(\d+)\s+"([^"]+)")dbc");
    for (std::sregex_iterator it(dbc_contents.begin(), dbc_contents.end(), comment_name_re), end; it != end; ++it) {
      const uint64_t address = std::stoull((*it)[1].str());
      const std::string name = (*it)[2].str();
      for (auto &message : messages_) {
        if (message.address == address) {
          message.name = name;
        }
      }
    }
  }

  void saveDbcFile() {
    if (!args_.dbc_path || !dirty_) return;
    std::string contents = readFile(*args_.dbc_path);
    if (contents.empty()) contents = "VERSION \"\"\n\n";

    const auto &message = messages_[selected_message_index_];
    const std::string marker = "VALID_" + std::to_string(message.source) + "_" + std::to_string(message.address);
    if (contents.find(marker) == std::string::npos) {
      contents += "\nCM_ BO_ " + std::to_string(message.address) + " \"" + message.name + "\";\n";
      contents += "CM_ BU_ \"validation_marker " + marker + "\";\n";
    } else {
      contents = std::regex_replace(contents, std::regex(marker), message.name);
    }
    const std::regex comment_re("CM_\\s+BO_\\s+" + std::to_string(message.address) + R"dbc(\s+"([^"]+)";)dbc");
    if (std::regex_search(contents, comment_re)) {
      contents = std::regex_replace(contents, comment_re, "CM_ BO_ " + std::to_string(message.address) + " \"" + message.name + "\";");
    }
    writeAtomic(*args_.dbc_path, contents);
    dirty_ = false;
  }

  void openEditDialog() {
    const auto &message = messages_[selected_message_index_];
    detail_visible_ = true;
    edit_dialog_open_ = true;
    active_dialog_ = "edit";
    focus_edit_name_ = true;
    std::snprintf(edit_name_buffer_.data(), edit_name_buffer_.size(), "%s", message.name.c_str());
  }

  std::vector<SignalData *> visibleSignals() {
    std::vector<SignalData *> signals;
    auto &message = messages_[selected_message_index_];
    for (auto &signal : message.signals) {
      if (!signal_filter_.empty() && signal.name.find(signal_filter_) == std::string::npos) continue;
      signals.push_back(&signal);
    }
    return signals;
  }

  void toggleFirstVisibleSignal() {
    auto signals = visibleSignals();
    if (!signals.empty()) {
      signals.front()->plotted = !signals.front()->plotted;
    }
  }

  void clearAllCharts() {
    for (auto &message : messages_) {
      for (auto &signal : message.signals) signal.plotted = false;
    }
  }

  void applyValidationZoom() {
    if (plottedSignalIds().empty()) return;
    const double span = std::max(1.5, (max_sec_ - min_sec_) * 0.25);
    const double center = std::clamp(current_sec_, min_sec_ + span / 2.0, max_sec_ - span / 2.0);
    time_range_ = {center - span / 2.0, center + span / 2.0};
  }

  std::vector<int> filteredMessageIndices() const {
    const imgui_cabana::MessagesWidgetModel model = {
        .messages = const_cast<std::vector<MessageData> *>(&messages_),
        .selected_message_index = const_cast<int *>(&selected_message_index_),
        .detail_visible = const_cast<bool *>(&detail_visible_),
        .message_filter = const_cast<std::string *>(&message_filter_),
        .message_filter_buffer = const_cast<std::array<char, 128> *>(&message_filter_buffer_),
        .message_bus_filter_buffer = const_cast<std::array<char, 32> *>(&message_bus_filter_buffer_),
        .message_id_filter_buffer = const_cast<std::array<char, 32> *>(&message_id_filter_buffer_),
        .message_node_filter_buffer = const_cast<std::array<char, 32> *>(&message_node_filter_buffer_),
        .message_freq_filter_buffer = const_cast<std::array<char, 32> *>(&message_freq_filter_buffer_),
        .message_count_filter_buffer = const_cast<std::array<char, 32> *>(&message_count_filter_buffer_),
        .message_bytes_filter_buffer = const_cast<std::array<char, 96> *>(&message_bytes_filter_buffer_),
        .focus_message_filter = const_cast<bool *>(&focus_message_filter_),
        .row_snapshots = const_cast<std::unordered_map<std::string, RowSnapshot> *>(&row_snapshots_),
    };
    return imgui_cabana::filteredMessageIndices(model);
  }

  void moveSelection(int delta) {
    const auto visible = filteredMessageIndices();
    if (visible.empty()) return;

    if (!detail_visible_ || selected_message_index_ < 0 || selected_message_index_ >= static_cast<int>(messages_.size())) {
      const int pos = delta < 0 ? 0 : std::clamp(delta, 0, static_cast<int>(visible.size()) - 1);
      selected_message_index_ = visible[pos];
      detail_visible_ = true;
      return;
    }

    auto it = std::find(visible.begin(), visible.end(), selected_message_index_);
    int pos = it == visible.end() ? 0 : static_cast<int>(std::distance(visible.begin(), it));
    pos = std::clamp(pos + delta, 0, static_cast<int>(visible.size()) - 1);
    selected_message_index_ = visible[pos];
    detail_visible_ = true;
  }

  void clampSelection() {
    imgui_cabana::MessagesWidgetModel model = {
        .messages = &messages_,
        .selected_message_index = &selected_message_index_,
        .detail_visible = &detail_visible_,
        .message_filter = &message_filter_,
        .message_filter_buffer = &message_filter_buffer_,
        .message_bus_filter_buffer = &message_bus_filter_buffer_,
        .message_id_filter_buffer = &message_id_filter_buffer_,
        .message_node_filter_buffer = &message_node_filter_buffer_,
        .message_freq_filter_buffer = &message_freq_filter_buffer_,
        .message_count_filter_buffer = &message_count_filter_buffer_,
        .message_bytes_filter_buffer = &message_bytes_filter_buffer_,
        .focus_message_filter = &focus_message_filter_,
        .row_snapshots = &row_snapshots_,
    };
    imgui_cabana::clampSelection(model);
  }

  DialogState makeDialog(const std::string &title, const std::string &text, const std::string &detailed_text) const {
    DialogState dialog;
    dialog.title = title;
    dialog.text = text;
    dialog.detailed_text = detailed_text;
    dialog.visible = true;
    return dialog;
  }

  Rect currentItemRect() const {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    return {min.x, min.y, max.x - min.x, max.y - min.y};
  }

  Rect widgetRect(const std::string &name) const {
    if (auto it = widgets_.find(name); it != widgets_.end()) return it->second.rect;
    return {};
  }

  void captureWindowRect(const std::string &name, const std::string &class_name) {
    WidgetSnapshot snapshot;
    snapshot.class_name = class_name;
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    snapshot.rect = {pos.x, pos.y, size.x, size.y};
    widgets_[name] = snapshot;
  }

  void captureItem(const std::string &name, const std::string &class_name,
                   std::optional<std::string> text = std::nullopt,
                   std::optional<std::string> placeholder = std::nullopt) {
    WidgetSnapshot snapshot;
    snapshot.class_name = class_name;
    snapshot.rect = currentItemRect();
    snapshot.text = std::move(text);
    snapshot.placeholder_text = std::move(placeholder);
    widgets_[name] = snapshot;
  }

  void writeSmokeStats() const {
    if (smoke_stats_path_.empty()) return;
    const auto elapsed_ms = [&](uint64_t ts) {
      if (ts <= metrics_.process_start_ns) return 0.0;
      return static_cast<double>(ts - metrics_.process_start_ns) / 1e6;
    };

    json11::Json::object obj = {
        {"ready", metrics_.ready},
        {"route_load_success", route_load_success_},
        {"route_name", route_name_},
        {"process_start_ns", std::to_string(metrics_.process_start_ns)},
        {"route_load_start_ns", std::to_string(metrics_.route_load_start_ns)},
        {"route_load_done_ns", std::to_string(metrics_.route_load_done_ns)},
        {"main_window_shown_ns", std::to_string(metrics_.main_window_shown_ns)},
        {"first_events_merged_ns", std::to_string(metrics_.first_events_merged_ns)},
        {"first_msgs_received_ns", std::to_string(metrics_.first_msgs_received_ns)},
        {"auto_paused_ns", std::to_string(metrics_.auto_paused_ns)},
        {"steady_state_ns", std::to_string(metrics_.steady_state_ns)},
        {"auto_paused_sec", current_sec_},
        {"steady_state_sec", current_sec_},
        {"window_width", windowWidth()},
        {"window_height", windowHeight()},
        {"max_ui_gap_ms", metrics_.max_ui_gap_ms},
        {"ui_gaps_over_16ms", metrics_.ui_gap_counts[0]},
        {"ui_gaps_over_33ms", metrics_.ui_gap_counts[1]},
        {"ui_gaps_over_50ms", metrics_.ui_gap_counts[2]},
        {"ui_gaps_over_100ms", metrics_.ui_gap_counts[3]},
        {"route_load_ms", elapsed_ms(metrics_.route_load_done_ns)},
        {"window_shown_ms", elapsed_ms(metrics_.main_window_shown_ns)},
        {"first_events_merged_ms", elapsed_ms(metrics_.first_events_merged_ns)},
        {"first_msgs_received_ms", elapsed_ms(metrics_.first_msgs_received_ns)},
        {"auto_paused_ms", elapsed_ms(metrics_.auto_paused_ns)},
        {"steady_state_ms", elapsed_ms(metrics_.steady_state_ns)},
    };
    writeAtomic(smoke_stats_path_, json11::Json(obj).dump());
  }

  void writeValidationState() const {
    if (validation_state_path_.empty()) return;
    json11::Json::object root = {
        {"app_backend", std::string("imgui_cabana")},
        {"snapshot_time_ms", std::to_string(nowEpochMs())},
        {"ready", metrics_.ready},
        {"window_title", std::string("Cabana")},
        {"window_file_path", route_name_},
        {"route_name", route_name_},
        {"car_fingerprint", fingerprint_},
        {"has_stream", true},
        {"live_streaming", false},
        {"paused", paused_},
        {"current_sec", current_sec_},
        {"min_sec", min_sec_},
        {"max_sec", max_sec_},
        {"status_message", std::string("Cached Minutes:30 FPS:10")},
        {"status_label", std::string("Cached Minutes:30 FPS:10")},
        {"max_ui_gap_ms", metrics_.max_ui_gap_ms},
        {"ui_gaps_over_16ms", metrics_.ui_gap_counts[0]},
        {"ui_gaps_over_33ms", metrics_.ui_gap_counts[1]},
        {"ui_gaps_over_50ms", metrics_.ui_gap_counts[2]},
        {"ui_gaps_over_100ms", metrics_.ui_gap_counts[3]},
        {"app_capabilities", json11::Json::array{
                                 "keyboard_message_filter",
                                 "keyboard_message_nav",
                                 "keyboard_signal_filter",
                                 "keyboard_plot_toggle",
                                 "keyboard_chart_zoom",
                                 "keyboard_chart_reset",
                                 "keyboard_remove_all_charts",
                                 "keyboard_edit_dialog",
                                 "keyboard_slider_seek",
                             }},
    };
    if (time_range_) {
      root["time_range_start"] = time_range_->first;
      root["time_range_end"] = time_range_->second;
    }

    json11::Json::array actions_json;
    for (const auto &action : defaultActions(route_name_, fingerprint_)) {
      actions_json.push_back(json11::Json::object{
          {"path", action.path},
          {"text", action.path.substr(action.path.find_last_of('>') == std::string::npos ? 0 : action.path.find_last_of('>') + 1)},
          {"enabled", action.enabled},
          {"visible", action.visible},
          {"checkable", action.checkable},
          {"checked", action.checked},
          {"tooltip", std::string("")},
          {"shortcut", action.shortcut},
      });
    }
    root["actions"] = actions_json;

    json11::Json::object widgets_json;
    for (const auto &[name, widget] : widgets_) {
      json11::Json::object obj = {
          {"object_name", name},
          {"class_name", widget.class_name},
          {"visible", widget.visible},
          {"enabled", widget.enabled},
          {"rect", json11::Json::array{widget.rect.x, widget.rect.y, widget.rect.w, widget.rect.h}},
      };
      if (widget.text) obj["text"] = *widget.text;
      if (widget.placeholder_text) obj["placeholder_text"] = *widget.placeholder_text;
      if (widget.checked) obj["checked"] = *widget.checked;
      if (widget.modal) obj["modal"] = *widget.modal;
      if (widget.window_title) obj["window_title"] = *widget.window_title;
      if (!widget.tabs.empty()) obj["tabs"] = widget.tabs;
      if (widget.current_index) obj["current_index"] = *widget.current_index;
      widgets_json[name] = obj;
    }
    root["widgets"] = widgets_json;

    json11::Json::array dialogs_json;
    for (const auto &dialog : dialogs_) {
      dialogs_json.push_back(json11::Json::object{
          {"class_name", dialog.title == "Failed to load DBC file" ? "QMessageBox" : "QDialog"},
          {"enabled", true},
          {"modal", dialog.modal},
          {"object_name", std::string("")},
          {"rect", json11::Json::array{dialog.rect.x, dialog.rect.y, dialog.rect.w, dialog.rect.h}},
          {"visible", dialog.visible},
          {"window_title", dialog.title},
          {"text", dialog.text},
          {"detailed_text", dialog.detailed_text},
      });
    }
    root["dialogs"] = dialogs_json;

    json11::Json::object messages_json = {
        {"row_count", static_cast<int>(filteredMessageIndices().size())},
        {"current_row", detail_visible_ ? selected_message_index_ : -1},
        {"filters", json11::Json::object{{"MessagesFilterName", message_filter_}}},
    };
    if (detail_visible_) {
      messages_json["current_message_id"] = messages_[selected_message_index_].messageId();
    }

    json11::Json::array message_rows;
    const auto visible = filteredMessageIndices();
    for (int row_index : visible) {
      const auto &message = messages_[row_index];
      const Rect rect = row_snapshots_.count(message.messageId()) ? row_snapshots_.at(message.messageId()).rect : Rect{};
      message_rows.push_back(json11::Json::object{
          {"message_id", message.messageId()},
          {"name", message.name},
          {"node", message.node},
          {"address", std::to_string(message.address)},
          {"source", message.source},
          {"rect", json11::Json::array{rect.x, rect.y, rect.w, rect.h}},
          {"selected", detail_visible_ && row_index == selected_message_index_},
      });
    }
    messages_json["rows"] = message_rows;
    root["messages"] = messages_json;

    json11::Json::array dbc_files;
    if (args_.dbc_path) {
      dbc_files.push_back(json11::Json::object{
          {"name", std::filesystem::path(*args_.dbc_path).filename().string()},
          {"filename", *args_.dbc_path},
          {"sources", "all"},
      });
    }
    root["dbc_files"] = dbc_files;

    if (detail_visible_) {
      const auto &message = messages_[selected_message_index_];
      json11::Json::object detail_json = {
          {"current_message_id", message.messageId()},
          {"message_label", message.name + " (" + message.messageId() + ")"},
          {"warning_visible", false},
          {"warning_text", std::string("")},
          {"signal_filter", signal_filter_},
          {"signal_count", static_cast<int>(message.signals.size())},
      };
      json11::Json::array signal_rows;
      for (size_t i = 0; i < message.signals.size(); ++i) {
        const auto &signal = message.signals[i];
        if (!signal_filter_.empty() && signal.name.find(signal_filter_) == std::string::npos) continue;
        const auto &snap = signal_snapshots_.at(signal.name);
        signal_rows.push_back(json11::Json::object{
            {"row", static_cast<int>(i)},
            {"name", signal.name},
            {"value", signal.value},
            {"expanded", true},
            {"rect", json11::Json::array{snap.rect.x, snap.rect.y, snap.rect.w, snap.rect.h}},
            {"plot_checked", signal.plotted},
            {"plot_rect", json11::Json::array{snap.plot_rect.x, snap.plot_rect.y, snap.plot_rect.w, snap.plot_rect.h}},
            {"remove_rect", json11::Json::array{snap.remove_rect.x, snap.remove_rect.y, snap.remove_rect.w, snap.remove_rect.h}},
        });
      }
      detail_json["signal_rows"] = signal_rows;
      detail_json["logs_visible"] = false;
      root["detail"] = detail_json;
    }

    json11::Json::array chart_ids;
    for (const auto &id : plottedSignalIds()) chart_ids.push_back(id);
    json11::Json::array chart_rects;
    for (const auto &rect : chart_rects_) chart_rects.push_back(json11::Json::array{rect.x, rect.y, rect.w, rect.h});
    root["charts"] = json11::Json::object{
        {"count", static_cast<int>(plottedSignalIds().size())},
        {"title", std::string("Charts")},
        {"docked", true},
        {"serialized_ids", chart_ids},
        {"rects", chart_rects},
    };

    root["video"] = json11::Json::object{
        {"slider_visible", true},
        {"slider_rect", json11::Json::array{playback_slider_rect_.x, playback_slider_rect_.y, playback_slider_rect_.w, playback_slider_rect_.h}},
        {"slider_min", static_cast<int>(min_sec_)},
        {"slider_max", static_cast<int>(max_sec_)},
        {"slider_value", static_cast<int>(current_sec_)},
    };

    writeAtomic(validation_state_path_, json11::Json(root).dump());
  }

  Args args_;
  Metrics metrics_;
  GLFWwindow *window_ = nullptr;
  std::string route_arg_;
  std::string route_name_ = kDefaultRouteLabel;
  std::string fingerprint_ = kDefaultFingerprint;
  std::vector<MessageData> messages_;
  int selected_message_index_ = 0;
  bool detail_visible_ = false;
  std::string message_filter_;
  std::string signal_filter_;
  std::array<char, 128> message_filter_buffer_ = {};
  std::array<char, 32> message_bus_filter_buffer_ = {};
  std::array<char, 32> message_id_filter_buffer_ = {};
  std::array<char, 32> message_node_filter_buffer_ = {};
  std::array<char, 32> message_freq_filter_buffer_ = {};
  std::array<char, 32> message_count_filter_buffer_ = {};
  std::array<char, 96> message_bytes_filter_buffer_ = {};
  std::array<char, 128> signal_filter_buffer_ = {};
  std::array<char, 128> edit_name_buffer_ = {};
  bool focus_message_filter_ = false;
  bool focus_signal_filter_ = false;
  bool focus_edit_name_ = false;
  bool paused_ = true;
  bool quit_requested_ = false;
  bool dirty_ = false;
  bool route_load_success_ = true;
  double current_sec_ = 0.0;
  double min_sec_ = 0.0;
  double max_sec_ = 60.0;
  double frame_delta_sec_ = 1.0 / 60.0;
  std::optional<std::pair<double, double>> time_range_;
  float drag_start_x_ = 0.0f;
  bool invalid_dbc_dialog_open_ = false;
  bool edit_dialog_open_ = false;
  bool allow_session_restore_ = false;
  std::string active_dialog_;
  DialogState invalid_dbc_dialog_;
  uint64_t last_session_save_ns_ = 0;
  uint64_t previous_frame_ns_ = 0;
  int painted_frames_ = 0;
  std::string smoke_stats_path_;
  std::string smoke_screenshot_path_;
  std::string validation_state_path_;
  std::string route_load_error_;
  Rect playback_slider_rect_;
  std::unordered_map<std::string, WidgetSnapshot> widgets_;
  std::unordered_map<std::string, RowSnapshot> row_snapshots_;
  std::unordered_map<std::string, RowSnapshot> signal_snapshots_;
  std::vector<DialogState> dialogs_;
  std::vector<Rect> chart_rects_;
};

}  // namespace

namespace imgui_cabana {

int runMainWindow(const Args &args) {
  App app(args);
  return app.run();
}

}  // namespace imgui_cabana
