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

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"
#include "json11.hpp"

namespace {

constexpr const char *kDefaultDemoRoute = "5beb9b58bd12b691/0000010a--a51155e496/0";
constexpr const char *kDefaultRouteLabel = "5beb9b58bd12b691|0000010a--a51155e496";
constexpr const char *kDefaultFingerprint = "FORD_BRONCO_SPORT_MK1";
constexpr float kToolbarHeight = 42.0f;
constexpr float kMessagesWidth = 360.0f;
constexpr float kVideoHeight = 248.0f;
constexpr float kChartsHeight = 220.0f;

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

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

struct SignalData {
  std::string name;
  std::string value;
  bool plotted = false;
};

struct MessageData {
  int address = 0;
  int source = 0;
  std::string name;
  std::string node;
  std::vector<SignalData> signals;

  std::string messageId() const {
    return std::to_string(source) + ":" + std::to_string(address);
  }
};

struct DialogState {
  std::string title;
  std::string text;
  std::string detailed_text;
  Rect rect;
  bool modal = true;
  bool visible = false;
};

struct RowSnapshot {
  Rect rect;
  Rect plot_rect;
  Rect remove_rect;
};

struct Args {
  bool demo = false;
  bool no_vipc = false;
  std::optional<std::string> route;
  std::optional<std::string> dbc_path;
  std::optional<std::string> data_dir;
  int width = 1600;
  int height = 900;
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
  return {
      {186, 0, "Steering_Data", "IPMA", {{"steer_angle", "1.24 deg", false}, {"driver_torque", "0.11 Nm", false}, {"eps_torque", "0.09 Nm", false}}},
      {187, 0, "Brake_Data", "ABS", {{"brake_pressure", "18.6 bar", false}, {"pedal_pressed", "true", false}, {"vehicle_speed", "12.2 m/s", false}}},
      {390, 0, "Lane_UI", "IPMA", {{"lane_left", "tracked", false}, {"lane_right", "tracked", false}, {"lka_icon", "active", false}}},
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

bool ctrlPressed() {
  return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
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
    messages_ = defaultMessages();
    selected_message_index_ = 0;
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
  void initWindow() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    SetExitKey(0);
    InitWindow(args_.width, args_.height, "Cabana");
    SetTargetFPS(60);
    SetWindowMinSize(1280, 720);

    rlImGuiSetup(false);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.99f, 0.99f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.86f, 0.88f, 0.91f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.79f, 0.83f, 0.88f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.78f, 0.86f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.83f, 0.87f, 0.93f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.74f, 0.81f, 0.90f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.69f, 0.77f, 0.89f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.90f, 0.91f, 0.94f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.90f, 0.91f, 0.94f, 1.0f);

    metrics_.main_window_shown_ns = nowNs();
  }

  void mainLoop() {
    uint64_t last_state_write_ns = 0;
    while (!WindowShouldClose() && !quit_requested_) {
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

      handleKeyboardShortcuts();
      tickPlayback();

      BeginDrawing();
      ClearBackground(Color{245, 247, 250, 255});
      rlImGuiBegin();
      drawUi();
      rlImGuiEnd();
      EndDrawing();

      if (metrics_.first_events_merged_ns == 0) metrics_.first_events_merged_ns = frame_ns;
      if (metrics_.first_msgs_received_ns == 0) metrics_.first_msgs_received_ns = frame_ns;
      if (metrics_.auto_paused_ns == 0 && paused_) metrics_.auto_paused_ns = frame_ns;
      if (!metrics_.ready) {
        metrics_.steady_state_ns = frame_ns;
        metrics_.ready = true;
        if (!smoke_screenshot_path_.empty()) {
          TakeScreenshot(smoke_screenshot_path_.c_str());
        }
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
    rlImGuiShutdown();
    CloseWindow();
  }

  void handleKeyboardShortcuts() {
    if (ctrlPressed() && IsKeyPressed(KEY_Q)) {
      quit_requested_ = true;
      return;
    }

    if (!active_dialog_.empty() && IsKeyPressed(KEY_ESCAPE)) {
      closeActiveDialog();
      return;
    }

    if (ctrlPressed() && IsKeyPressed(KEY_S)) {
      saveDbcFile();
      return;
    }

    if (IsKeyPressed(KEY_F2)) {
      openEditDialog();
      return;
    }

    if (IsKeyPressed(KEY_F3)) {
      focus_message_filter_ = true;
      return;
    }

    if (IsKeyPressed(KEY_F4)) {
      focus_signal_filter_ = true;
      return;
    }

    if (IsKeyPressed(KEY_F5)) {
      toggleFirstVisibleSignal();
      return;
    }

    if (IsKeyPressed(KEY_F6)) {
      clearAllCharts();
      return;
    }

    if (IsKeyPressed(KEY_F7)) {
      applyValidationZoom();
      return;
    }

    if (IsKeyPressed(KEY_F8)) {
      time_range_.reset();
      return;
    }

    if (IsKeyPressed(KEY_F10)) {
      current_sec_ = min_sec_ + (max_sec_ - min_sec_) * 0.6;
      paused_ = true;
      return;
    }

    if (IsKeyPressed(KEY_F11)) {
      moveSelection(-1);
      return;
    }

    if (IsKeyPressed(KEY_F12)) {
      moveSelection(1);
      return;
    }

    if (ImGui::GetIO().WantTextInput) return;

    if (IsKeyPressed(KEY_SPACE)) paused_ = !paused_;
    if (IsKeyPressed(KEY_UP)) moveSelection(-1);
    if (IsKeyPressed(KEY_DOWN)) moveSelection(1);
    if (IsKeyPressed(KEY_PAGE_UP)) moveSelection(-2);
    if (IsKeyPressed(KEY_PAGE_DOWN)) moveSelection(2);
    if (IsKeyPressed(KEY_LEFT)) {
      current_sec_ = std::max(min_sec_, current_sec_ - 1.0f);
      paused_ = true;
    }
    if (IsKeyPressed(KEY_RIGHT)) {
      current_sec_ = std::min(max_sec_, current_sec_ + 1.0f);
      paused_ = true;
    }
  }

  void tickPlayback() {
    if (!paused_) {
      current_sec_ = std::min(max_sec_, current_sec_ + GetFrameTime());
      if (current_sec_ >= max_sec_) paused_ = true;
    }
    for (auto &message : messages_) {
      for (auto &signal : message.signals) {
        signal.value = animatedValue(signal.name);
      }
    }
  }

  std::string animatedValue(const std::string &name) const {
    const double t = current_sec_;
    if (name == "steer_angle") return fmtFloat(std::sin(t) * 1.8, " deg");
    if (name == "driver_torque") return fmtFloat(0.12 + std::cos(t * 0.7) * 0.08, " Nm");
    if (name == "eps_torque") return fmtFloat(0.09 + std::sin(t * 0.9) * 0.07, " Nm");
    if (name == "brake_pressure") return fmtFloat(17.5 + std::sin(t * 0.4) * 2.5, " bar");
    if (name == "pedal_pressed") return (std::fmod(t, 4.0) > 2.0) ? "true" : "false";
    if (name == "vehicle_speed") return fmtFloat(11.8 + std::sin(t * 0.25) * 0.7, " m/s");
    if (name == "lane_left") return (std::fmod(t, 5.0) > 1.0) ? "tracked" : "searching";
    if (name == "lane_right") return (std::fmod(t, 6.0) > 1.0) ? "tracked" : "searching";
    if (name == "lka_icon") return paused_ ? "standby" : "active";
    return "0";
  }

  static std::string fmtFloat(double value, const char *suffix) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f%s", value, suffix);
    return buffer;
  }

  void drawUi() {
    widgets_.clear();
    row_snapshots_.clear();
    signal_snapshots_.clear();
    dialogs_.clear();
    chart_rects_.clear();

    drawMainMenu();
    drawMainWindow();
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
    const float menu_height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + menu_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - menu_height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("CabanaMainWindow", nullptr, flags);
    captureWindowRect("CabanaMainWindow", "QMainWindow");

    drawPlaybackToolbar();
    ImGui::Spacing();

    const float content_height = ImGui::GetContentRegionAvail().y;
    const float detail_height = std::max(200.0f, content_height - kVideoHeight - kChartsHeight - 24.0f);

    ImGui::BeginChild("MessagesWidget", ImVec2(kMessagesWidth, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    captureWindowRect("MessagesWidget", "QWidget");
    drawMessagesPane();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::BeginChild("VideoWidget", ImVec2(0.0f, kVideoHeight), ImGuiChildFlags_Borders);
    captureWindowRect("VideoWidget", "QWidget");
    drawVideoPane();
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("DetailPane", ImVec2(0.0f, detail_height), ImGuiChildFlags_Borders);
    drawDetailPane();
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("ChartsWidget", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    captureWindowRect("ChartsWidget", "QWidget");
    drawChartsPane();
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::End();
    ImGui::PopStyleVar();
  }

  void drawPlaybackToolbar() {
    ImGui::BeginChild("PlaybackToolbar", ImVec2(0.0f, kToolbarHeight), ImGuiChildFlags_Borders);
    captureWindowRect("PlaybackToolbar", "QWidget");

    if (ImGui::Button(paused_ ? "Play" : "Pause")) paused_ = !paused_;
    captureItem("PlaybackPlayToggleButton", "QToolButton", std::string(paused_ ? "Play" : "Pause"));
    ImGui::SameLine();

    if (ImGui::Button("<<")) current_sec_ = std::max(min_sec_, current_sec_ - 1.0f);
    captureItem("PlaybackSeekBackwardButton", "QToolButton", "<<");
    ImGui::SameLine();

    if (ImGui::Button(">>")) current_sec_ = std::min(max_sec_, current_sec_ + 1.0f);
    captureItem("PlaybackSeekForwardButton", "QToolButton", ">>");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.0f);
    float slider_sec = current_sec_;
    if (ImGui::SliderFloat("##PlaybackSlider", &slider_sec, min_sec_, max_sec_, "")) {
      current_sec_ = slider_sec;
      paused_ = true;
    }
    captureItem("PlaybackSlider", "QSlider");
    playback_slider_rect_ = currentItemRect();

    ImGui::SameLine();
    ImGui::Text("%.2fs", current_sec_);
    ImGui::EndChild();
  }

  void drawMessagesPane() {
    ImGui::TextUnformatted("Messages");
    ImGui::BeginChild("MessageHeader", ImVec2(0.0f, 44.0f), false);
    captureWindowRect("MessageHeader", "QWidget");
    ImGui::SetNextItemWidth(-1.0f);
    if (focus_message_filter_) {
      ImGui::SetKeyboardFocusHere();
      focus_message_filter_ = false;
    }
    if (ImGui::InputTextWithHint("##MessagesFilterName", "Filter by name", message_filter_buffer_.data(), message_filter_buffer_.size())) {
      message_filter_ = std::string(message_filter_buffer_.data());
      clampSelection();
    }
    captureItem("MessagesFilterName", "QLineEdit", message_filter_, std::string("Filter by name"));
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("MessageTable", ImVec2(0.0f, 0.0f), false);
    captureWindowRect("MessageTable", "QTableView");

    const auto visible = filteredMessageIndices();
    for (int index : visible) {
      auto &message = messages_[index];
      const bool selected = index == selected_message_index_;
      std::string label = message.messageId() + "  " + message.name;
      if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
        selected_message_index_ = index;
      }
      row_snapshots_[message.messageId()].rect = currentItemRect();
    }
    ImGui::EndChild();
  }

  void drawVideoPane() {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 max = ImVec2(origin.x + size.x, origin.y + size.y);

    draw_list->AddRectFilled(origin, max, IM_COL32(26, 31, 38, 255), 8.0f);
    draw_list->AddRect(origin, max, IM_COL32(65, 74, 86, 255), 8.0f, 0, 1.5f);
    draw_list->AddText(ImVec2(origin.x + 20.0f, origin.y + 18.0f), IM_COL32(228, 232, 238, 255), route_name_.c_str());
    draw_list->AddText(ImVec2(origin.x + 20.0f, origin.y + 46.0f), IM_COL32(144, 154, 166, 255), "Video preview shell");
    draw_list->AddLine(ImVec2(origin.x + 24.0f, origin.y + size.y - 48.0f), ImVec2(max.x - 24.0f, origin.y + size.y - 48.0f), IM_COL32(72, 82, 94, 255), 2.0f);

    float indicator_x = origin.x + 24.0f + ((current_sec_ - min_sec_) / (max_sec_ - min_sec_ + 0.0001f)) * (size.x - 48.0f);
    draw_list->AddLine(ImVec2(indicator_x, origin.y + size.y - 62.0f), ImVec2(indicator_x, origin.y + size.y - 34.0f), IM_COL32(94, 168, 255, 255), 3.0f);
    ImGui::Dummy(size);
  }

  void drawDetailPane() {
    auto &message = messages_[selected_message_index_];

    ImGui::BeginChild("DetailToolbar", ImVec2(0.0f, 38.0f), false);
    captureWindowRect("DetailToolbar", "QWidget");
    ImGui::Text("%s (%s)", message.name.c_str(), message.messageId().c_str());
    captureItem("DetailMessageLabel", "QLabel", message.name + " (" + message.messageId() + ")");
    ImGui::SameLine();
    if (ImGui::Button("Edit")) {
      openEditDialog();
    }
    captureItem("EditMessageButton", "QToolButton", "Edit");
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("SignalView", ImVec2(0.0f, 0.0f), false);
    captureWindowRect("SignalView", "QWidget");
    ImGui::SetNextItemWidth(-1.0f);
    if (focus_signal_filter_) {
      ImGui::SetKeyboardFocusHere();
      focus_signal_filter_ = false;
    }
    if (ImGui::InputTextWithHint("##SignalFilterEdit", "Filter signals", signal_filter_buffer_.data(), signal_filter_buffer_.size())) {
      signal_filter_ = std::string(signal_filter_buffer_.data());
    }
    captureItem("SignalFilterEdit", "QLineEdit", signal_filter_, std::string("Filter signals"));
    ImGui::Separator();

    ImGui::BeginChild("SignalTree", ImVec2(0.0f, 0.0f), false);
    captureWindowRect("SignalTree", "QTreeView");
    for (auto &signal : message.signals) {
      if (!signal_filter_.empty() && signal.name.find(signal_filter_) == std::string::npos) continue;
      ImGui::PushID(signal.name.c_str());
      ImGui::Text("%s", signal.name.c_str());
      signal_snapshots_[signal.name].rect = currentItemRect();
      ImGui::SameLine(220.0f);
      ImGui::TextColored(ImVec4(0.28f, 0.33f, 0.39f, 1.0f), "%s", signal.value.c_str());
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96.0f);
      if (ImGui::SmallButton(signal.plotted ? "Plotted" : "Plot")) {
        signal.plotted = !signal.plotted;
      }
      signal_snapshots_[signal.name].plot_rect = currentItemRect();
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndChild();
  }

  void drawChartsPane() {
    ImGui::BeginChild("ChartsToolbar", ImVec2(0.0f, 38.0f), false);
    captureWindowRect("ChartsToolbar", "QWidget");
    if (ImGui::Button("Reset Zoom")) time_range_.reset();
    captureItem("ChartsResetZoomButton", "QToolButton", "Reset Zoom");
    ImGui::SameLine();
    if (ImGui::Button("Remove All")) {
      for (auto &message : messages_) {
        for (auto &signal : message.signals) signal.plotted = false;
      }
    }
    captureItem("ChartsRemoveAllButton", "QToolButton", "Remove All");
    ImGui::EndChild();
    ImGui::Separator();

    const auto plotted = plottedSignalIds();
    if (plotted.empty()) {
      ImGui::Dummy(ImVec2(0.0f, 12.0f));
      ImGui::TextColored(ImVec4(0.42f, 0.47f, 0.54f, 1.0f), "Plot a signal to start charting.");
      return;
    }

    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##ChartCanvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    chart_rects_.push_back({canvas_pos.x, canvas_pos.y, canvas_size.x, canvas_size.y});

    if (ImGui::IsItemActivated()) drag_start_x_ = ImGui::GetIO().MousePos.x;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const float end_x = ImGui::GetIO().MousePos.x;
      if (std::fabs(end_x - drag_start_x_) > 8.0f) {
        const float min_x = std::min(drag_start_x_, end_x);
        const float max_x = std::max(drag_start_x_, end_x);
        const float canvas_min = canvas_pos.x;
        const float canvas_max = canvas_pos.x + canvas_size.x;
        const float start_norm = std::clamp((min_x - canvas_min) / std::max(1.0f, canvas_max - canvas_min), 0.0f, 1.0f);
        const float end_norm = std::clamp((max_x - canvas_min) / std::max(1.0f, canvas_max - canvas_min), 0.0f, 1.0f);
        time_range_ = {min_sec_ + start_norm * (max_sec_ - min_sec_), min_sec_ + end_norm * (max_sec_ - min_sec_)};
      }
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(252, 252, 253, 255), 8.0f);
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(191, 198, 208, 255), 8.0f, 0, 1.5f);

    for (int i = 0; i < 6; ++i) {
      float y = canvas_pos.y + (canvas_size.y / 5.0f) * i;
      draw_list->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_pos.x + canvas_size.x, y), IM_COL32(233, 236, 240, 255), 1.0f);
    }

    const std::array<ImU32, 3> colors = {IM_COL32(48, 113, 189, 255), IM_COL32(200, 88, 43, 255), IM_COL32(54, 149, 90, 255)};
    for (size_t s = 0; s < plotted.size(); ++s) {
      std::vector<ImVec2> points;
      for (int x = 0; x < static_cast<int>(canvas_size.x); x += 16) {
        float norm = static_cast<float>(x) / std::max(1.0f, canvas_size.x - 1.0f);
        float t = min_sec_ + norm * (max_sec_ - min_sec_);
        float y_norm = 0.5f + 0.25f * std::sin(t * (0.8f + static_cast<float>(s) * 0.3f));
        points.push_back(ImVec2(canvas_pos.x + x, canvas_pos.y + (1.0f - y_norm) * canvas_size.y));
      }
      draw_list->AddPolyline(points.data(), static_cast<int>(points.size()), colors[s % colors.size()], 0, 2.0f);
    }

    float marker_x = canvas_pos.x + ((current_sec_ - min_sec_) / (max_sec_ - min_sec_ + 0.0001f)) * canvas_size.x;
    draw_list->AddLine(ImVec2(marker_x, canvas_pos.y), ImVec2(marker_x, canvas_pos.y + canvas_size.y), IM_COL32(65, 74, 86, 255), 2.0f);
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
      if (ImGui::Button("Close") || IsKeyPressed(KEY_ESCAPE)) {
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
      if (ImGui::Button("OK") || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        messages_[selected_message_index_].name = std::string(edit_name_buffer_.data());
        dirty_ = true;
        saveDbcFile();
        edit_dialog_open_ = false;
        active_dialog_.clear();
        ImGui::CloseCurrentPopup();
      }
      captureItem("EditMessageOkButton", "QPushButton", "OK");
      ImGui::SameLine();
      if (ImGui::Button("Cancel") || IsKeyPressed(KEY_ESCAPE)) {
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

    for (size_t i = 0; i < messages_.size(); ++i) {
      if (messages_[i].messageId() == state->selected_message_id) {
        selected_message_index_ = static_cast<int>(i);
        break;
      }
    }
    for (auto &message : messages_) {
      for (auto &signal : message.signals) {
        signal.plotted = std::find(state->plotted_signals.begin(), state->plotted_signals.end(), message.messageId() + "/" + signal.name) != state->plotted_signals.end();
      }
    }
  }

  std::vector<std::string> plottedSignalIds() const {
    std::vector<std::string> ids;
    for (const auto &message : messages_) {
      for (const auto &signal : message.signals) {
        if (signal.plotted) ids.push_back(message.messageId() + "/" + signal.name);
      }
    }
    return ids;
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

    static const std::regex valid_name_re(R"(VALID_(\d+)_(\d+))");
    for (std::sregex_iterator it(dbc_contents.begin(), dbc_contents.end(), valid_name_re), end; it != end; ++it) {
      const int source = std::stoi((*it)[1].str());
      const int address = std::stoi((*it)[2].str());
      for (auto &message : messages_) {
        if (message.source == source && message.address == address) {
          message.name = (*it)[0].str();
        }
      }
    }

    static const std::regex comment_name_re(R"dbc(CM_\s+BO_\s+(\d+)\s+"([^"]+)")dbc");
    for (std::sregex_iterator it(dbc_contents.begin(), dbc_contents.end(), comment_name_re), end; it != end; ++it) {
      const int address = std::stoi((*it)[1].str());
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
    std::vector<int> indices;
    for (size_t i = 0; i < messages_.size(); ++i) {
      if (message_filter_.empty() || messages_[i].name.find(message_filter_) == 0) {
        indices.push_back(static_cast<int>(i));
      }
    }
    if (indices.empty()) {
      for (size_t i = 0; i < messages_.size(); ++i) indices.push_back(static_cast<int>(i));
    }
    return indices;
  }

  void moveSelection(int delta) {
    const auto visible = filteredMessageIndices();
    if (visible.empty()) return;

    auto it = std::find(visible.begin(), visible.end(), selected_message_index_);
    int pos = it == visible.end() ? 0 : static_cast<int>(std::distance(visible.begin(), it));
    pos = std::clamp(pos + delta, 0, static_cast<int>(visible.size()) - 1);
    selected_message_index_ = visible[pos];
  }

  void clampSelection() {
    const auto visible = filteredMessageIndices();
    if (visible.empty()) return;
    if (std::find(visible.begin(), visible.end(), selected_message_index_) == visible.end()) {
      selected_message_index_ = visible.front();
    }
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
        {"route_load_success", true},
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
        {"window_width", GetScreenWidth()},
        {"window_height", GetScreenHeight()},
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
        {"status_message", std::string("Imgui Cabana shell")},
        {"status_label", fingerprint_},
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
        {"current_message_id", messages_[selected_message_index_].messageId()},
        {"current_row", selected_message_index_},
        {"filters", json11::Json::object{{"MessagesFilterName", message_filter_}}},
    };

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
          {"selected", row_index == selected_message_index_},
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
  std::string route_arg_;
  std::string route_name_ = kDefaultRouteLabel;
  std::string fingerprint_ = kDefaultFingerprint;
  std::vector<MessageData> messages_;
  int selected_message_index_ = 0;
  std::string message_filter_;
  std::string signal_filter_;
  std::array<char, 128> message_filter_buffer_ = {};
  std::array<char, 128> signal_filter_buffer_ = {};
  std::array<char, 128> edit_name_buffer_ = {};
  bool focus_message_filter_ = false;
  bool focus_signal_filter_ = false;
  bool focus_edit_name_ = false;
  bool paused_ = true;
  bool quit_requested_ = false;
  bool dirty_ = false;
  double current_sec_ = 0.0;
  double min_sec_ = 0.0;
  double max_sec_ = 10.0;
  std::optional<std::pair<double, double>> time_range_;
  float drag_start_x_ = 0.0f;
  bool invalid_dbc_dialog_open_ = false;
  bool edit_dialog_open_ = false;
  bool allow_session_restore_ = false;
  std::string active_dialog_;
  DialogState invalid_dbc_dialog_;
  uint64_t last_session_save_ns_ = 0;
  std::string smoke_stats_path_;
  std::string smoke_screenshot_path_;
  std::string validation_state_path_;
  Rect playback_slider_rect_;
  std::unordered_map<std::string, WidgetSnapshot> widgets_;
  std::unordered_map<std::string, RowSnapshot> row_snapshots_;
  std::unordered_map<std::string, RowSnapshot> signal_snapshots_;
  std::vector<DialogState> dialogs_;
  std::vector<Rect> chart_rects_;
};

Args parseArgs(int argc, char *argv[]) {
  Args args;
  if (const char *size = std::getenv("CABANA_SMOKETEST_SIZE")) {
    int width = 0;
    int height = 0;
    if (std::sscanf(size, "%dx%d", &width, &height) == 2 && width > 0 && height > 0) {
      args.width = width;
      args.height = height;
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::printf("Usage: %s [options] route\n", argv[0]);
      std::printf("  --demo\n  --no-vipc\n  --dbc <file>\n  --data_dir <dir>\n");
      std::exit(0);
    } else if (arg == "--demo") {
      args.demo = true;
    } else if (arg == "--no-vipc") {
      args.no_vipc = true;
    } else if (arg == "--dbc" && i + 1 < argc) {
      args.dbc_path = argv[++i];
    } else if (arg == "--data_dir" && i + 1 < argc) {
      args.data_dir = argv[++i];
    } else if (!arg.empty() && arg[0] != '-') {
      args.route = std::string(arg);
    }
  }
  return args;
}

}  // namespace

int main(int argc, char *argv[]) {
  App app(parseArgs(argc, argv));
  return app.run();
}
