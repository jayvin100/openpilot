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
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

struct AppSession {
  fs::path layout_path;
  std::string route_name;
  std::string data_dir;
  SketchLayout layout;
  RouteData route_data;
  std::unordered_map<std::string, const RouteSeries *> series_by_path;
  std::vector<BrowserNode> browser_nodes;
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
  bool request_new_tab = false;
  bool request_duplicate_tab = false;
  bool request_close_tab = false;
  bool follow_latest = false;
  bool has_shared_range = false;
  bool has_hover_time = false;
  int active_tab_index = 0;
  std::vector<TabUiState> tabs;
  std::array<char, 128> route_buffer = {};
  std::array<char, 128> browser_filter = {};
  std::array<char, 512> data_dir_buffer = {};
  std::string selected_browser_path;
  std::string error_text;
  bool open_error_popup = false;
  std::string status_text = "Ready";
  double route_x_min = 0.0;
  double route_x_max = 1.0;
  double x_view_min = 0.0;
  double x_view_max = 1.0;
  double hovered_time = 0.0;
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

class TerminalRouteProgress {
public:
  explicit TerminalRouteProgress(bool enabled) : enabled_(enabled) {}

  void update(const RouteLoadProgress &progress) {
    if (!enabled_) {
      return;
    }

    if (progress.segment_index != active_segment_) {
      active_segment_ = progress.segment_index;
      saw_download_ = false;
    }

    double overall = 0.0;
    std::string detail = "Resolving route";
    if (progress.stage == RouteLoadStage::Finished) {
      overall = 1.0;
      detail = "Ready";
    } else if (progress.segment_count > 0) {
      double segment_fraction = 0.0;
      if (progress.total > 0) {
        segment_fraction = std::clamp(progress.current / static_cast<double>(progress.total), 0.0, 1.0);
      }

      double within_segment = 0.0;
      if (progress.stage == RouteLoadStage::DownloadingSegment) {
        saw_download_ = true;
        within_segment = 0.35 * segment_fraction;
        detail = "Downloading segment " + std::to_string(progress.segment_index + 1) + "/" + std::to_string(progress.segment_count);
      } else if (progress.stage == RouteLoadStage::ParsingSegment) {
        within_segment = saw_download_ ? 0.35 + 0.65 * segment_fraction : segment_fraction;
        detail = "Parsing segment " + std::to_string(progress.segment_index + 1) + "/" + std::to_string(progress.segment_count);
      }
      overall = std::clamp((progress.segment_index + within_segment) / static_cast<double>(progress.segment_count), 0.0, 1.0);
    }

    render(overall, detail);
  }

  void finish() {
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
  bool saw_download_ = false;
  size_t active_segment_ = 0;
  int last_percent_ = -1;
  size_t last_line_width_ = 0;
  std::string last_detail_;
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
  style.Colors[ImGuiCol_Tab] = color_rgb(232, 236, 241);
  style.Colors[ImGuiCol_TabHovered] = color_rgb(240, 243, 246);
  style.Colors[ImGuiCol_TabSelected] = color_rgb(245, 247, 249);
  style.Colors[ImGuiCol_TabSelectedOverline] = color_rgb(176, 183, 192);
  style.Colors[ImGuiCol_TabDimmed] = color_rgb(227, 231, 236);
  style.Colors[ImGuiCol_TabDimmedSelected] = color_rgb(239, 242, 245);
  style.Colors[ImGuiCol_TabDimmedSelectedOverline] = color_rgb(176, 183, 192);
  style.Colors[ImGuiCol_DockingEmptyBg] = color_rgb(244, 246, 248);
  style.Colors[ImGuiCol_DockingPreview] = color_rgb(69, 115, 184, 0.22f);

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

void sync_ui_state(UiState *state, const SketchLayout &layout) {
  const bool initializing = state->tabs.empty();
  state->tabs.resize(layout.tabs.size());
  if (layout.tabs.empty()) {
    state->active_tab_index = 0;
    return;
  }
  if (initializing) {
    state->active_tab_index = std::clamp(layout.current_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  }
  state->active_tab_index = std::clamp(state->active_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  for (size_t i = 0; i < layout.tabs.size(); ++i) {
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

std::string pane_window_name(int tab_index, int pane_index, const Pane &pane) {
  std::string title = pane.title.empty() ? "plot" : pane.title;
  return title + "##tab" + std::to_string(tab_index) + "_pane" + std::to_string(pane_index);
}

ImGuiID dockspace_id_for_tab(int tab_index) {
  return ImHashStr(("jotpluggler_dockspace_" + std::to_string(tab_index)).c_str());
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
  SplitLeft,
  SplitRight,
  SplitTop,
  SplitBottom,
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

Pane make_empty_pane(const std::string &title = "plot") {
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
  layout.tabs.push_back(make_empty_tab("tab0"));
  layout.current_tab_index = 0;
  return layout;
}

bool tab_name_exists(const SketchLayout &layout, const std::string &name) {
  return std::any_of(layout.tabs.begin(), layout.tabs.end(), [&](const WorkspaceTab &tab) {
    return tab.tab_name == name;
  });
}

std::string next_tab_name(const SketchLayout &layout, const std::string &base_name) {
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

std::string route_summary(const AppSession &session) {
  std::string summary = session.route_name;
  if (!session.data_dir.empty()) {
    summary += " (local)";
  }
  return summary;
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

void rebuild_route_index(AppSession *session) {
  session->series_by_path.clear();
  for (const RouteSeries &series : session->route_data.series) {
    session->series_by_path.emplace(series.path, &series);
  }
}

const RouteSeries *find_route_series(const AppSession &session, const std::string &path) {
  auto it = session.series_by_path.find(path);
  return it == session.series_by_path.end() ? nullptr : it->second;
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
      return;
    }
  }

  state->x_view_min = state->route_x_min;
  state->x_view_max = state->route_x_max;
  if (state->x_view_max <= state->x_view_min) {
    state->x_view_max = state->x_view_min + 1.0;
  }
  state->has_shared_range = true;
}

void clamp_shared_range(UiState *state) {
  if (!state->has_shared_range) {
    return;
  }
  const double min_span = 0.1;
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
  const double span = std::max(0.1, state->x_view_max - state->x_view_min);
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
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Route...")) {
        state->open_open_route = true;
      }
      if (ImGui::MenuItem("Reload Route")) {
        state->request_reload = true;
        state->status_text = "Reloading route";
      }
      ImGui::Separator();
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
    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::MenuItem("Reset Plot View")) {
        state->reset_plot_view = true;
        state->status_text = "Plot view reset";
      }
      ImGui::Separator();
      if (ImGui::MenuItem("New Tab")) {
        state->request_new_tab = true;
      }
      if (ImGui::MenuItem("Duplicate Tab")) {
        state->request_duplicate_tab = true;
      }
      if (ImGui::MenuItem("Close Tab", nullptr, false, state->tabs.size() > 1)) {
        state->request_close_tab = true;
      }
      ImGui::Separator();
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

void draw_status_bar(const UiMetrics &ui, UiState *state) {
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
    if (ImGui::Button(state->streaming ? "Live" : "Ready", ImVec2(52.0f, 0.0f))) {
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state->status_text.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(54.0f, 0.0f))) {
      state->reset_plot_view = true;
      state->status_text = "Plot view reset";
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Follow", &state->follow_latest)) {
      state->status_text = state->follow_latest ? "Following latest samples" : "Follow disabled";
    }
    if (state->has_shared_range) {
      const double span = std::max(0.1, state->x_view_max - state->x_view_min);
      double center = 0.5 * (state->x_view_min + state->x_view_max);
      const double min_center = state->route_x_min + span * 0.5;
      const double max_center = state->route_x_max - span * 0.5;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(std::max(120.0f, ui.width - 600.0f));
      if (ImGui::SliderScalar("##timeline_center", ImGuiDataType_Double, &center, &min_center, &max_center, "%.1f s")) {
        state->x_view_min = center - span * 0.5;
        state->x_view_max = center + span * 0.5;
        state->follow_latest = false;
      }
      ImGui::SameLine();
      double span_value = span;
      const double max_span = std::max(span, state->route_x_max - state->route_x_min);
      const double min_span = 0.1;
      ImGui::SetNextItemWidth(90.0f);
      if (ImGui::DragScalar("Span", ImGuiDataType_Double, &span_value, 0.1, &min_span, &max_span, "%.1f")) {
        span_value = std::clamp(span_value, min_span, max_span);
        const double current_center = 0.5 * (state->x_view_min + state->x_view_max);
        state->x_view_min = current_center - span_value * 0.5;
        state->x_view_max = current_center + span_value * 0.5;
        state->follow_latest = false;
        clamp_shared_range(state);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("x %.1f to %.1f", state->x_view_min, state->x_view_max);
      if (state->has_hover_time) {
        ImGui::SameLine();
        ImGui::TextDisabled("| cursor %.3f", state->hovered_time);
      }
    }
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

void draw_browser_node(AppSession *session, const BrowserNode &node, UiState *state, const std::string &filter) {
  if (!browser_node_matches(node, filter)) {
    return;
  }

  if (node.children.empty()) {
    const bool selected = state->selected_browser_path == node.full_path;
    if (ImGui::Selectable(node.label.c_str(), selected)) {
      state->selected_browser_path = node.full_path;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      state->selected_browser_path = node.full_path;
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
      draw_browser_node(session, child, state, filter);
    }
    ImGui::TreePop();
  }
}

void draw_sidebar(AppSession *session, const UiMetrics &ui, UiState *state) {
  const WorkspaceTab *tab = active_tab(session->layout, *state);
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

    ImGui::SeparatorText("Timeline");
    if (ImGui::Checkbox("Follow latest", &state->follow_latest)) {
      state->status_text = state->follow_latest ? "Following latest samples" : "Follow disabled";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View", ImVec2(88.0f, 0.0f))) {
      state->reset_plot_view = true;
      state->status_text = "Plot view reset";
    }
    if (state->has_shared_range) {
      ImGui::TextDisabled("View %.2f to %.2f", state->x_view_min, state->x_view_max);
    }
    if (session->route_data.has_time_range) {
      ImGui::TextDisabled("Route %.2f to %.2f", session->route_data.x_min, session->route_data.x_max);
    }
    if (state->has_hover_time) {
      ImGui::TextDisabled("Cursor %.3f", state->hovered_time);
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Timeseries List");
    ImGui::InputTextWithHint("##browser_filter", "filter paths", state->browser_filter.data(), state->browser_filter.size());
    ImGui::TextDisabled("%zu available", session->route_data.paths.size());
    if (ImGui::BeginChild("##timeseries_browser", ImVec2(0.0f, 170.0f), true)) {
      const std::string filter = string_from_buffer(state->browser_filter);
      for (const BrowserNode &node : session->browser_nodes) {
        draw_browser_node(session, node, state, filter);
      }
    }
    ImGui::EndChild();

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

std::string path_curve_label(std::string_view path) {
  if (path.empty() || path.front() != '/') {
    return std::string(path);
  }
  std::vector<std::string_view> parts;
  size_t start = 1;
  while (start < path.size()) {
    const size_t end = path.find('/', start);
    parts.push_back(path.substr(start, end == std::string_view::npos ? path.size() - start : end - start));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  if (parts.size() >= 2) {
    const std::string_view parent = parts[parts.size() - 2];
    if (parent.find("canState") == 0 || std::all_of(parent.begin(), parent.end(), ::isdigit)) {
      return std::string(parent) + "/" + std::string(parts.back());
    }
  }
  return parts.empty() ? std::string(path) : std::string(parts.back());
}

Curve make_curve_for_path(const Pane &pane, const std::string &path) {
  Curve curve;
  curve.name = path;
  curve.label = path_curve_label(path);
  curve.color = next_curve_color(pane);
  return curve;
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
  if (pane.title.empty() || pane.title == "plot") {
    pane.title = curve_display_name(curve);
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
  state->status_text = inserted ? "Added " + path : "Curve already present";
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
    if (new_pane.title == "plot") {
      new_pane.title = curve_display_name(*curve);
    }
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
  if (tab->panes.size() <= 1 || pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
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
  pane.title = "plot";
}

void create_runtime_tab(SketchLayout *layout, UiState *state) {
  const std::string tab_name = next_tab_name(*layout, "tab");
  layout->tabs.push_back(make_empty_tab(tab_name));
  state->tabs.push_back(TabUiState{.dock_needs_build = true, .active_pane_index = 0});
  state->active_tab_index = static_cast<int>(layout->tabs.size()) - 1;
  mark_all_docks_dirty(state);
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
  state->tabs.push_back(TabUiState{.dock_needs_build = true, .active_pane_index = active_pane_index});
  state->active_tab_index = static_cast<int>(layout->tabs.size()) - 1;
  mark_all_docks_dirty(state);
  state->status_text = "Duplicated tab";
}

void close_runtime_tab(SketchLayout *layout, UiState *state) {
  if (layout->tabs.size() <= 1) {
    return;
  }
  const int tab_index = std::clamp(state->active_tab_index, 0, static_cast<int>(layout->tabs.size()) - 1);
  layout->tabs.erase(layout->tabs.begin() + static_cast<std::ptrdiff_t>(tab_index));
  if (tab_index < static_cast<int>(state->tabs.size())) {
    state->tabs.erase(state->tabs.begin() + static_cast<std::ptrdiff_t>(tab_index));
  }
  if (state->active_tab_index >= static_cast<int>(layout->tabs.size())) {
    state->active_tab_index = static_cast<int>(layout->tabs.size()) - 1;
  }
  sync_ui_state(state, *layout);
  mark_all_docks_dirty(state);
  state->status_text = "Closed tab";
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

PlotBounds compute_plot_bounds(const Pane &pane,
                               const std::vector<PreparedCurve> &prepared_curves,
                               const UiState &state) {
  PlotBounds bounds;
  bounds.x_min = state.has_shared_range ? state.x_view_min : 0.0;
  bounds.x_max = state.has_shared_range ? state.x_view_max : 1.0;
  if (bounds.x_max <= bounds.x_min) {
    bounds.x_max = bounds.x_min + 1.0;
  }

  if (pane.range.valid && pane.range.top != pane.range.bottom) {
    bounds.y_min = std::min(pane.range.bottom, pane.range.top);
    bounds.y_max = std::max(pane.range.bottom, pane.range.top);
    return bounds;
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
  ensure_non_degenerate_range(&min_value, &max_value, 0.06, 0.1);
  bounds.y_min = min_value;
  bounds.y_max = max_value;
  return bounds;
}

void draw_plot(const AppSession &session, const Pane &pane, UiState *state) {
  std::vector<PreparedCurve> prepared_curves;
  prepared_curves.reserve(pane.curves.size());
  const int max_points = std::max(256, static_cast<int>(ImGui::GetContentRegionAvail().x) * 2);
  for (const Curve &curve : pane.curves) {
    if (!curve.visible || !curve_has_samples(session, curve)) {
      continue;
    }
    PreparedCurve prepared;
    if (build_curve_series(session, curve, *state, max_points, &prepared)) {
      prepared_curves.push_back(std::move(prepared));
    }
  }

  const PlotBounds bounds = compute_plot_bounds(pane, prepared_curves, *state);
  const int supported_count = static_cast<int>(prepared_curves.size());
  const ImVec2 plot_size = ImGui::GetContentRegionAvail();

  ImPlot::PushStyleColor(ImPlotCol_PlotBg, color_rgb(255, 255, 255));
  ImPlot::PushStyleColor(ImPlotCol_PlotBorder, color_rgb(186, 190, 196));
  ImPlot::PushStyleColor(ImPlotCol_LegendBg, color_rgb(248, 249, 251, 0.92f));
  ImPlot::PushStyleColor(ImPlotCol_LegendBorder, color_rgb(168, 175, 184));
  ImPlot::PushStyleColor(ImPlotCol_AxisGrid, color_rgb(226, 231, 236));
  ImPlot::PushStyleColor(ImPlotCol_AxisText, color_rgb(95, 103, 112));

  ImPlotFlags plot_flags = ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_Crosshairs;
  if (supported_count == 0) {
    plot_flags |= ImPlotFlags_NoLegend;
  }

  const ImPlotAxisFlags x_axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  ImPlotAxisFlags y_axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  const bool explicit_y = pane.range.valid && pane.range.top != pane.range.bottom;
  if (!explicit_y && supported_count > 0) {
    y_axis_flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
  }

  const double previous_x_min = state->x_view_min;
  const double previous_x_max = state->x_view_max;
  if (ImPlot::BeginPlot("##plot", plot_size, plot_flags)) {
    ImPlot::SetupAxes(nullptr, nullptr, x_axis_flags, y_axis_flags);
    ImPlot::SetupAxisFormat(ImAxis_X1, "%.1f s");
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g");
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
    if (state->has_hover_time) {
      ImPlotSpec cursor_spec;
      cursor_spec.LineColor = color_rgb(108, 118, 128, 0.7f);
      cursor_spec.LineWeight = 1.0f;
      cursor_spec.Flags = ImPlotItemFlags_NoLegend;
      ImPlot::PlotInfLines("##hover_cursor", &state->hovered_time, 1, cursor_spec);
    }
    if (ImPlot::IsPlotHovered()) {
      state->hovered_time = ImPlot::GetPlotMousePos().x;
      state->has_hover_time = true;
    }
    ImPlot::EndPlot();
  }
  clamp_shared_range(state);
  if (std::abs(state->x_view_min - previous_x_min) > 1.0e-6
      || std::abs(state->x_view_max - previous_x_max) > 1.0e-6) {
    if (!state->reset_plot_view) {
      state->follow_latest = false;
    }
  }
  ImPlot::PopStyleColor(6);
}

std::optional<PaneMenuAction> draw_pane_context_menu(const WorkspaceTab &tab, int pane_index) {
  if (!ImGui::BeginPopupContextWindow("##pane_context")) {
    return std::nullopt;
  }

  PaneMenuAction action;
  action.pane_index = pane_index;
  if (ImGui::MenuItem("Split Left")) {
    action.kind = PaneMenuActionKind::SplitLeft;
  } else if (ImGui::MenuItem("Split Right")) {
    action.kind = PaneMenuActionKind::SplitRight;
  } else if (ImGui::MenuItem("Split Top")) {
    action.kind = PaneMenuActionKind::SplitTop;
  } else if (ImGui::MenuItem("Split Bottom")) {
    action.kind = PaneMenuActionKind::SplitBottom;
  } else if (ImGui::MenuItem("Clear Pane")) {
    action.kind = PaneMenuActionKind::Clear;
  } else if (ImGui::MenuItem("Close Pane", nullptr, false, tab.panes.size() > 1)) {
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

  switch (action.kind) {
    case PaneMenuActionKind::SplitLeft:
      if (split_pane(tab, pane_index, PaneDropZone::Left)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      }
      break;
    case PaneMenuActionKind::SplitRight:
      if (split_pane(tab, pane_index, PaneDropZone::Right)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      }
      break;
    case PaneMenuActionKind::SplitTop:
      if (split_pane(tab, pane_index, PaneDropZone::Top)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      }
      break;
    case PaneMenuActionKind::SplitBottom:
      if (split_pane(tab, pane_index, PaneDropZone::Bottom)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      }
      break;
    case PaneMenuActionKind::Clear:
      clear_pane(tab, pane_index);
      tab_state->active_pane_index = pane_index;
      break;
    case PaneMenuActionKind::Close:
      if (close_pane(tab, pane_index)) {
        tab_state->active_pane_index = std::clamp(pane_index, 0, static_cast<int>(tab->panes.size()) - 1);
      }
      break;
    case PaneMenuActionKind::None:
      return false;
  }

  mark_tab_dock_dirty(state, state->active_tab_index);
  state->status_text = "Workspace updated";
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
        mark_tab_dock_dirty(state, state->active_tab_index);
      }
      return ok;
    }
    Pane &target = tab->panes[static_cast<size_t>(action.target_pane_index)];
    Curve curve = make_curve_for_path(target, action.browser_path);
    if (split_pane(tab, action.target_pane_index, action.zone, curve)) {
      tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      mark_tab_dock_dirty(state, state->active_tab_index);
      state->status_text = "Split pane and added " + action.browser_path;
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
    mark_tab_dock_dirty(state, state->active_tab_index);
    state->status_text = inserted ? "Added " + curve_display_name(curve) : "Curve already present";
    return true;
  }
  if (split_pane(tab, action.target_pane_index, action.zone, curve)) {
    tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
    mark_tab_dock_dirty(state, state->active_tab_index);
    state->status_text = "Split pane and added " + curve_display_name(curve);
    return true;
  }
  return false;
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
    std::optional<PaneMenuAction> menu_action;
    std::optional<PaneDropAction> drop_action;
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
      draw_plot(*session, pane, state);
      menu_action = draw_pane_context_menu(*tab, static_cast<int>(i));
      drop_action = draw_pane_drop_target(state->active_tab_index, static_cast<int>(i));
    }
    ImGui::End();
    ImGui::PopStyleColor(5);
    if (menu_action.has_value() && apply_pane_menu_action(session, state, static_cast<int>(i), *menu_action)) {
      return;
    }
    if (drop_action.has_value() && apply_pane_drop_action(session, state, *drop_action)) {
      return;
    }
  }
}

void draw_workspace(AppSession *session, const UiMetrics &ui, UiState *state) {
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
    if (ImGui::BeginTabBar("##layout_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
      enum class TabActionKind {
        None,
        New,
        Duplicate,
        Close,
      };
      TabActionKind pending_action = TabActionKind::None;
      int pending_tab_index = -1;
      for (size_t i = 0; i < session->layout.tabs.size(); ++i) {
        const WorkspaceTab &tab = session->layout.tabs[i];
        const bool selected = static_cast<int>(i) == state->active_tab_index;
        const bool opened = ImGui::BeginTabItem(tab.tab_name.empty() ? "tab" : tab.tab_name.c_str(),
                                                nullptr,
                                                selected ? ImGuiTabItemFlags_SetSelected : 0);
        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::MenuItem("New Tab")) {
            pending_action = TabActionKind::New;
          }
          if (ImGui::MenuItem("Duplicate Tab")) {
            pending_action = TabActionKind::Duplicate;
            pending_tab_index = static_cast<int>(i);
          }
          if (ImGui::MenuItem("Close Tab", nullptr, false, session->layout.tabs.size() > 1)) {
            pending_action = TabActionKind::Close;
            pending_tab_index = static_cast<int>(i);
          }
          ImGui::EndPopup();
        }
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

      if (state->request_new_tab || pending_action == TabActionKind::New) {
        create_runtime_tab(&session->layout, state);
        state->request_new_tab = false;
      } else if (state->request_duplicate_tab || pending_action == TabActionKind::Duplicate) {
        if (pending_tab_index >= 0) {
          state->active_tab_index = pending_tab_index;
        }
        duplicate_runtime_tab(&session->layout, state);
        state->request_duplicate_tab = false;
      } else if (state->request_close_tab || pending_action == TabActionKind::Close) {
        if (pending_tab_index >= 0) {
          state->active_tab_index = pending_tab_index;
        }
        close_runtime_tab(&session->layout, state);
        state->request_close_tab = false;
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

void rebuild_session_route_data(AppSession *session, UiState *state,
                                const RouteLoadProgressCallback &progress = {}) {
  session->route_data = load_route_data(session->route_name, session->data_dir, progress);
  rebuild_route_index(session);
  session->browser_nodes = build_browser_tree(session->route_data.paths);
  if (!state->selected_browser_path.empty() && find_route_series(*session, state->selected_browser_path) == nullptr) {
    state->selected_browser_path.clear();
  }
  state->has_shared_range = false;
  state->has_hover_time = false;
  reset_shared_range(state, *session);
}

bool reload_session(AppSession *session, UiState *state, const std::string &route_name, const std::string &data_dir) {
  try {
    session->route_name = route_name;
    session->data_dir = data_dir;
    rebuild_session_route_data(session, state);
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
  ensure_shared_range(state, *session);
  if (state->reset_plot_view) {
    reset_shared_range(state, *session);
  } else if (state->follow_latest) {
    update_follow_range(state);
  } else {
    clamp_shared_range(state);
  }
  const float menu_height = draw_main_menu_bar(state);
  const UiMetrics ui = compute_ui_metrics(ImGui::GetMainViewport()->Size, menu_height);
  draw_sidebar(session, ui, state);
  draw_workspace(session, ui, state);
  draw_pane_windows(session, state);
  draw_status_bar(ui, state);
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
  const fs::path layout_path = options.layout.empty() ? fs::path() : resolve_layout_path(options.layout);
  AppSession session = {
    .layout_path = layout_path,
    .route_name = options.route_name,
    .data_dir = options.data_dir,
    .layout = options.layout.empty() ? make_empty_layout() : load_sketch_layout(layout_path),
  };
  UiState ui_state;
  sync_ui_state(&ui_state, session.layout);
  TerminalRouteProgress route_progress(::isatty(STDERR_FILENO) != 0);
  rebuild_session_route_data(&session, &ui_state, [&](const RouteLoadProgress &update) {
    route_progress.update(update);
  });
  route_progress.finish();
  sync_route_buffers(&ui_state, session);

  GlfwRuntime glfw_runtime(options);
  ImGuiRuntime imgui_runtime(glfw_runtime.window());
  configure_style();

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
