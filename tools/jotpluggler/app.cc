#include "tools/jotpluggler/app.h"
#include "tools/jotpluggler/imgui_impl_glfw.h"
#include "tools/jotpluggler/sketch_layout.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace jotpluggler {
namespace fs = std::filesystem;

namespace {

constexpr float kSidebarWidth = 282.0f;
constexpr float kContentGap = 12.0f;
constexpr float kContentRightPadding = 8.0f;
constexpr float kToolbarHeight = 24.0f;
constexpr float kStatusBarHeight = 28.0f;
constexpr float kTabStripHeight = 34.0f;
constexpr float kPaneInset = 8.0f;
constexpr float kPaneGutter = 8.0f;

struct UiMetrics {
  float width = 0.0f;
  float height = 0.0f;
  float top_offset = 0.0f;
  float sidebar_width = kSidebarWidth;
  float content_x = 0.0f;
  float content_y = 0.0f;
  float content_w = 0.0f;
  float content_h = 0.0f;
  float pane_origin_x = 0.0f;
  float pane_origin_y = 0.0f;
  float pane_area_w = 0.0f;
  float pane_area_h = 0.0f;
  float status_bar_y = 0.0f;
};

struct AppSession {
  fs::path layout_path;
  std::string route_name;
  std::string data_dir;
  SketchLayout layout;
};

struct PlotBounds {
  double x_min = 0.0;
  double x_max = 1.0;
  double y_min = 0.0;
  double y_max = 1.0;
};

struct UnsupportedCurve {
  std::string label;
  std::string reason;
};

struct TabUiState {
  bool dock_needs_build = true;
  int active_pane_index = 0;
};

struct UiState {
  bool streaming = false;
  int buffer_size = 5;
  int source_index = 0;
  bool open_about = false;
  bool open_custom_series = false;
  bool open_open_route = false;
  bool open_save_screenshot = false;
  bool request_close = false;
  bool reset_plot_view = false;
  bool request_reload = false;
  int active_tab_index = 0;
  std::vector<TabUiState> tabs;
  std::array<char, 128> route_buffer = {};
  std::array<char, 512> data_dir_buffer = {};
  std::string error_text;
  bool open_error_popup = false;
  std::string status_text = "Ready";
};

void glfw_error_callback(int error, const char *description) {
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

void ensure_parent_dir(const fs::path &path) {
  const fs::path parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

void run_or_throw(const std::string &command, const std::string &action) {
  const int ret = std::system(command.c_str());
  if (ret != 0) {
    throw std::runtime_error(action + " failed with exit code " + std::to_string(ret));
  }
}

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
  style.Colors[ImGuiCol_Separator] = color_rgb(194, 198, 204);
  style.Colors[ImGuiCol_ScrollbarBg] = color_rgb(240, 242, 245);
  style.Colors[ImGuiCol_ScrollbarGrab] = color_rgb(202, 207, 214);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = color_rgb(180, 186, 194);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = color_rgb(164, 171, 180);

  ImPlotStyle &plot_style = ImPlot::GetStyle();
  plot_style.PlotBorderSize = 1.0f;
  plot_style.MinorAlpha = 0.0f;
  plot_style.LegendPadding = ImVec2(6.0f, 6.0f);
  plot_style.LegendInnerPadding = ImVec2(6.0f, 4.0f);
  plot_style.LegendSpacing = ImVec2(8.0f, 3.0f);
  plot_style.PlotPadding = ImVec2(10.0f, 8.0f);
}

UiMetrics compute_ui_metrics(const ImVec2 &size, float top_offset) {
  UiMetrics ui;
  ui.width = size.x;
  ui.height = size.y;
  ui.top_offset = top_offset;
  ui.content_x = ui.sidebar_width + kContentGap;
  ui.content_y = top_offset + 4.0f;
  ui.content_w = std::max(1.0f, size.x - ui.content_x - kContentRightPadding);
  ui.content_h = std::max(1.0f, size.y - ui.content_y - kStatusBarHeight - 4.0f);
  ui.pane_origin_x = ui.content_x + kPaneInset;
  ui.pane_origin_y = ui.content_y + kTabStripHeight + 4.0f;
  ui.pane_area_w = std::max(1.0f, ui.content_w - 2.0f * kPaneInset);
  ui.pane_area_h = std::max(1.0f, ui.content_h - kTabStripHeight - 8.0f);
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

void sync_ui_state(UiState *state, const SketchLayout &layout) {
  state->tabs.resize(layout.tabs.size());
  if (layout.tabs.empty()) {
    state->active_tab_index = 0;
    return;
  }
  state->active_tab_index = std::clamp(state->active_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  for (size_t i = 0; i < layout.tabs.size(); ++i) {
    const int pane_count = static_cast<int>(layout.tabs[i].panes.size());
    state->tabs[i].active_pane_index = pane_count <= 0
      ? 0
      : std::clamp(state->tabs[i].active_pane_index, 0, pane_count - 1);
  }
}

void mark_docks_dirty(UiState *state) {
  for (TabUiState &tab_state : state->tabs) {
    tab_state.dock_needs_build = true;
  }
}

void sync_route_buffers(UiState *state, const AppSession &session) {
  copy_to_buffer(session.route_name, &state->route_buffer);
  copy_to_buffer(session.data_dir, &state->data_dir_buffer);
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

const Pane *active_pane(const SketchLayout &layout, const UiState &state) {
  const WorkspaceTab *tab = active_tab(layout, state);
  if (tab == nullptr || tab->panes.empty()) {
    return nullptr;
  }
  const TabUiState *tab_state = state.tabs.empty() ? nullptr : &state.tabs[static_cast<size_t>(std::clamp(state.active_tab_index, 0, static_cast<int>(state.tabs.size()) - 1))];
  const int pane_index = tab_state == nullptr ? 0 : std::clamp(tab_state->active_pane_index, 0, static_cast<int>(tab->panes.size()) - 1);
  return &tab->panes[static_cast<size_t>(pane_index)];
}

Pane *active_pane(SketchLayout *layout, UiState *state) {
  WorkspaceTab *tab = active_tab(layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr || tab->panes.empty()) {
    return nullptr;
  }
  tab_state->active_pane_index = std::clamp(tab_state->active_pane_index, 0, static_cast<int>(tab->panes.size()) - 1);
  return &tab->panes[static_cast<size_t>(tab_state->active_pane_index)];
}

std::string pane_window_name(int tab_index, int pane_index, const Pane &pane) {
  std::string title = pane.title.empty() ? "plot" : pane.title;
  return title + "##tab" + std::to_string(tab_index) + "_pane" + std::to_string(pane_index);
}

ImGuiID dockspace_id_for_tab(int tab_index) {
  return ImHashStr(("jotpluggler_dockspace_" + std::to_string(tab_index)).c_str());
}

std::string curve_display_name(const Curve &curve);
std::vector<UnsupportedCurve> collect_unsupported_curves(const Pane &pane);

bool curve_has_samples(const Curve &curve) {
  return curve.xs.size() > 1 && curve.xs.size() == curve.ys.size();
}

size_t total_curve_count(const WorkspaceTab &tab) {
  size_t total = 0;
  for (const Pane &pane : tab.panes) {
    total += pane.curves.size();
  }
  return total;
}

size_t sampled_curve_count(const WorkspaceTab &tab) {
  size_t total = 0;
  for (const Pane &pane : tab.panes) {
    for (const Curve &curve : pane.curves) {
      total += curve.visible && curve_has_samples(curve) ? 1U : 0U;
    }
  }
  return total;
}

std::string route_summary(const AppSession &session) {
  std::string summary = session.route_name;
  if (!session.data_dir.empty()) {
    summary += " (local)";
  }
  return summary;
}

void show_not_implemented_modal(const char *popup_name, const char *title, const char *body) {
  if (ImGui::BeginPopupModal(popup_name, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    ImGui::TextWrapped("%s", body);
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

float draw_main_menu_bar(UiState *state) {
  float height = ImGui::GetFrameHeight();
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, color_rgb(46, 46, 46));
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("App")) {
      if (ImGui::MenuItem("Open Route...")) {
        state->open_open_route = true;
      }
      if (ImGui::MenuItem("Reload Route")) {
        state->request_reload = true;
        state->status_text = "Reloading route";
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) {
        state->request_close = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::MenuItem("Reset Plot View")) {
        state->reset_plot_view = true;
        state->status_text = "Plot view reset";
      }
      if (ImGui::MenuItem("Custom Series...")) {
        state->open_custom_series = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About JotPlugger")) {
        state->open_about = true;
      }
      ImGui::EndMenu();
    }
    height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
  }
  ImGui::PopStyleColor();
  return height;
}

float draw_toolbar(const AppSession &session, UiState *state, float top_offset) {
  ImGui::SetNextWindowPos(ImVec2(0.0f, top_offset));
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->Size.x, kToolbarHeight));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 2.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(61, 69, 79));
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, color_rgb(61, 69, 79));
  ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(235, 238, 244));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_MenuBar;
  if (ImGui::Begin("##toolbar", nullptr, flags)) {
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Screenshot...")) {
          state->open_save_screenshot = true;
          state->status_text = "Screenshot export is CLI-driven for now";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close")) {
          state->request_close = true;
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Open Route")) {
      state->open_open_route = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) {
      state->request_reload = true;
      state->status_text = "Reloading route";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", route_summary(session).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const WorkspaceTab *tab = active_tab(session.layout, *state);
    ImGui::TextDisabled("%s", tab == nullptr || tab->tab_name.empty() ? "tab0" : tab->tab_name.c_str());
  }
  ImGui::End();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
  return kToolbarHeight;
}

void draw_workspace_background(const UiMetrics &ui) {
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ui.top_offset));
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, std::max(1.0f, viewport->Size.y - ui.top_offset - kStatusBarHeight)));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(227, 229, 233));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoInputs;
  ImGui::Begin("##workspace_background", nullptr, flags);
  ImGui::End();
  ImGui::PopStyleColor();
}

void draw_status_bar(const UiMetrics &ui, const UiState &state) {
  ImGui::SetNextWindowPos(ImVec2(0.0f, ui.status_bar_y));
  ImGui::SetNextWindowSize(ImVec2(ui.width, kStatusBarHeight));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(248, 248, 249));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(188, 193, 199));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##status_bar", nullptr, flags)) {
    if (ImGui::Button(state.streaming ? "Live" : "Ready", ImVec2(52.0f, 0.0f))) {
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.status_text.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(180.0f, ui.width - 220.0f));
    ImGui::ProgressBar(state.streaming ? 0.25f : 0.0f, ImVec2(110.0f, 0.0f));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::BeginCombo("##status_scale", "1.0")) {
      ImGui::Selectable("0.5");
      ImGui::Selectable("1.0", true);
      ImGui::Selectable("2.0");
      ImGui::EndCombo();
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

void draw_sidebar(AppSession *session, const UiMetrics &ui, UiState *state) {
  const WorkspaceTab *tab = active_tab(session->layout, *state);
  const Pane *pane = active_pane(session->layout, *state);
  TabUiState *tab_state = active_tab_state(state);
  ImGui::SetNextWindowPos(ImVec2(0.0f, ui.top_offset));
  ImGui::SetNextWindowSize(ImVec2(ui.sidebar_width, std::max(1.0f, ui.height - ui.top_offset - kStatusBarHeight)));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(238, 240, 244));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(190, 197, 205));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##sidebar", nullptr, flags)) {
    ImGui::SeparatorText("Data");
    ImGui::TextUnformatted(route_summary(*session).c_str());
    if (ImGui::Button("Open...", ImVec2(72.0f, 0.0f))) {
      state->open_open_route = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(72.0f, 0.0f))) {
      state->request_reload = true;
      state->status_text = "Reloading route";
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Layout");
    ImGui::TextUnformatted(tab == nullptr || tab->tab_name.empty() ? "tab0" : tab->tab_name.c_str());
    ImGui::Spacing();

    ImGui::SeparatorText("Streaming");
    if (ImGui::Button(state->streaming ? "Stop" : "Start", ImVec2(64.0f, 24.0f))) {
      state->streaming = !state->streaming;
      state->status_text = state->streaming ? "Streaming started" : "Streaming stopped";
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::InputInt("##buffer_size", &state->buffer_size, 0, 0)) {
      state->buffer_size = std::max(1, state->buffer_size);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Buffer");
    static constexpr std::array<const char *, 1> kSources = {"Cereal Subscriber"};
    const char *source_label = kSources[std::clamp(state->source_index, 0, static_cast<int>(kSources.size()) - 1)];
    if (ImGui::BeginCombo("##source_select", source_label)) {
      for (int i = 0; i < static_cast<int>(kSources.size()); ++i) {
        const bool selected = i == state->source_index;
        if (ImGui::Selectable(kSources[i], selected)) {
          state->source_index = i;
          state->status_text = "Source updated";
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Publishers");
    if (ImGui::BeginListBox("##publishers", ImVec2(-FLT_MIN, 52.0f))) {
      ImGui::TextDisabled("none");
      ImGui::EndListBox();
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Panes");
    if (tab != nullptr && tab_state != nullptr && ImGui::BeginListBox("##pane_list", ImVec2(-FLT_MIN, 120.0f))) {
      for (size_t i = 0; i < tab->panes.size(); ++i) {
        const bool selected = static_cast<int>(i) == tab_state->active_pane_index;
        const std::string &title = tab->panes[i].title.empty() ? std::string("plot") : tab->panes[i].title;
        if (ImGui::Selectable(title.c_str(), selected)) {
          tab_state->active_pane_index = static_cast<int>(i);
          state->status_text = "Active pane updated";
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndListBox();
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Timeseries List");
    const int total_curves = tab == nullptr ? 0 : static_cast<int>(total_curve_count(*tab));
    const int sampled_curves = tab == nullptr ? 0 : static_cast<int>(sampled_curve_count(*tab));
    ImGui::TextDisabled("%d of %d", sampled_curves, total_curves);
    const float child_height = std::max(90.0f, ImGui::GetContentRegionAvail().y - 200.0f);
    if (pane != nullptr && ImGui::BeginListBox("##pane_curves", ImVec2(-FLT_MIN, child_height))) {
      for (Curve &curve : active_pane(&session->layout, state)->curves) {
        const std::string label = curve_display_name(curve);
        if (ImGui::Checkbox(label.c_str(), &curve.visible)) {
          state->status_text = curve.visible ? "Curve enabled" : "Curve hidden";
        }
      }
      ImGui::EndListBox();
    }

    ImGui::SeparatorText("Sources");
    if (ImGui::BeginListBox("##timeseries_roots", ImVec2(-FLT_MIN, 84.0f))) {
      for (const std::string &root : session->layout.roots) {
        ImGui::Selectable(root.c_str(), false, ImGuiSelectableFlags_Disabled);
      }
      ImGui::EndListBox();
    }

    if (pane != nullptr) {
      const std::vector<UnsupportedCurve> unsupported = collect_unsupported_curves(*pane);
      if (!unsupported.empty()) {
        ImGui::SeparatorText("Unsupported");
        const size_t shown = std::min<size_t>(unsupported.size(), 4);
        for (size_t i = 0; i < shown; ++i) {
          ImGui::BulletText("%s: %s", unsupported[i].label.c_str(), unsupported[i].reason.c_str());
        }
        if (unsupported.size() > shown) {
          ImGui::TextDisabled("+%zu more", unsupported.size() - shown);
        }
      }
    }

    if (ImGui::Button("Custom Series", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
      state->open_custom_series = true;
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

std::optional<UnsupportedCurve> unsupported_curve(const Curve &curve) {
  if (!curve.visible) {
    return std::nullopt;
  }
  if (curve_has_samples(curve)) {
    return std::nullopt;
  }
  if (!curve.name.empty() && curve.name.front() != '/') {
    return UnsupportedCurve{curve_display_name(curve), "custom math not implemented"};
  }
  if (!curve.name.empty()) {
    return UnsupportedCurve{curve_display_name(curve), "route has no data for this path"};
  }
  return UnsupportedCurve{curve_display_name(curve), "curve is not implemented"};
}

std::vector<UnsupportedCurve> collect_unsupported_curves(const Pane &pane) {
  std::vector<UnsupportedCurve> unsupported;
  unsupported.reserve(pane.curves.size());
  for (const Curve &curve : pane.curves) {
    if (auto status = unsupported_curve(curve); status.has_value()) {
      unsupported.push_back(std::move(*status));
    }
  }
  return unsupported;
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

PlotBounds compute_plot_bounds(const Pane &pane) {
  PlotBounds bounds;

  const bool explicit_x = pane.range.valid && pane.range.right > pane.range.left;
  const bool explicit_y = pane.range.valid && pane.range.top != pane.range.bottom;
  if (explicit_x) {
    bounds.x_min = pane.range.left;
    bounds.x_max = pane.range.right;
  } else {
    bool found = false;
    double min_value = 0.0;
    double max_value = 1.0;
    for (const Curve &curve : pane.curves) {
      if (!curve.visible || !curve_has_samples(curve)) {
        continue;
      }
      extend_range(curve.xs, &found, &min_value, &max_value);
    }
    if (!found) {
      min_value = 0.0;
      max_value = 1.0;
    }
    if (max_value <= min_value) {
      max_value = min_value + 1.0;
    }
    bounds.x_min = min_value;
    bounds.x_max = max_value;
  }

  if (explicit_y) {
    bounds.y_min = std::min(pane.range.bottom, pane.range.top);
    bounds.y_max = std::max(pane.range.bottom, pane.range.top);
  } else {
    bool found = false;
    double min_value = 0.0;
    double max_value = 1.0;
    for (const Curve &curve : pane.curves) {
      if (!curve.visible || !curve_has_samples(curve)) {
        continue;
      }
      extend_range(curve.ys, &found, &min_value, &max_value);
    }
    if (!found) {
      min_value = 0.0;
      max_value = 1.0;
    }
    ensure_non_degenerate_range(&min_value, &max_value, 0.06, 0.1);
    bounds.y_min = min_value;
    bounds.y_max = max_value;
  }

  return bounds;
}

void draw_plot(const Pane &pane, bool reset_plot_view) {
  const PlotBounds bounds = compute_plot_bounds(pane);
  const std::vector<UnsupportedCurve> unsupported = collect_unsupported_curves(pane);
  const int supported_count = static_cast<int>(std::count_if(
    pane.curves.begin(), pane.curves.end(), [](const Curve &curve) { return curve.visible && curve_has_samples(curve); }));
  const float status_height = unsupported.empty()
    ? 0.0f
    : std::min(86.0f, 28.0f + 18.0f * static_cast<float>(std::min<size_t>(unsupported.size(), 3)));
  ImVec2 plot_size = ImGui::GetContentRegionAvail();
  if (status_height > 0.0f) {
    plot_size.y = std::max(48.0f, plot_size.y - status_height - 6.0f);
  }

  ImPlot::PushStyleColor(ImPlotCol_PlotBg, color_rgb(255, 255, 255));
  ImPlot::PushStyleColor(ImPlotCol_PlotBorder, color_rgb(186, 190, 196));
  ImPlot::PushStyleColor(ImPlotCol_LegendBg, color_rgb(248, 249, 251, 0.92f));
  ImPlot::PushStyleColor(ImPlotCol_LegendBorder, color_rgb(168, 175, 184));
  ImPlot::PushStyleColor(ImPlotCol_AxisGrid, color_rgb(226, 231, 236));
  ImPlot::PushStyleColor(ImPlotCol_AxisText, color_rgb(95, 103, 112));

  ImPlotFlags plot_flags = ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText;
  if (supported_count == 0) {
    plot_flags |= ImPlotFlags_NoLegend;
  }

  const ImPlotAxisFlags axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  if (ImPlot::BeginPlot("##plot", plot_size, plot_flags)) {
    ImPlot::SetupAxes(nullptr, nullptr, axis_flags, axis_flags);
    const ImPlotCond axis_cond = reset_plot_view ? ImPlotCond_Always : ImPlotCond_Once;
    ImPlot::SetupAxisLimits(ImAxis_X1, bounds.x_min, bounds.x_max, axis_cond);
    ImPlot::SetupAxisLimits(ImAxis_Y1, bounds.y_min, bounds.y_max, axis_cond);
    if (supported_count > 0) {
      ImPlot::SetupLegend(ImPlotLocation_NorthEast);
    }

    for (size_t i = 0; i < pane.curves.size(); ++i) {
      const Curve &curve = pane.curves[i];
      if (!curve.visible || !curve_has_samples(curve)) {
        continue;
      }
      const std::string label = !curve.label.empty() ? curve.label : (!curve.name.empty() ? curve.name : "curve");
      std::string series_id = label + "##curve" + std::to_string(i);
      ImPlotSpec spec;
      spec.LineColor = color_rgb(curve.color);
      spec.LineWeight = curve.derivative ? 1.8f : 2.3f;
      spec.Flags = ImPlotLineFlags_SkipNaN;
      if (!curve.xs.empty() && curve.xs.size() == curve.ys.size()) {
        ImPlot::PlotLine(series_id.c_str(), curve.xs.data(), curve.ys.data(), static_cast<int>(curve.xs.size()), spec);
      }
    }
    ImPlot::EndPlot();
  }
  ImPlot::PopStyleColor(6);

  if (unsupported.empty()) {
    return;
  }

  if (ImGui::BeginChild("##unsupported_curves", ImVec2(0.0f, status_height), true,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::TextDisabled("Unsupported in current renderer:");
    const size_t shown = std::min<size_t>(unsupported.size(), 3);
    for (size_t i = 0; i < shown; ++i) {
      const UnsupportedCurve &curve = unsupported[i];
      ImGui::BulletText("%s: %s", curve.label.c_str(), curve.reason.c_str());
    }
    if (unsupported.size() > shown) {
      ImGui::TextDisabled("+%zu more", unsupported.size() - shown);
    }
  }
  ImGui::EndChild();
}

ImGuiDir dock_direction(SplitOrientation orientation) {
  return orientation == SplitOrientation::Horizontal ? ImGuiDir_Left : ImGuiDir_Up;
}

void build_dock_tree(const WorkspaceNode &node, const WorkspaceTab &tab, int tab_index, ImGuiID dock_id) {
  if (node.is_pane) {
    if (node.pane_index >= 0 && node.pane_index < static_cast<int>(tab.panes.size())) {
      ImGui::DockBuilderDockWindow(pane_window_name(tab_index, node.pane_index, tab.panes[static_cast<size_t>(node.pane_index)]).c_str(), dock_id);
    }
    return;
  }
  if (node.children.empty()) {
    return;
  }
  if (node.children.size() == 1) {
    build_dock_tree(node.children.front(), tab, tab_index, dock_id);
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
    build_dock_tree(node.children[i], tab, tab_index, child_id);
    current = remainder_id;
    remaining = std::max(0.0f, remaining - child_size);
  }
  build_dock_tree(node.children.back(), tab, tab_index, current);
}

void ensure_dockspace(const WorkspaceTab &tab, int tab_index, TabUiState *tab_state, ImVec2 dockspace_size) {
  if (dockspace_size.x <= 0.0f || dockspace_size.y <= 0.0f || tab_state == nullptr) {
    return;
  }
  if (!tab_state->dock_needs_build) {
    return;
  }

  const ImGuiID dockspace_id = dockspace_id_for_tab(tab_index);
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);
  build_dock_tree(tab.root, tab, tab_index, dockspace_id);
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(250, 250, 251));
    ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(194, 198, 204));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, color_rgb(252, 252, 253));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, color_rgb(252, 252, 253));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, color_rgb(252, 252, 253));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    const std::string window_name = pane_window_name(state->active_tab_index, static_cast<int>(i), pane);
    const bool opened = ImGui::Begin(window_name.c_str(), nullptr, flags);
    if (opened) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
          || (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(0))) {
        tab_state->active_pane_index = static_cast<int>(i);
      }
      draw_plot(pane, state->reset_plot_view);
    }
    ImGui::End();
    ImGui::PopStyleColor(5);
  }
}

void draw_workspace(const AppSession &session, const UiMetrics &ui, UiState *state) {
  ImGui::SetNextWindowPos(ImVec2(ui.content_x, ui.content_y));
  ImGui::SetNextWindowSize(ImVec2(ui.content_w, ui.content_h));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(244, 246, 248));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(186, 191, 198));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##workspace_host", nullptr, flags)) {
    if (ImGui::BeginTabBar("##layout_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
      for (size_t i = 0; i < session.layout.tabs.size(); ++i) {
        const WorkspaceTab &tab = session.layout.tabs[i];
        const bool selected = static_cast<int>(i) == state->active_tab_index;
        const bool opened = ImGui::BeginTabItem(tab.tab_name.empty() ? "tab" : tab.tab_name.c_str(),
                                                nullptr,
                                                selected ? ImGuiTabItemFlags_SetSelected : 0);
        if (opened) {
          state->active_tab_index = static_cast<int>(i);
          if (i < state->tabs.size()) {
            ensure_dockspace(tab, static_cast<int>(i), &state->tabs[i], ImGui::GetContentRegionAvail());
          }
          ImGui::DockSpace(dockspace_id_for_tab(static_cast<int>(i)), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
}

bool reload_session(AppSession *session, UiState *state, const std::string &route_name, const std::string &data_dir) {
  try {
    SketchLayout new_layout = load_sketch_layout(session->layout_path, route_name, data_dir);
    session->route_name = route_name;
    session->data_dir = data_dir;
    session->layout = std::move(new_layout);
    sync_ui_state(state, session->layout);
    mark_docks_dirty(state);
    sync_route_buffers(state, *session);
    state->status_text = "Loaded route " + route_name;
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to load route";
    return false;
  }
}

void draw_popups(AppSession *session, UiState *state) {
  if (state->open_about) {
    ImGui::OpenPopup("About JotPlugger");
    state->open_about = false;
  }
  if (state->open_custom_series) {
    ImGui::OpenPopup("Custom Series");
    state->open_custom_series = false;
  }
  if (state->open_open_route) {
    ImGui::OpenPopup("Open Route");
    state->open_open_route = false;
  }
  if (state->open_save_screenshot) {
    ImGui::OpenPopup("Save Screenshot");
    state->open_save_screenshot = false;
  }

  show_not_implemented_modal(
    "About JotPlugger",
    "About JotPlugger",
    "JotPlugger is the in-progress native Dear ImGui replacement for PlotJuggler in tools/jotpluggler.");
  show_not_implemented_modal(
    "Custom Series",
    "Custom Series",
    "Custom series editing is not implemented yet.");
  show_not_implemented_modal(
    "Save Screenshot",
    "Save Screenshot",
    "Screenshot export is currently driven by the CLI with --output <png>.");
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
  if (state->open_error_popup) {
    ImGui::OpenPopup("Route Error");
    state->open_error_popup = false;
  }
  if (ImGui::BeginPopupModal("Route Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", state->error_text.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void render_layout(AppSession *session, UiState *state) {
  const float menu_height = draw_main_menu_bar(state);
  const float toolbar_height = draw_toolbar(*session, state, menu_height);
  const UiMetrics ui = compute_ui_metrics(ImGui::GetMainViewport()->Size, menu_height + toolbar_height);
  draw_workspace_background(ui);
  draw_sidebar(session, ui, state);
  draw_workspace(*session, ui, state);
  draw_pane_windows(session, state);
  draw_status_bar(ui, *state);
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
  render_layout(session, state);
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
  if (state->request_reload) {
    reload_session(session, state, session->route_name, session->data_dir);
    state->request_reload = false;
  }
  state->reset_plot_view = false;
}

int run_app(const Options &options) {
  const fs::path layout_path = resolve_layout_path(options.layout);
  AppSession session = {
    .layout_path = layout_path,
    .route_name = options.route_name,
    .data_dir = options.data_dir,
    .layout = load_sketch_layout(layout_path, options.route_name, options.data_dir),
  };
  GlfwRuntime glfw_runtime(options);
  ImGuiRuntime imgui_runtime(glfw_runtime.window());
  configure_style();
  UiState ui_state;
  sync_ui_state(&ui_state, session.layout);
  sync_route_buffers(&ui_state, session);

  const bool should_capture = !options.output_path.empty();
  const fs::path output_path = should_capture ? fs::path(options.output_path) : fs::path();
  if (options.show) {
    bool captured = false;
    while (!glfwWindowShouldClose(glfw_runtime.window())) {
      const fs::path *capture_path = (!captured && should_capture) ? &output_path : nullptr;
      render_frame(glfw_runtime.window(), &session, &ui_state, capture_path);
      captured = captured || should_capture;
    }
    return 0;
  }

  render_frame(glfw_runtime.window(), &session, &ui_state, nullptr);
  if (should_capture) {
    render_frame(glfw_runtime.window(), &session, &ui_state, &output_path);
  }
  return 0;
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
