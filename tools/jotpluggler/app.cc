#include "tools/jotpluggler/jotpluggler.h"
#include "tools/jotpluggler/app_camera.h"
#include "tools/jotpluggler/app_map.h"
#include "system/hardware/hw.h"
#include "imgui_impl_glfw.h"

#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <unistd.h>

#include "third_party/json11/json11.hpp"

namespace fs = std::filesystem;


constexpr const char *UNTITLED_PANE_TITLE = "...";

constexpr float SIDEBAR_WIDTH = 320.0f;
constexpr float SIDEBAR_MIN_WIDTH = 220.0f;
constexpr float SIDEBAR_MAX_WIDTH = 520.0f;
constexpr float TIMELINE_BAR_HEIGHT = 14.0f;
constexpr float STATUS_BAR_HEIGHT = 52.0f;
constexpr double MIN_HORIZONTAL_ZOOM_SECONDS = 2.0;
constexpr double PLOT_Y_PAD_FRACTION = 0.4;
ImFont *g_ui_font = nullptr;
ImFont *g_ui_bold_font = nullptr;
ImFont *g_mono_font = nullptr;

struct UiMetrics {
  float width = 0.0f;
  float height = 0.0f;
  float top_offset = 0.0f;
  float sidebar_width = SIDEBAR_WIDTH;
  float content_x = 0.0f;
  float content_y = 0.0f;
  float content_w = 0.0f;
  float content_h = 0.0f;
  float status_bar_y = 0.0f;
};

struct PlotBounds {
  double x_min = 0.0;
  double x_max = 1.0;
  double y_min = 0.0;
  double y_max = 1.0;
};

const fs::path &repo_root() {
  static const fs::path root = []() -> fs::path {
#ifdef JOTP_REPO_ROOT
    return JOTP_REPO_ROOT;
#else
    std::array<char, 4096> buf = {};
    const ssize_t count = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (count <= 0) throw std::runtime_error("Unable to resolve executable path");
    return fs::path(std::string(buf.data(), static_cast<size_t>(count))).parent_path().parent_path().parent_path();
#endif
  }();
  return root;
}

std::optional<fs::path> jetbrains_mono_font_path() {
  const char *home = std::getenv("HOME");
  std::vector<fs::path> candidates;
  if (home != nullptr) {
    candidates.push_back(fs::path(home) / ".local/share/fonts/fonts/ttf/JetBrainsMono-Regular.ttf");
    candidates.push_back(fs::path(home) / ".local/share/fonts/fonts/variable/JetBrainsMono[wght].ttf");
  }
  candidates.push_back(fs::path("/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf"));
  for (const fs::path &candidate : candidates) {
    if (fs::exists(candidate)) return candidate;
  }
  return std::nullopt;
}

std::optional<fs::path> inter_font_path() {
  std::vector<fs::path> candidates = {
    repo_root() / "selfdrive" / "assets" / "fonts" / "Inter-Regular.ttf",
    repo_root() / "selfdrive" / "ui" / "installer" / "inter-ascii.ttf",
  };
  for (const fs::path &candidate : candidates) {
    if (fs::exists(candidate)) return candidate;
  }
  return std::nullopt;
}

std::optional<fs::path> inter_semibold_font_path() {
  const fs::path candidate = repo_root() / "selfdrive" / "assets" / "fonts" / "Inter-SemiBold.ttf";
  if (fs::exists(candidate)) {
    return candidate;
  }
  return std::nullopt;
}

std::string layout_name_from_arg(const std::string &layout_arg) {
  const fs::path raw(layout_arg);
  if (raw.extension() == ".xml" || raw.extension() == ".json") {
    return raw.stem().string();
  }
  if (raw.filename() != raw) return raw.filename().replace_extension("").string();
  fs::path stem_path = raw;
  return stem_path.replace_extension("").string();
}

fs::path resolve_layout_path(const std::string &layout_arg) {
  const fs::path direct(layout_arg);
  if (fs::exists(direct)) {
    if (direct.extension() == ".json") return fs::absolute(direct);
    const fs::path sibling_json = direct.parent_path() / (direct.stem().string() + ".json");
    if (direct.extension() == ".xml" && fs::exists(sibling_json)) {
      return fs::absolute(sibling_json);
    }
  }
  const fs::path candidate = repo_root() / "tools" / "jotpluggler" / "layouts" / (layout_name_from_arg(layout_arg) + ".json");
  if (!fs::exists(candidate)) throw std::runtime_error("Unknown layout: " + layout_arg);
  return candidate;
}

fs::path layouts_dir() {
  return repo_root() / "tools" / "jotpluggler" / "layouts";
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
  return autosave_dir() / (sanitize_layout_stem(stem) + ".json");
}

std::vector<std::string> scan_layout_names() {
  std::vector<std::string> names;
  const fs::path root = layouts_dir();
  if (!fs::exists(root) || !fs::is_directory(root)) {
    return names;
  }
  for (const auto &entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    names.push_back(entry.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool g_layout_names_dirty = true;

const std::vector<std::string> &available_layout_names() {
  static std::vector<std::string> cached;
  if (g_layout_names_dirty) {
    cached = scan_layout_names();
    g_layout_names_dirty = false;
  }
  return cached;
}

void run_or_throw(const std::string &command, const std::string &action) {
  const int ret = std::system(command.c_str());
  if (ret != 0) throw std::runtime_error(action + " failed with exit code " + std::to_string(ret));
}

bool reload_layout(AppSession *session, UiState *state, const std::string &layout_arg);
bool reload_session(AppSession *session, UiState *state, const std::string &route_name, const std::string &data_dir);
void reset_shared_range(UiState *state, const AppSession &session);
SketchLayout make_empty_layout();
void cancel_rename_tab(UiState *state);
void sync_ui_state(UiState *state, const SketchLayout &layout);
void sync_layout_buffers(UiState *state, const AppSession &session);
void mark_all_docks_dirty(UiState *state);
bool start_stream_session(AppSession *session,
                          UiState *state,
                          const std::string &address,
                          double buffer_seconds,
                          bool preserve_existing_data = false);
void stop_stream_session(AppSession *session, UiState *state, bool preserve_data = true);
bool apply_special_item_to_active_pane(AppSession *session, UiState *state, std::string_view item_id);

void start_new_layout(AppSession *session, UiState *state, const std::string &status_text = "New untitled layout") {
  session->layout = make_empty_layout();
  session->layout_path.clear();
  session->autosave_path.clear();
  state->undo.reset(session->layout);
  state->layout_dirty = false;
  state->status_text = status_text;
  state->tabs.clear();
  cancel_rename_tab(state);
  sync_ui_state(state, session->layout);
  sync_layout_buffers(state, *session);
  mark_all_docks_dirty(state);
  reset_shared_range(state, *session);
}

void apply_dbc_override_change(AppSession *session, UiState *state, const std::string &dbc_override) {
  session->dbc_override = dbc_override;
  if (session->data_mode == SessionDataMode::Stream) {
    start_stream_session(session, state, session->stream_address, session->stream_buffer_seconds, false);
  } else if (!session->route_name.empty()) {
    reload_session(session, state, session->route_name, session->data_dir);
  } else if (dbc_override.empty()) {
    state->status_text = "DBC auto-detect enabled";
  } else {
    state->status_text = "DBC set to " + dbc_override;
  }
}

void configure_style() {
  ImGui::StyleColorsLight();
  ImPlot::StyleColorsLight();

  ImGuiIO &io = ImGui::GetIO();
  g_ui_font = nullptr;
  g_ui_bold_font = nullptr;
  g_mono_font = nullptr;
  const std::optional<fs::path> ui_font_path = inter_font_path();
  const std::optional<fs::path> ui_bold_font_path = inter_semibold_font_path();
  const std::optional<fs::path> mono_font_path = jetbrains_mono_font_path();
  ImFontConfig font_cfg;
  font_cfg.OversampleH = 2;
  font_cfg.OversampleV = 2;
  font_cfg.RasterizerDensity = 1.0f;
  bootstrap_icons::loadFont(16.0f);
  const auto add_font_with_icons = [&](const fs::path &path, float size) -> ImFont * {
    ImFont *font = io.Fonts->AddFontFromFileTTF(path.c_str(), size, &font_cfg);
    if (font != nullptr) {
      bootstrap_icons::mergeFont(size);
    }
    return font;
  };
  if (ui_font_path.has_value()) {
    if (ImFont *font = add_font_with_icons(*ui_font_path, 16.0f); font != nullptr) {
      g_ui_font = font;
      io.FontDefault = font;
    }
  }
  if (ui_bold_font_path.has_value()) {
    g_ui_bold_font = add_font_with_icons(*ui_bold_font_path, 16.75f);
  }
  if (g_ui_font == nullptr && mono_font_path.has_value()) {
    if (ImFont *font = add_font_with_icons(*mono_font_path, 15.75f); font != nullptr) {
      g_mono_font = font;
      io.FontDefault = font;
    }
  }
  if (g_mono_font == nullptr && mono_font_path.has_value()) {
    g_mono_font = add_font_with_icons(*mono_font_path, 15.75f);
  }
  if (g_ui_bold_font == nullptr) {
    g_ui_bold_font = g_ui_font;
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
  style.WindowPadding = ImVec2(8.0f, 7.0f);
  style.FramePadding = ImVec2(6.0f, 3.0f);
  style.ItemSpacing = ImVec2(8.0f, 5.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 3.0f);
  struct ColorDef { ImGuiCol idx; int r, g, b; };
  constexpr ColorDef COLORS[] = {
    {ImGuiCol_WindowBg, 250, 250, 251},  {ImGuiCol_ChildBg, 255, 255, 255},
    {ImGuiCol_Border, 194, 198, 204},    {ImGuiCol_TitleBg, 252, 252, 253},
    {ImGuiCol_TitleBgActive, 252, 252, 253}, {ImGuiCol_TitleBgCollapsed, 252, 252, 253},
    {ImGuiCol_Text, 74, 80, 88},         {ImGuiCol_TextDisabled, 108, 118, 128},
    {ImGuiCol_Button, 255, 255, 255},    {ImGuiCol_ButtonHovered, 246, 248, 250},
    {ImGuiCol_ButtonActive, 238, 240, 244}, {ImGuiCol_FrameBg, 255, 255, 255},
    {ImGuiCol_FrameBgHovered, 248, 249, 251}, {ImGuiCol_FrameBgActive, 241, 244, 248},
    {ImGuiCol_Header, 243, 245, 248},    {ImGuiCol_HeaderHovered, 237, 240, 244},
    {ImGuiCol_HeaderActive, 232, 236, 240}, {ImGuiCol_PopupBg, 248, 249, 251},
    {ImGuiCol_MenuBarBg, 232, 236, 241}, {ImGuiCol_Separator, 194, 198, 204},
    {ImGuiCol_ScrollbarBg, 240, 242, 245}, {ImGuiCol_ScrollbarGrab, 202, 207, 214},
    {ImGuiCol_ScrollbarGrabHovered, 180, 186, 194}, {ImGuiCol_ScrollbarGrabActive, 164, 171, 180},
    {ImGuiCol_Tab, 219, 224, 230},       {ImGuiCol_TabHovered, 232, 236, 241},
    {ImGuiCol_TabSelected, 250, 251, 253}, {ImGuiCol_TabSelectedOverline, 92, 109, 136},
    {ImGuiCol_TabDimmed, 213, 219, 226}, {ImGuiCol_TabDimmedSelected, 244, 247, 249},
    {ImGuiCol_TabDimmedSelectedOverline, 92, 109, 136}, {ImGuiCol_DockingEmptyBg, 244, 246, 248},
  };
  for (const auto &c : COLORS) { style.Colors[c.idx] = color_rgb(c.r, c.g, c.b); }
  style.Colors[ImGuiCol_DockingPreview] = color_rgb(69, 115, 184, 0.22f);

  ImPlotStyle &plot_style = ImPlot::GetStyle();
  plot_style.PlotBorderSize = 1.0f;
  plot_style.MinorAlpha = 0.65f;
  plot_style.LegendPadding = ImVec2(6.0f, 5.0f);
  plot_style.LegendInnerPadding = ImVec2(6.0f, 3.0f);
  plot_style.LegendSpacing = ImVec2(7.0f, 2.0f);
  plot_style.PlotPadding = ImVec2(4.0f, 8.0f);
  plot_style.FitPadding = ImVec2(0.02f, 0.4f);

  ImPlot::MapInputDefault();
  ImPlotInputMap &input_map = ImPlot::GetInputMap();
  input_map.Pan = ImGuiMouseButton_Right;
  input_map.PanMod = ImGuiMod_None;
  input_map.Select = ImGuiMouseButton_Left;
  input_map.SelectCancel = ImGuiMouseButton_Right;
  input_map.SelectMod = ImGuiMod_None;
}

void app_push_mono_font() {
  if (g_mono_font != nullptr) {
    ImGui::PushFont(g_mono_font);
  }
}

void app_pop_mono_font() {
  if (g_mono_font != nullptr) {
    ImGui::PopFont();
  }
}

void app_push_bold_font() {
  if (g_ui_bold_font != nullptr) {
    ImGui::PushFont(g_ui_bold_font);
  }
}

void app_pop_bold_font() {
  if (g_ui_bold_font != nullptr) {
    ImGui::PopFont();
  }
}

UiMetrics compute_ui_metrics(const ImVec2 &size, float top_offset, float sidebar_width) {
  UiMetrics ui;
  ui.width = size.x;
  ui.height = size.y;
  ui.top_offset = top_offset;
  ui.sidebar_width = std::clamp(sidebar_width, SIDEBAR_MIN_WIDTH, std::min(SIDEBAR_MAX_WIDTH, size.x * 0.6f));
  ui.content_x = ui.sidebar_width;
  ui.content_y = top_offset;
  ui.content_w = std::max(1.0f, size.x - ui.content_x);
  ui.content_h = std::max(1.0f, size.y - ui.content_y - STATUS_BAR_HEIGHT);
  ui.status_bar_y = std::max(0.0f, size.y - STATUS_BAR_HEIGHT);
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
    state->tabs[i].map_panes.resize(static_cast<size_t>(std::max(0, pane_count)));
    state->tabs[i].camera_panes.resize(static_cast<size_t>(std::max(0, pane_count)));
    state->tabs[i].active_pane_index = pane_count <= 0
      ? 0
      : std::clamp(state->tabs[i].active_pane_index, 0, pane_count - 1);
  }
}

void resize_tab_pane_state(TabUiState *tab_state, size_t pane_count) {
  if (tab_state == nullptr) return;
  tab_state->map_panes.resize(pane_count);
  tab_state->camera_panes.resize(pane_count);
}

void sync_route_buffers(UiState *state, const AppSession &session) {
  copy_to_buffer(session.route_name, &state->route_buffer);
  copy_to_buffer(session.data_dir, &state->data_dir_buffer);
}

void sync_stream_buffers(UiState *state, const AppSession &session) {
  copy_to_buffer(session.stream_address, &state->stream_address_buffer);
  state->stream_remote = !is_local_stream_address(session.stream_address);
  state->stream_buffer_seconds = session.stream_buffer_seconds;
}

fs::path default_layout_save_path(const AppSession &session) {
  return session.layout_path.empty() ? layouts_dir() / "new-layout.json" : session.layout_path;
}

void sync_layout_buffers(UiState *state, const AppSession &session) {
  copy_to_buffer(session.layout_path.empty() ? std::string() : session.layout_path.string(), &state->load_layout_buffer);
  copy_to_buffer(default_layout_save_path(session).string(), &state->save_layout_buffer);
}

const WorkspaceTab *app_active_tab(const SketchLayout &layout, const UiState &state) {
  if (layout.tabs.empty()) return nullptr;
  const int index = std::clamp(state.active_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  return &layout.tabs[static_cast<size_t>(index)];
}

WorkspaceTab *app_active_tab(SketchLayout *layout, const UiState &state) {
  if (layout->tabs.empty()) return nullptr;
  const int index = std::clamp(state.active_tab_index, 0, static_cast<int>(layout->tabs.size()) - 1);
  return &layout->tabs[static_cast<size_t>(index)];
}

TabUiState *app_active_tab_state(UiState *state) {
  if (state->tabs.empty()) return nullptr;
  const int index = std::clamp(state->active_tab_index, 0, static_cast<int>(state->tabs.size()) - 1);
  return &state->tabs[static_cast<size_t>(index)];
}

std::string pane_window_name(int tab_runtime_id, int pane_index, const Pane &pane) {
  const char *title = pane.title.empty() ? UNTITLED_PANE_TITLE : pane.title.c_str();
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s###tab%d_pane%d", title, tab_runtime_id, pane_index);
  return buf;
}

std::string tab_item_label(const WorkspaceTab &tab, int tab_runtime_id) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s##workspace_tab_%d", tab.tab_name.c_str(), tab_runtime_id);
  return buf;
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
  char buf[48];
  std::snprintf(buf, sizeof(buf), "jotpluggler_dockspace_%d", tab_runtime_id);
  return ImHashStr(buf);
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
  OpenAxisLimits,
  OpenCustomSeries,
  SplitLeft,
  SplitRight,
  SplitTop,
  SplitBottom,
  ResetView,
  ResetHorizontal,
  ResetVertical,
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
  std::vector<std::string> browser_paths;
  std::string special_item_id;
  PaneCurveDragPayload curve_ref;
};


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
  if (node->is_pane) return node->pane_index == pane_index;

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
    if (node->pane_index != target_pane_index) return false;
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

  if (node->orientation == orientation) {
    for (size_t i = 0; i < node->children.size(); ++i) {
      WorkspaceNode &child = node->children[i];
      if (!child.is_pane || child.pane_index != target_pane_index) {
        continue;
      }

      WorkspaceNode new_pane;
      new_pane.is_pane = true;
      new_pane.pane_index = new_pane_index;

      const auto insert_it = node->children.begin() + static_cast<std::ptrdiff_t>(new_before ? i : i + 1);
      node->children.insert(insert_it, std::move(new_pane));
      node->sizes.assign(node->children.size(), 1.0f / static_cast<float>(node->children.size()));
      return true;
    }
  }

  for (WorkspaceNode &child : node->children) {
    if (split_pane_node(&child, target_pane_index, orientation, new_before, new_pane_index)) return true;
  }
  return false;
}

Pane make_empty_pane(const std::string &title = UNTITLED_PANE_TITLE) {
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
  if (!tab_name_exists(layout, base)) return base;
  for (int i = 2; i < 1000; ++i) {
    const std::string candidate = base + " " + std::to_string(i);
    if (!tab_name_exists(layout, candidate)) return candidate;
  }
  return base + " copy";
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
    save_layout_json(session->layout, session->autosave_path);
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

bool active_tab_has_map_pane(const SketchLayout &layout) {
  if (layout.tabs.empty()) {
    return false;
  }
  const int tab_index = std::clamp(layout.current_tab_index, 0, static_cast<int>(layout.tabs.size()) - 1);
  const WorkspaceTab &tab = layout.tabs[static_cast<size_t>(tab_index)];
  return std::any_of(tab.panes.begin(), tab.panes.end(), [](const Pane &pane) {
    return pane.kind == PaneKind::Map || pane.kind == PaneKind::Camera;
  });
}

const char *camera_view_label(CameraViewKind view) {
  switch (view) {
    case CameraViewKind::Driver: return "Driver Camera";
    case CameraViewKind::WideRoad: return "Wide Road Camera";
    case CameraViewKind::QRoad: return "qRoad Camera";
    case CameraViewKind::Road:
    default: return "Road Camera";
  }
}

std::optional<CameraViewKind> camera_view_from_special_item(std::string_view item_id) {
  if (item_id == "camera_road") return CameraViewKind::Road;
  if (item_id == "camera_driver") return CameraViewKind::Driver;
  if (item_id == "camera_wide_road") return CameraViewKind::WideRoad;
  if (item_id == "camera_qroad") return CameraViewKind::QRoad;
  return std::nullopt;
}

const char *special_item_label(std::string_view item_id) {
  if (item_id == "map") return "Map";
  if (const auto view = camera_view_from_special_item(item_id)) {
    return camera_view_label(*view);
  }
  return "Item";
}

constexpr std::array<std::pair<const char *, const char *>, 5> kBrowserSpecialItems = {{
  {"map", "Map"},
  {"camera_road", "Road Camera"},
  {"camera_driver", "Driver Camera"},
  {"camera_wide_road", "Wide Road Camera"},
  {"camera_qroad", "qRoad Camera"},
}};

void draw_browser_special_item(AppSession *session, UiState *state, const char *item_id, const char *label) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 row_size(std::max(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight());
  ImGui::PushID(item_id);
  ImGui::InvisibleButton("##special_data_row", row_size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  if (hovered) {
    const ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered);
    draw_list->AddRectFilled(rect.Min, rect.Max, bg, 0.0f);
  }
  ImGui::RenderTextEllipsis(draw_list,
                            ImVec2(rect.Min.x + style.FramePadding.x, rect.Min.y + style.FramePadding.y),
                            ImVec2(rect.Max.x - style.FramePadding.x, rect.Max.y),
                            rect.Max.x - style.FramePadding.x,
                            label,
                            nullptr,
                            nullptr);
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
    ImGui::SetDragDropPayload("JOTP_SPECIAL_ITEM", item_id, std::strlen(item_id) + 1);
    ImGui::TextUnformatted(label);
    ImGui::EndDragDropSource();
  }
  ImGui::PopID();
}

std::array<uint8_t, 3> app_next_curve_color(const Pane &pane) {
  static constexpr std::array<std::array<uint8_t, 3>, 10> PALETTE = {{
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
  return PALETTE[pane.curves.size() % PALETTE.size()];
}

#include "tools/jotpluggler/app_session_flow.cc"

#include "tools/jotpluggler/app_sidebar_flow.cc"

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
    const bool show_load_progress = session->route_loader && (load.active || load.total_segments > 0);
    const bool streaming = session->data_mode == SessionDataMode::Stream;
    SidebarCameraFeed *sidebar_camera = session->pane_camera_feeds[static_cast<size_t>(CameraViewKind::Road)].get();
    if (show_camera_feed && sidebar_camera != nullptr) {
      sidebar_camera->draw(ImGui::GetContentRegionAvail().x, load.active);
    } else if (streaming) {
      ImGui::SeparatorText("Camera");
      ImGui::TextDisabled("Camera not available during live stream.");
      ImGui::Spacing();
    }

    ImGui::SeparatorText(streaming ? "Stream" : "Route");
    if (streaming) {
      const StreamPollSnapshot stream = session->stream_poller ? session->stream_poller->snapshot() : StreamPollSnapshot{};
      const bool paused = stream.paused || session->stream_paused;
      const bool live = stream.connected && !paused;
      const ImVec4 status_color = live ? color_rgb(38, 135, 67) : (paused ? color_rgb(168, 119, 34) : color_rgb(155, 63, 63));
      ImGui::TextColored(status_color, "%s %s", live ? "●" : "○", stream.address.c_str());
      ImGui::TextDisabled("%s%s", stream.remote ? "Remote (ZMQ)" : "Local (MSGQ)", paused ? "  paused" : "");
      const double span = session->route_data.has_time_range ? (session->route_data.x_max - session->route_data.x_min) : 0.0;
      const float fill = stream.buffer_seconds <= 0.0
        ? 0.0f
        : std::clamp(static_cast<float>(span / stream.buffer_seconds), 0.0f, 1.0f);
      ImGui::ProgressBar(fill, ImVec2(-FLT_MIN, 0.0f), nullptr);
      ImGui::TextDisabled("%.0fs buffer | %zu series", session->stream_buffer_seconds, session->route_data.series.size());
      const char *button_label = paused ? "Resume" : "Pause";
      if (ImGui::Button(button_label, ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
        if (paused) {
          start_stream_session(session, state, session->stream_address, session->stream_buffer_seconds, true);
        } else {
          stop_stream_session(session, state);
          state->status_text = "Paused stream " + session->stream_address;
        }
      }
    } else if (session->route_name.empty()) {
      ImGui::TextDisabled("No route loaded");
    }
    if (!session->route_data.car_fingerprint.empty()) {
      ImGui::TextWrapped("Car: %s", session->route_data.car_fingerprint.c_str());
    }
    const std::vector<std::string> dbc_names = available_dbc_names();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##dbc_combo", dbc_combo_label(*session).c_str())) {
      const bool auto_selected = session->dbc_override.empty();
      if (ImGui::Selectable("Auto", auto_selected)) {
        apply_dbc_override_change(session, state, {});
      }
      if (auto_selected) {
        ImGui::SetItemDefaultFocus();
      }
      ImGui::Separator();
      for (const std::string &dbc_name : dbc_names) {
        const bool selected = session->dbc_override == dbc_name;
        if (ImGui::Selectable(dbc_name.c_str(), selected) && !selected) {
          apply_dbc_override_change(session, state, dbc_name);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SeparatorText("Layout");
    const std::vector<std::string> layouts = available_layout_names();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##layout_combo", layout_combo_label(*session, *state).c_str())) {
      if (ImGui::Selectable("New Layout")) {
        start_new_layout(session, state);
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
    const float layout_button_gap = ImGui::GetStyle().ItemSpacing.x;
    const float layout_row_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float layout_button_width = std::max(1.0f, (layout_row_width - 2.0f * layout_button_gap) / 3.0f);
    if (ImGui::Button("New", ImVec2(layout_button_width, 0.0f))) {
      start_new_layout(session, state);
    }
    ImGui::SameLine(0.0f, layout_button_gap);
    if (ImGui::Button("Save", ImVec2(layout_button_width, 0.0f))) {
      state->request_save_layout = true;
    }
    ImGui::SameLine(0.0f, layout_button_gap);
    ImGui::BeginDisabled(!state->layout_dirty);
    if (ImGui::Button("Reset", ImVec2(layout_button_width, 0.0f))) {
      state->request_reset_layout = true;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::SeparatorText("Data Sources");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##browser_filter", "Search...", state->browser_filter.data(), state->browser_filter.size());
    const float footer_height = ImGui::GetFrameHeightWithSpacing()
                              + ImGui::GetTextLineHeightWithSpacing()
                              + 16.0f
                              + (show_load_progress ? (ImGui::GetFrameHeightWithSpacing() + 12.0f) : 0.0f);
    const float browser_height = std::max(1.0f, ImGui::GetContentRegionAvail().y - footer_height);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
    if (ImGui::BeginChild("##timeseries_browser", ImVec2(0.0f, browser_height), true)) {
      const std::string filter = lowercase(string_from_buffer(state->browser_filter));
      std::vector<std::string> visible_paths;
      for (const BrowserNode &node : session->browser_nodes) {
        collect_visible_leaf_paths(node, filter, &visible_paths);
      }
      for (const auto &[item_id, label] : kBrowserSpecialItems) {
        draw_browser_special_item(session, state, item_id, label);
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      ImGui::Separator();
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      for (const BrowserNode &node : session->browser_nodes) {
        draw_browser_node(session, node, state, filter, visible_paths);
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    ImGui::SeparatorText("Custom Series");
    if (ImGui::Button("Create...", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f))) {
      open_custom_series_editor(state, state->selected_browser_path);
    }
    if (show_load_progress) {
      const float total = static_cast<float>(std::max<size_t>(1, load.total_segments));
      const float progress = load.total_segments == 0
        ? 0.0f
        : std::clamp(static_cast<float>(load.segments_downloaded + load.segments_parsed) / (2.0f * total), 0.0f, 1.0f);
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
      ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), nullptr);
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
}

std::string app_curve_display_name(const Curve &curve) {
  if (!curve.label.empty()) return curve.label;
  if (!curve.name.empty()) return curve.name;
  return "curve";
}

Curve make_curve_for_path(const Pane &pane, const std::string &path) {
  Curve curve;
  curve.name = path;
  curve.label = path;
  curve.color = app_next_curve_color(pane);
  return curve;
}

bool add_curve_to_pane(WorkspaceTab *tab, int pane_index, Curve curve) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  if (pane.kind != PaneKind::Plot) {
    pane.kind = PaneKind::Plot;
    if (pane.title == "Map"
        || pane.title == camera_view_label(CameraViewKind::Road)
        || pane.title == camera_view_label(CameraViewKind::Driver)
        || pane.title == camera_view_label(CameraViewKind::WideRoad)
        || pane.title == camera_view_label(CameraViewKind::QRoad)) {
      pane.title = UNTITLED_PANE_TITLE;
    }
  }
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
  if (app_find_route_series(*session, path) == nullptr) {
    state->status_text = "Path not found in route";
    return false;
  }
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  if (tab == nullptr || pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    state->status_text = "No active pane";
    return false;
  }
  const SketchLayout before_layout = session->layout;
  const bool inserted = add_curve_to_pane(tab, pane_index, make_curve_for_path(tab->panes[static_cast<size_t>(pane_index)], path));
  bool autosave_ok = true;
  if (inserted) {
    state->undo.push(before_layout);
    autosave_ok = mark_layout_dirty(session, state);
  }
  if (autosave_ok) {
    state->status_text = inserted ? "Added " + path : "Curve already present";
  }
  return true;
}

int add_path_curves_to_pane(AppSession *session, UiState *state, int pane_index, const std::vector<std::string> &paths) {
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  if (tab == nullptr || pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    state->status_text = "No active pane";
    return 0;
  }

  int inserted_count = 0;
  int duplicate_count = 0;
  const SketchLayout before_layout = session->layout;
  for (const std::string &path : paths) {
    if (app_find_route_series(*session, path) == nullptr) continue;
    if (add_curve_to_pane(tab, pane_index, make_curve_for_path(tab->panes[static_cast<size_t>(pane_index)], path))) {
      ++inserted_count;
    } else {
      ++duplicate_count;
    }
  }

  if (inserted_count > 0) {
    state->undo.push(before_layout);
    if (mark_layout_dirty(session, state)) {
      state->status_text = inserted_count == 1
        ? "Added " + paths.front()
        : "Added " + std::to_string(inserted_count) + " curves";
    }
    return inserted_count;
  }

  if (duplicate_count > 0) {
    state->status_text = duplicate_count == 1 ? "Curve already present" : "Curves already present";
  } else {
    state->status_text = "No matching series found";
  }
  return 0;
}

bool app_add_curve_to_active_pane(AppSession *session, UiState *state, const std::string &path) {
  const TabUiState *tab_state = app_active_tab_state(state);
  if (tab_state == nullptr) {
    state->status_text = "No active pane";
    return false;
  }
  return add_path_curve_to_pane(session, state, tab_state->active_pane_index, path);
}

bool pane_is_empty_for_special_item(const Pane &pane) {
  return pane.kind == PaneKind::Plot && pane.curves.empty();
}

bool pane_can_accept_special_item(const Pane &pane) {
  return pane_is_empty_for_special_item(pane) || pane.kind == PaneKind::Map || pane.kind == PaneKind::Camera;
}

bool convert_pane_to_map(WorkspaceTab *tab, TabUiState *tab_state, int pane_index) {
  if (tab == nullptr || tab_state == nullptr) return false;
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) return false;
  Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  const PaneKind previous_kind = pane.kind;
  if (pane.kind == PaneKind::Map) {
    tab_state->active_pane_index = pane_index;
    return false;
  }
  pane.kind = PaneKind::Map;
  if (pane.title == UNTITLED_PANE_TITLE || previous_kind != PaneKind::Plot) {
    pane.title = "Map";
  }
  tab_state->active_pane_index = pane_index;
  return true;
}

bool convert_pane_to_camera(WorkspaceTab *tab, TabUiState *tab_state, int pane_index, CameraViewKind view) {
  if (tab == nullptr || tab_state == nullptr) return false;
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) return false;
  Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  if (pane.kind == PaneKind::Camera && pane.camera_view == view) {
    tab_state->active_pane_index = pane_index;
    return false;
  }
  pane.kind = PaneKind::Camera;
  pane.camera_view = view;
  pane.title = camera_view_label(view);
  resize_tab_pane_state(tab_state, tab->panes.size());
  tab_state->camera_panes[static_cast<size_t>(pane_index)].fit_to_pane = true;
  tab_state->active_pane_index = pane_index;
  return true;
}

bool apply_special_item_to_active_pane(AppSession *session, UiState *state, std::string_view item_id) {
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  TabUiState *tab_state = app_active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) return false;
  if (tab_state->active_pane_index < 0 || tab_state->active_pane_index >= static_cast<int>(tab->panes.size())) {
    state->status_text = "No active pane";
    return false;
  }
  if (!pane_can_accept_special_item(tab->panes[static_cast<size_t>(tab_state->active_pane_index)])) {
    state->status_text = std::string(special_item_label(item_id)) + " can only replace another special pane or use an empty pane";
    return false;
  }

  const SketchLayout before_layout = session->layout;
  bool changed = false;
  if (item_id == "map") {
    changed = convert_pane_to_map(tab, tab_state, tab_state->active_pane_index);
  } else if (const auto view = camera_view_from_special_item(item_id)) {
    changed = convert_pane_to_camera(tab, tab_state, tab_state->active_pane_index, *view);
  } else {
    return false;
  }
  if (!changed) {
    state->status_text = std::string(special_item_label(item_id)) + " already shown in pane";
    return false;
  }
  resize_tab_pane_state(tab_state, tab->panes.size());
  state->undo.push(before_layout);
  if (mark_layout_dirty(session, state)) {
    state->status_text = std::string(special_item_label(item_id)) + " added to active pane";
  }
  return true;
}

bool split_pane(WorkspaceTab *tab, int pane_index, PaneDropZone zone, std::optional<Curve> curve = std::nullopt) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  if (zone == PaneDropZone::Center) return false;

  const int new_pane_index = static_cast<int>(tab->panes.size());
  Pane new_pane = make_empty_pane();
  if (curve.has_value()) {
    new_pane.curves.push_back(*curve);
  }
  tab->panes.push_back(std::move(new_pane));

  const bool vertical = zone == PaneDropZone::Top || zone == PaneDropZone::Bottom;
  const bool new_before = zone == PaneDropZone::Left || zone == PaneDropZone::Top;
  return split_pane_node(&tab->root, pane_index,
    vertical ? SplitOrientation::Vertical : SplitOrientation::Horizontal,
    new_before, new_pane_index);
}

bool close_pane(WorkspaceTab *tab, int pane_index) {
  if (pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return false;
  }
  if (tab->panes.size() <= 1) {
    tab->panes[static_cast<size_t>(pane_index)] = make_empty_pane();
    return true;
  }
  if (remove_pane_node(&tab->root, pane_index)) return false;
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
  pane.title = UNTITLED_PANE_TITLE;
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
    const SketchLayout before_layout = session->layout;
    rename_runtime_tab(&session->layout, state);
    state->undo.push(before_layout);
    mark_layout_dirty(session, state);
  }
}

bool curve_has_samples(const AppSession &session, const Curve &curve) {
  if (curve_has_local_samples(curve)) return true;
  if (curve.name.empty() || curve.name.front() != '/') {
    return false;
  }
  const RouteSeries *series = app_find_route_series(session, curve.name);
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
  const EnumInfo *enum_info = nullptr;
  SeriesFormat display_info;
  std::optional<double> legend_value;
  std::vector<double> xs;
  std::vector<double> ys;
};

struct StateBlock {
  double t0 = 0.0;
  double t1 = 0.0;
  int value = 0;
  std::string label;
};

struct PaneEnumContext {
  std::vector<const EnumInfo *> enums;
};

struct PaneValueFormatContext {
  SeriesFormat format;
  bool valid = false;
};

bool curves_are_bool_like(const std::vector<PreparedCurve> &prepared_curves) {
  if (prepared_curves.empty()) {
    return false;
  }
  for (const PreparedCurve &curve : prepared_curves) {
    if (!curve.display_info.integer_like || curve.ys.empty()) {
      return false;
    }
    bool found_finite = false;
    for (double value : curve.ys) {
      if (!std::isfinite(value)) continue;
      found_finite = true;
      if (std::abs(value) > 0.01 && std::abs(value - 1.0) > 0.01) {
        return false;
      }
    }
    if (!found_finite) {
      return false;
    }
  }
  return true;
}

bool curve_is_state_like(const PreparedCurve &curve) {
  if (!curve.display_info.integer_like || curve.xs.size() < 2 || curve.xs.size() != curve.ys.size()) {
    return false;
  }
  if (curve.enum_info != nullptr) {
    return true;
  }
  std::unordered_set<int> distinct_values;
  for (double value : curve.ys) {
    if (!std::isfinite(value)) {
      continue;
    }
    distinct_values.insert(static_cast<int>(std::llround(value)));
    if (distinct_values.size() > 12) {
      return false;
    }
  }
  return !distinct_values.empty();
}

bool curves_use_state_blocks(const std::vector<PreparedCurve> &prepared_curves) {
  if (prepared_curves.empty()) {
    return false;
  }
  for (const PreparedCurve &curve : prepared_curves) {
    if (!curve_is_state_like(curve)) {
      return false;
    }
  }
  return true;
}

ImU32 state_block_color(int value, float alpha = 1.0f) {
  static constexpr std::array<std::array<uint8_t, 3>, 8> kPalette = {{
    {{111, 143, 175}},
    {{0, 163, 108}},
    {{255, 195, 0}},
    {{199, 0, 57}},
    {{123, 97, 255}},
    {{0, 150, 136}},
    {{214, 48, 49}},
    {{52, 73, 94}},
  }};
  const size_t index = static_cast<size_t>(std::abs(value)) % kPalette.size();
  return ImGui::GetColorU32(color_rgb(kPalette[index], alpha));
}

std::string state_block_label(const PreparedCurve &curve, int value) {
  if (curve.enum_info != nullptr && value >= 0 && static_cast<size_t>(value) < curve.enum_info->names.size()) {
    const std::string &name = curve.enum_info->names[static_cast<size_t>(value)];
    if (!name.empty()) {
      return name;
    }
  }
  return std::to_string(value);
}

std::vector<StateBlock> build_state_blocks(const PreparedCurve &curve) {
  std::vector<StateBlock> blocks;
  if (curve.xs.size() < 2 || curve.xs.size() != curve.ys.size()) {
    return blocks;
  }

  int current_value = static_cast<int>(std::llround(curve.ys.front()));
  double start_time = curve.xs.front();
  for (size_t i = 1; i < curve.xs.size(); ++i) {
    const int value = static_cast<int>(std::llround(curve.ys[i]));
    if (value == current_value) {
      continue;
    }
    const double end_time = curve.xs[i];
    if (end_time > start_time) {
      blocks.push_back(StateBlock{
        .t0 = start_time,
        .t1 = end_time,
        .value = current_value,
        .label = state_block_label(curve, current_value),
      });
    }
    current_value = value;
    start_time = end_time;
  }

  const double final_time = curve.xs.back();
  if (final_time >= start_time) {
    blocks.push_back(StateBlock{
      .t0 = start_time,
      .t1 = final_time,
      .value = current_value,
      .label = state_block_label(curve, current_value),
    });
  }
  return blocks;
}

void app_decimate_samples_impl(const std::vector<double> &xs_in,
                           const std::vector<double> &ys_in,
                           int max_points,
                           std::vector<double> *xs_out,
                           std::vector<double> *ys_out) {

  const size_t bucket_count = std::max<size_t>(1, static_cast<size_t>(max_points / 4));
  const size_t bucket_size = std::max<size_t>(
    1,
    static_cast<size_t>(std::ceil(static_cast<double>(xs_in.size()) / static_cast<double>(bucket_count))));
  xs_out->reserve(bucket_count * 4 + 2);
  ys_out->reserve(bucket_count * 4 + 2);

  size_t last_index = std::numeric_limits<size_t>::max();
  auto append_index = [&](size_t index) {
    if (index >= xs_in.size() || index == last_index) {
      return;
    }
    xs_out->push_back(xs_in[index]);
    ys_out->push_back(ys_in[index]);
    last_index = index;
  };

  for (size_t start = 0; start < xs_in.size(); start += bucket_size) {
    const size_t end = std::min(xs_in.size(), start + bucket_size);
    size_t min_index = start;
    size_t max_index = start;
    for (size_t index = start + 1; index < end; ++index) {
      if (ys_in[index] < ys_in[min_index]) {
        min_index = index;
      }
      if (ys_in[index] > ys_in[max_index]) {
        max_index = index;
      }
    }

    std::array<size_t, 4> indices = {start, min_index, max_index, end - 1};
    std::sort(indices.begin(), indices.end());
    for (size_t index : indices) {
      append_index(index);
    }
  }
}

void app_decimate_samples(const std::vector<double> &xs_in,
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
  app_decimate_samples_impl(xs_in, ys_in, max_points, xs_out, ys_out);
}

void app_decimate_samples(std::vector<double> &&xs_in,
                      std::vector<double> &&ys_in,
                      int max_points,
                      std::vector<double> *xs_out,
                      std::vector<double> *ys_out) {
  xs_out->clear();
  ys_out->clear();
  if (xs_in.empty() || xs_in.size() != ys_in.size()) {
    return;
  }
  if (max_points <= 0 || static_cast<int>(xs_in.size()) <= max_points) {
    *xs_out = std::move(xs_in);
    *ys_out = std::move(ys_in);
    return;
  }
  app_decimate_samples_impl(xs_in, ys_in, max_points, xs_out, ys_out);
}

std::optional<double> app_sample_xy_value_at_time(const std::vector<double> &xs,
                                              const std::vector<double> &ys,
                                              bool stairs,
                                              double tm) {
  if (xs.size() < 2 || xs.size() != ys.size()) {
    return std::nullopt;
  }
  if (tm <= xs.front()) return ys.front();
  if (tm >= xs.back()) return ys.back();

  const auto upper = std::lower_bound(xs.begin(), xs.end(), tm);
  if (upper == xs.begin()) return ys.front();
  if (upper == xs.end()) return ys.back();

  const size_t upper_index = static_cast<size_t>(std::distance(xs.begin(), upper));
  const size_t lower_index = upper_index - 1;
  const double x0 = xs[lower_index];
  const double x1 = xs[upper_index];
  const double y0 = ys[lower_index];
  const double y1 = ys[upper_index];
  if (std::abs(tm - x1) < 1.0e-9) return y1;
  if (stairs || x1 <= x0) return y0;
  const double alpha = (tm - x0) / (x1 - x0);
  return y0 + (y1 - y0) * alpha;
}

int format_enum_axis_tick(double value, char *buf, int size, void *user_data) {
  const auto *ctx = static_cast<const PaneEnumContext *>(user_data);
  const int idx = static_cast<int>(std::llround(value));
  if (ctx != nullptr && idx >= 0 && std::abs(value - static_cast<double>(idx)) < 0.01) {
    std::vector<std::string_view> names;
    names.reserve(ctx->enums.size());
    for (const EnumInfo *info : ctx->enums) {
      if (info == nullptr || static_cast<size_t>(idx) >= info->names.size()) {
        continue;
      }
      const std::string &name = info->names[static_cast<size_t>(idx)];
      if (name.empty()) continue;
      if (std::find(names.begin(), names.end(), std::string_view(name)) == names.end()) {
        names.emplace_back(name);
      }
    }
    if (!names.empty()) {
      std::string joined;
      for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
          joined += ", ";
        }
        joined += names[i];
      }
      return std::snprintf(buf, size, "%d (%s)", idx, joined.c_str());
    }
  }
  return std::snprintf(buf, size, "%.6g", value);
}

int format_numeric_axis_tick(double value, char *buf, int size, void *user_data) {
  const auto *ctx = static_cast<const PaneValueFormatContext *>(user_data);
  if (ctx == nullptr || !ctx->valid) {
    return std::snprintf(buf, size, "%.6g", value);
  }
  if (ctx->format.integer_like) {
    const double nearest_int = std::round(value);
    if (std::abs(value - nearest_int) > 1.0e-6) {
      int decimals = 1;
      while (decimals < 4) {
        const double scale = std::pow(10.0, decimals);
        const double rounded = std::round(value * scale) / scale;
        if (std::abs(value - rounded) <= 1.0e-6) {
          break;
        }
        ++decimals;
      }
      return std::snprintf(buf, size, "%.*f", decimals, value);
    }
  }
  return std::snprintf(buf, size, ctx->format.fmt, value);
}

void merge_pane_value_format(PaneValueFormatContext *ctx, const SeriesFormat &format) {
  if (!ctx->valid) {
    ctx->format = format;
    ctx->valid = true;
    return;
  }
  ctx->format.has_negative = ctx->format.has_negative || format.has_negative;
  ctx->format.digits_before = std::max(ctx->format.digits_before, format.digits_before);
  ctx->format.decimals = std::max(ctx->format.decimals, format.decimals);
  ctx->format.integer_like = ctx->format.decimals == 0;
  const int sign_width = ctx->format.has_negative ? 1 : 0;
  const int dot_width = ctx->format.decimals > 0 ? 1 : 0;
  ctx->format.total_width = sign_width + ctx->format.digits_before + dot_width + ctx->format.decimals;
  std::snprintf(ctx->format.fmt, sizeof(ctx->format.fmt), "%%%d.%df",
                ctx->format.total_width, ctx->format.decimals);
}

std::string curve_legend_label(const PreparedCurve &curve, bool has_cursor_time, size_t label_width) {
  if (!has_cursor_time) return curve.label;
  if (!curve.legend_value.has_value()) return curve.label;
  const std::string value_text = format_display_value(*curve.legend_value, curve.display_info, curve.enum_info);
  if (value_text.empty()) return curve.label;
  const size_t padded_width = std::max(label_width, curve.label.size());
  return curve.label + std::string(padded_width - curve.label.size() + 2, ' ') + value_text;
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
    const RouteSeries *series = app_find_route_series(session, curve.name);
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
    if (end_index <= begin_index + 1) return false;
    xs.assign(series->times.begin() + begin_index, series->times.begin() + end_index);
    ys.assign(series->values.begin() + begin_index, series->values.begin() + end_index);
  }

  std::vector<double> transformed_xs;
  std::vector<double> transformed_ys;
  if (curve.derivative) {
    if (xs.size() < 2) return false;
    transformed_xs.reserve(xs.size() - 1);
    transformed_ys.reserve(ys.size() - 1);
    for (size_t i = 1; i < xs.size(); ++i) {
      const double dt = curve.derivative_dt > 0.0 ? curve.derivative_dt : (xs[i] - xs[i - 1]);
      if (dt <= 0.0) continue;
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

  prepared->label = app_curve_display_name(curve);
  prepared->color = curve.color;
  prepared->line_weight = curve.derivative ? 1.8f : 2.25f;
  if (!curve.derivative
      && curve.value_scale == 1.0
      && curve.value_offset == 0.0
      && !curve_has_local_samples(curve)
      && !curve.name.empty()
      && curve.name.front() == '/') {
    auto it = session.route_data.enum_info.find(curve.name);
    if (it != session.route_data.enum_info.end()) {
      prepared->enum_info = &it->second;
    }
  }
  if (prepared->enum_info != nullptr) {
    prepared->display_info = compute_series_format(transformed_ys, true);
  } else if (!curve_has_local_samples(curve)
             && !curve.derivative
             && curve.value_scale == 1.0
             && curve.value_offset == 0.0
             && !curve.name.empty()
             && curve.name.front() == '/') {
    auto display_it = session.route_data.series_formats.find(curve.name);
    if (display_it != session.route_data.series_formats.end()) {
      prepared->display_info = display_it->second;
    } else {
      prepared->display_info = compute_series_format(transformed_ys, false);
    }
  } else {
    prepared->display_info = compute_series_format(transformed_ys, false);
  }
  const bool stairs = !curve.derivative && prepared->display_info.integer_like;
  if (state.has_tracker_time) {
    prepared->legend_value = app_sample_xy_value_at_time(transformed_xs, transformed_ys, stairs, state.tracker_time);
  }
  if (stairs) {
    prepared->xs = std::move(transformed_xs);
    prepared->ys = std::move(transformed_ys);
  } else {
    app_decimate_samples(std::move(transformed_xs), std::move(transformed_ys), max_points, &prepared->xs, &prepared->ys);
  }
  prepared->stairs = stairs;
  return prepared->xs.size() > 1 && prepared->xs.size() == prepared->ys.size();
}

bool draw_pane_close_button_overlay() {
  const ImVec2 window_pos = ImGui::GetWindowPos();
  const ImVec2 content_min = ImGui::GetWindowContentRegionMin();
  const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
  const ImRect rect(ImVec2(window_pos.x + content_max.x - 42.0f, window_pos.y + content_min.y + 4.0f),
                    ImVec2(window_pos.x + content_max.x - 4.0f, window_pos.y + content_min.y + 42.0f));
  const bool hovered = ImGui::IsMouseHoveringRect(rect.Min, rect.Max, false);
  const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const float pad = 11.0f;
  const ImU32 color = hovered || held
    ? ImGui::GetColorU32(color_rgb(72, 79, 88))
    : ImGui::GetColorU32(color_rgb(138, 146, 156));
  draw_list->AddLine(ImVec2(rect.Min.x + pad, rect.Min.y + pad),
                     ImVec2(rect.Max.x - pad, rect.Max.y - pad),
                     color,
                     2.4f);
  draw_list->AddLine(ImVec2(rect.Min.x + pad, rect.Max.y - pad),
                     ImVec2(rect.Max.x - pad, rect.Min.y + pad),
                     color,
                     2.4f);
  return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

void draw_pane_frame_overlay() {
  const ImVec2 window_pos = ImGui::GetWindowPos();
  const ImVec2 content_min = ImGui::GetWindowContentRegionMin();
  const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
  const ImRect frame_rect(ImVec2(window_pos.x + content_min.x, window_pos.y + content_min.y),
                          ImVec2(window_pos.x + content_max.x, window_pos.y + content_max.y));
  ImGui::GetWindowDrawList()->AddRect(frame_rect.Min,
                                      frame_rect.Max,
                                      ImGui::GetColorU32(color_rgb(186, 190, 196)),
                                      0.0f,
                                      0,
                                      1.0f);
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
  if (curves_are_bool_like(prepared_curves)) {
    min_value = std::min(min_value, 0.0);
    max_value = std::max(max_value, 1.0);
  }
  ensure_non_degenerate_range(&min_value, &max_value, PLOT_Y_PAD_FRACTION, 0.1);
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

void draw_state_blocks_pane(const std::vector<PreparedCurve> &prepared_curves, UiState *state) {
  if (prepared_curves.empty() || !state->has_shared_range || state->x_view_max <= state->x_view_min) {
    return;
  }

  ImDrawList *draw_list = ImPlot::GetPlotDrawList();
  const ImVec2 plot_min = ImPlot::GetPlotPos();
  const ImVec2 plot_size = ImPlot::GetPlotSize();
  const int curve_count = static_cast<int>(prepared_curves.size());
  if (plot_size.x <= 2.0f || plot_size.y <= 2.0f || curve_count <= 0) {
    return;
  }

  float label_width = 0.0f;
  if (curve_count > 1) {
    for (const PreparedCurve &curve : prepared_curves) {
      label_width = std::max(label_width, ImGui::CalcTextSize(curve.label.c_str()).x);
    }
    label_width = std::clamp(label_width + 14.0f, 72.0f, std::min(160.0f, plot_size.x * 0.35f));
  }

  const float row_height = plot_size.y / static_cast<float>(curve_count);
  const float blocks_min_x = plot_min.x + label_width;
  const float blocks_max_x = plot_min.x + plot_size.x;
  const float blocks_width = std::max(1.0f, blocks_max_x - blocks_min_x);
  const double x_span = std::max(1.0e-9, state->x_view_max - state->x_view_min);

  struct HoveredBlock {
    int curve_index = -1;
    StateBlock block;
  };
  std::optional<HoveredBlock> hovered;

  const ImVec2 mouse_pos = ImGui::GetMousePos();
  const bool plot_hovered = ImPlot::IsPlotHovered();

  for (int curve_index = 0; curve_index < curve_count; ++curve_index) {
    const PreparedCurve &curve = prepared_curves[static_cast<size_t>(curve_index)];
    const float y0 = plot_min.y + row_height * static_cast<float>(curve_index);
    const float y1 = y0 + row_height;
    const std::vector<StateBlock> blocks = build_state_blocks(curve);

    if (curve_index > 0) {
      draw_list->AddLine(ImVec2(plot_min.x, y0), ImVec2(plot_min.x + plot_size.x, y0),
                         IM_COL32(210, 214, 220, 255), 1.0f);
    }
    if (curve_count > 1) {
      draw_list->AddLine(ImVec2(blocks_min_x, y0), ImVec2(blocks_min_x, y1),
                         IM_COL32(210, 214, 220, 255), 1.0f);
      const float label_left = plot_min.x + 6.0f;
      const float label_right = std::max(label_left + 12.0f, blocks_min_x - 6.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(120, 128, 138));
      ImGui::RenderTextEllipsis(draw_list,
                                ImVec2(label_left, y0 + 4.0f),
                                ImVec2(label_right, y1 - 4.0f),
                                label_right,
                                curve.label.c_str(),
                                nullptr,
                                nullptr);
      ImGui::PopStyleColor();
    }

    for (const StateBlock &block : blocks) {
      const double visible_t0 = std::max(block.t0, state->x_view_min);
      const double visible_t1 = std::min(block.t1, state->x_view_max);
      if (visible_t1 <= visible_t0) {
        continue;
      }
      const float x0 = blocks_min_x + static_cast<float>((visible_t0 - state->x_view_min) / x_span) * blocks_width;
      const float x1 = blocks_min_x + static_cast<float>((visible_t1 - state->x_view_min) / x_span) * blocks_width;
      const ImU32 fill_color = state_block_color(block.value, 0.15f);
      const ImU32 line_color = state_block_color(block.value, 0.90f);
      draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(std::max(x1, x0 + 1.0f), y1), fill_color);
      draw_list->AddLine(ImVec2(x0, y0), ImVec2(x0, y1), line_color, 2.0f);

      const float block_width = x1 - x0;
      if (block_width > 14.0f) {
        const float text_left = x0 + 6.0f;
        const float text_right = x1 - 6.0f;
        if (text_right > text_left) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(state_block_color(block.value, 0.80f)));
          ImGui::RenderTextEllipsis(draw_list,
                                    ImVec2(text_left, y0 + 4.0f),
                                    ImVec2(text_right, y1 - 4.0f),
                                    text_right,
                                    block.label.c_str(),
                                    nullptr,
                                    nullptr);
          ImGui::PopStyleColor();
        }
      }

      if (plot_hovered && mouse_pos.x >= blocks_min_x && mouse_pos.x <= blocks_max_x && mouse_pos.y >= y0 && mouse_pos.y <= y1) {
        const double hover_time = state->x_view_min + static_cast<double>((mouse_pos.x - blocks_min_x) / blocks_width) * x_span;
        if (hover_time >= block.t0 && hover_time <= block.t1) {
          hovered = HoveredBlock{
            .curve_index = curve_index,
            .block = block,
          };
        }
      }
    }
  }

  if (hovered.has_value()) {
    const HoveredBlock &info = *hovered;
    ImGui::BeginTooltip();
    if (curve_count > 1) {
      ImGui::Text("%s: %s (%d)", prepared_curves[static_cast<size_t>(info.curve_index)].label.c_str(),
                  info.block.label.c_str(), info.block.value);
    } else {
      ImGui::Text("%s (%d)", info.block.label.c_str(), info.block.value);
    }
    ImGui::Separator();
    ImGui::Text("%.3fs -> %.3fs", info.block.t0, info.block.t1);
    ImGui::Text("duration: %.3fs", info.block.t1 - info.block.t0);
    ImGui::EndTooltip();
  }
}

void persist_shared_range_to_tab(WorkspaceTab *tab, const UiState &state) {
  if (tab == nullptr || !state.has_shared_range) {
    return;
  }
  const double x_min = state.x_view_min;
  const double x_max = state.x_view_max > state.x_view_min ? state.x_view_max : state.x_view_min + 1.0;
  for (Pane &pane : tab->panes) {
    pane.range.valid = true;
    pane.range.left = x_min;
    pane.range.right = x_max;
  }
}

void clear_pane_vertical_limits(Pane *pane) {
  if (pane == nullptr) {
    return;
  }
  pane->range.has_y_limit_min = false;
  pane->range.has_y_limit_max = false;
}

PlotBounds current_plot_bounds_for_pane(const AppSession &session, const Pane &pane, const UiState &state) {
  std::vector<PreparedCurve> prepared_curves;
  prepared_curves.reserve(pane.curves.size());
  constexpr int kAxisEditorMaxPoints = 2048;
  for (size_t curve_index = 0; curve_index < pane.curves.size(); ++curve_index) {
    const Curve &curve = pane.curves[curve_index];
    if (!curve.visible || !curve_has_samples(session, curve)) continue;
    PreparedCurve prepared;
    if (build_curve_series(session, curve, state, kAxisEditorMaxPoints, &prepared)) {
      prepared.pane_curve_index = static_cast<int>(curve_index);
      prepared_curves.push_back(std::move(prepared));
    }
  }
  return compute_plot_bounds(pane, prepared_curves, state);
}

void open_axis_limits_editor(const AppSession &session, UiState *state, int pane_index) {
  ensure_shared_range(state, session);
  clamp_shared_range(state, session);
  const WorkspaceTab *tab = app_active_tab(session.layout, *state);
  if (tab == nullptr || pane_index < 0 || pane_index >= static_cast<int>(tab->panes.size())) {
    return;
  }

  const Pane &pane = tab->panes[static_cast<size_t>(pane_index)];
  const PlotBounds bounds = current_plot_bounds_for_pane(session, pane, *state);
  AxisLimitsEditorState &editor = state->axis_limits;
  editor.open = true;
  editor.pane_index = pane_index;
  editor.x_min = state->x_view_min;
  editor.x_max = state->x_view_max;
  editor.y_min_enabled = pane.range.has_y_limit_min;
  editor.y_max_enabled = pane.range.has_y_limit_max;
  editor.y_min = pane.range.has_y_limit_min ? pane.range.y_limit_min : bounds.y_min;
  editor.y_max = pane.range.has_y_limit_max ? pane.range.y_limit_max : bounds.y_max;
}

bool apply_axis_limits_editor(AppSession *session, UiState *state) {
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  if (tab == nullptr) return false;

  AxisLimitsEditorState &editor = state->axis_limits;
  if (editor.pane_index < 0 || editor.pane_index >= static_cast<int>(tab->panes.size())) {
    state->error_text = "The selected pane is no longer available.";
    state->open_error_popup = true;
    return false;
  }
  if (!std::isfinite(editor.x_min) || !std::isfinite(editor.x_max)) {
    state->error_text = "Axis limits must be finite numbers.";
    state->open_error_popup = true;
    return false;
  }
  if (editor.x_max <= editor.x_min) {
    state->error_text = "X max must be greater than X min.";
    state->open_error_popup = true;
    return false;
  }
  if (editor.y_min_enabled && !std::isfinite(editor.y_min)) {
    state->error_text = "Y min must be a finite number.";
    state->open_error_popup = true;
    return false;
  }
  if (editor.y_max_enabled && !std::isfinite(editor.y_max)) {
    state->error_text = "Y max must be a finite number.";
    state->open_error_popup = true;
    return false;
  }
  if (editor.y_min_enabled && editor.y_max_enabled && editor.y_max <= editor.y_min) {
    state->error_text = "Y max must be greater than Y min.";
    state->open_error_popup = true;
    return false;
  }

  const SketchLayout before_layout = session->layout;
  state->has_shared_range = true;
  state->x_view_min = editor.x_min;
  state->x_view_max = editor.x_max;
  if (session->data_mode == SessionDataMode::Stream) {
    state->follow_latest = infer_stream_follow_state(*state, *session);
  } else {
    state->follow_latest = false;
  }
  state->suppress_range_side_effects = true;
  clamp_shared_range(state, *session);
  persist_shared_range_to_tab(tab, *state);

  Pane &pane = tab->panes[static_cast<size_t>(editor.pane_index)];
  pane.range.has_y_limit_min = editor.y_min_enabled;
  pane.range.has_y_limit_max = editor.y_max_enabled;
  if (editor.y_min_enabled) {
    pane.range.y_limit_min = editor.y_min;
  }
  if (editor.y_max_enabled) {
    pane.range.y_limit_max = editor.y_max;
  }

  const PlotBounds bounds = current_plot_bounds_for_pane(*session, pane, *state);
  pane.range.valid = true;
  pane.range.left = state->x_view_min;
  pane.range.right = state->x_view_max;
  pane.range.bottom = bounds.y_min;
  pane.range.top = bounds.y_max;

  state->undo.push(before_layout);
  const bool ok = mark_layout_dirty(session, state);
  if (ok) {
    state->status_text = "Axis limits updated";
  }
  return ok;
}

void draw_plot(const AppSession &session, Pane *pane, UiState *state) {
  std::vector<PreparedCurve> prepared_curves;
  prepared_curves.reserve(pane->curves.size());
  const int max_points = std::max(256, static_cast<int>(ImGui::GetContentRegionAvail().x) * 2);
  for (size_t curve_index = 0; curve_index < pane->curves.size(); ++curve_index) {
    const Curve &curve = pane->curves[curve_index];
    if (!curve.visible || !curve_has_samples(session, curve)) continue;
    PreparedCurve prepared;
    if (build_curve_series(session, curve, *state, max_points, &prepared)) {
      prepared.pane_curve_index = static_cast<int>(curve_index);
      prepared_curves.push_back(std::move(prepared));
    }
  }

  const PlotBounds bounds = compute_plot_bounds(*pane, prepared_curves, *state);
  PaneEnumContext enum_context;
  PaneValueFormatContext pane_value_format;
  const bool state_block_mode = curves_use_state_blocks(prepared_curves);
  bool all_enum_curves = !prepared_curves.empty();
  size_t max_legend_label_width = 0;
  for (const PreparedCurve &curve : prepared_curves) {
    max_legend_label_width = std::max(max_legend_label_width, curve.label.size());
    if (curve.enum_info != nullptr) {
      enum_context.enums.push_back(curve.enum_info);
    } else {
      all_enum_curves = false;
      merge_pane_value_format(&pane_value_format, curve.display_info);
    }
  }
  if (prepared_curves.empty()) {
    all_enum_curves = false;
  }
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
  if (state_block_mode) {
    plot_flags |= ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText;
  }
  if (supported_count == 0) {
    plot_flags |= ImPlotFlags_NoLegend;
  }

  const ImPlotAxisFlags x_axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  ImPlotAxisFlags y_axis_flags = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;
  if (state_block_mode) {
    y_axis_flags |= ImPlotAxisFlags_NoDecorations;
  }
  const bool explicit_y = pane->range.has_y_limit_min || pane->range.has_y_limit_max;
  if (!state_block_mode && !explicit_y && supported_count > 0) {
    y_axis_flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
  }

  const double previous_x_min = state->x_view_min;
  const double previous_x_max = state->x_view_max;
  app_push_mono_font();
  if (ImPlot::BeginPlot("##plot", plot_size, plot_flags)) {
    ImPlot::SetupAxes(nullptr, nullptr, x_axis_flags, y_axis_flags);
    ImPlot::SetupAxisFormat(ImAxis_X1, "%.1f");
    if (state_block_mode) {
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Always);
    } else if (all_enum_curves && !enum_context.enums.empty()) {
      ImPlot::SetupAxisFormat(ImAxis_Y1, format_enum_axis_tick, &enum_context);
    } else if (pane_value_format.valid) {
      ImPlot::SetupAxisFormat(ImAxis_Y1, format_numeric_axis_tick, &pane_value_format);
    } else {
      ImPlot::SetupAxisFormat(ImAxis_Y1, "%.6g");
    }
    ImPlot::SetupAxisLinks(ImAxis_X1, &state->x_view_min, &state->x_view_max);
    if (state->route_x_max > state->route_x_min) {
      const double x_constraint_min = session.data_mode == SessionDataMode::Stream
        ? state->route_x_min - std::max(MIN_HORIZONTAL_ZOOM_SECONDS, session.stream_buffer_seconds)
        : state->route_x_min;
      ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, x_constraint_min, state->route_x_max);
    }
    if (!state_block_mode) {
      ImPlot::SetupMouseText(ImPlotLocation_SouthEast, ImPlotMouseTextFlags_NoAuxAxes);
    }
    if (!state_block_mode && (explicit_y || supported_count == 0)) {
      ImPlot::SetupAxisLimits(ImAxis_Y1, bounds.y_min, bounds.y_max, ImPlotCond_Always);
    }
    if (!state_block_mode && supported_count > 0) {
      ImPlot::SetupLegend(ImPlotLocation_NorthEast);
    }

    if (state_block_mode) {
      draw_state_blocks_pane(prepared_curves, state);
    } else {
      for (size_t i = 0; i < prepared_curves.size(); ++i) {
        const PreparedCurve &curve = prepared_curves[i];
        std::string series_id = curve_legend_label(curve, has_cursor_time, max_legend_label_width) + "##curve" + std::to_string(i);
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
    }
    if (has_cursor_time) {
      const double clamped_cursor_time = std::clamp(cursor_time, state->route_x_min, state->route_x_max);
      ImPlotSpec cursor_spec;
      cursor_spec.LineColor = color_rgb(108, 118, 128, 0.7f);
      cursor_spec.LineWeight = 1.0f;
      cursor_spec.Flags = ImPlotItemFlags_NoLegend;
      ImPlot::PlotInfLines("##tracker_cursor", &clamped_cursor_time, 1, cursor_spec);
    }
    if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      state->tracker_time = std::clamp(ImPlot::GetPlotMousePos().x, state->route_x_min, state->route_x_max);
      state->has_tracker_time = true;
    }
    ImPlot::EndPlot();
  }
  app_pop_mono_font();
  clamp_shared_range(state, session);
  if (std::abs(state->x_view_min - previous_x_min) > 1.0e-6
      || std::abs(state->x_view_max - previous_x_max) > 1.0e-6) {
    if (!state->suppress_range_side_effects) {
      if (session.data_mode == SessionDataMode::Stream) {
        state->follow_latest = infer_stream_follow_state(*state, session);
      } else {
        state->follow_latest = false;
      }
    }
  }
  ImPlot::PopStyleColor(6);
}

std::optional<PaneMenuAction> draw_pane_context_menu(const WorkspaceTab &tab, int pane_index) {
  if (!ImGui::BeginPopupContextWindow("##pane_context")) return std::nullopt;

  PaneMenuAction action;
  action.pane_index = pane_index;
  const Pane *pane = pane_index >= 0 && pane_index < static_cast<int>(tab.panes.size())
    ? &tab.panes[static_cast<size_t>(pane_index)]
    : nullptr;
  const bool has_curves = pane_index >= 0
    && pane_index < static_cast<int>(tab.panes.size())
    && !tab.panes[static_cast<size_t>(pane_index)].curves.empty();
  const bool is_plot = pane != nullptr && pane->kind == PaneKind::Plot;
  if (bootstrap_icons::menuItem("sliders", "Edit Axis Limits...", nullptr, false, is_plot)) {
    action.kind = PaneMenuActionKind::OpenAxisLimits;
  }
  bootstrap_icons::menuItem("palette", "Edit Curve Style...", nullptr, false, false && is_plot);
  if (action.kind == PaneMenuActionKind::None
      && bootstrap_icons::menuItem("plus-slash-minus", "Apply filter to data...", nullptr, false, has_curves && is_plot)) {
    action.kind = PaneMenuActionKind::OpenCustomSeries;
  }
  ImGui::Separator();
  if (action.kind == PaneMenuActionKind::None && bootstrap_icons::menuItem("distribute-horizontal", "Split Left / Right")) {
    action.kind = PaneMenuActionKind::SplitRight;
  } else if (action.kind == PaneMenuActionKind::None
             && bootstrap_icons::menuItem("distribute-vertical", "Split Top / Bottom")) {
    action.kind = PaneMenuActionKind::SplitBottom;
  }
  ImGui::Separator();
  if (bootstrap_icons::menuItem("zoom-out", "Zoom Out", nullptr, false, is_plot)) {
    action.kind = PaneMenuActionKind::ResetView;
  } else if (bootstrap_icons::menuItem("arrow-left-right", "Zoom Out Horizontally", nullptr, false, is_plot)) {
    action.kind = PaneMenuActionKind::ResetHorizontal;
  } else if (bootstrap_icons::menuItem("arrow-down-up", "Zoom Out Vertically", nullptr, false, is_plot)) {
    action.kind = PaneMenuActionKind::ResetVertical;
  }
  ImGui::Separator();
  if (bootstrap_icons::menuItem("trash", "Remove ALL curves", nullptr, false, is_plot)) {
    action.kind = PaneMenuActionKind::Clear;
  }
  ImGui::Separator();
  bootstrap_icons::menuItem("arrow-left-right", "Flip Horizontal Axis", nullptr, false, false);
  bootstrap_icons::menuItem("arrow-down-up", "Flip Vertical Axis", nullptr, false, false);
  ImGui::Separator();
  bootstrap_icons::menuItem("files", "Copy", nullptr, false, false);
  bootstrap_icons::menuItem("clipboard2", "Paste", nullptr, false, false);
  bootstrap_icons::menuItem("file-earmark-image", "Copy image to clipboard", nullptr, false, false);
  bootstrap_icons::menuItem("save", "Save plot to file", nullptr, false, false);
  bootstrap_icons::menuItem("bar-chart", "Show data statistics", nullptr, false, false);
  ImGui::Separator();
  if (bootstrap_icons::menuItem("x-square", "Close Pane")) {
    action.kind = PaneMenuActionKind::Close;
  }
  ImGui::EndPopup();
  if (action.kind == PaneMenuActionKind::None) return std::nullopt;
  return action;
}

std::optional<PaneDropAction> draw_pane_drop_target(int tab_index, int pane_index, const Pane &target_pane) {
  if (ImGui::GetDragDropPayload() == nullptr) return std::nullopt;

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
      auto try_accept = [&](const char *type) -> const ImGuiPayload * {
        const ImGuiPayload *p = ImGui::AcceptDragDropPayload(type, ImGuiDragDropFlags_AcceptBeforeDelivery);
        if (p && p->Preview) {
          draw_list->AddRectFilled(zone.rect.Min, zone.rect.Max, IM_COL32(70, 130, 220, 55));
          draw_list->AddRect(zone.rect.Min, zone.rect.Max, IM_COL32(45, 95, 175, 220), 0.0f, 0, 2.0f);
        }
        return p;
      };
      auto deliver = [&](PaneDropAction action) -> std::optional<PaneDropAction> {
        action.zone = zone.zone;
        action.target_pane_index = pane_index;
        ImGui::EndDragDropTarget();
        ImGui::PopID();
        return action;
      };
      if (const ImGuiPayload *p = try_accept("JOTP_BROWSER_PATHS"); p && p->Delivery) {
        if (zone.zone != PaneDropZone::Center || target_pane.kind == PaneKind::Plot) {
          PaneDropAction action;
          action.from_browser = true;
          action.browser_paths = decode_browser_drag_payload(static_cast<const char *>(p->Data));
          return deliver(std::move(action));
        }
      }
      if (zone.zone == PaneDropZone::Center ? pane_can_accept_special_item(target_pane) : true) {
        if (const ImGuiPayload *p = try_accept("JOTP_SPECIAL_ITEM"); p && p->Delivery) {
          PaneDropAction action;
          action.special_item_id = static_cast<const char *>(p->Data);
          return deliver(std::move(action));
        }
      }
      if (const ImGuiPayload *p = try_accept("JOTP_PANE_CURVE"); p && p->Delivery) {
        if (zone.zone != PaneDropZone::Center || target_pane.kind == PaneKind::Plot) {
          PaneDropAction action;
          action.curve_ref = *static_cast<const PaneCurveDragPayload *>(p->Data);
          return deliver(std::move(action));
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
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  TabUiState *tab_state = app_active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) return false;

  const int original_pane_count = static_cast<int>(tab->panes.size());
  const SketchLayout before_layout = session->layout;
  bool dock_changed = false;
  bool layout_changed = false;
  switch (action.kind) {
    case PaneMenuActionKind::OpenAxisLimits:
      tab_state->active_pane_index = pane_index;
      open_axis_limits_editor(*session, state, pane_index);
      state->status_text = "Axis limits editor opened";
      return true;
    case PaneMenuActionKind::OpenCustomSeries:
      tab_state->active_pane_index = pane_index;
      open_custom_series_editor(state, preferred_custom_series_source(tab->panes[static_cast<size_t>(pane_index)]));
      state->status_text = "Custom series editor opened";
      return true;
    case PaneMenuActionKind::SplitLeft:
    case PaneMenuActionKind::SplitRight:
    case PaneMenuActionKind::SplitTop:
    case PaneMenuActionKind::SplitBottom: {
      constexpr PaneDropZone kZones[] = {PaneDropZone::Left, PaneDropZone::Right, PaneDropZone::Top, PaneDropZone::Bottom};
      const auto zone = kZones[static_cast<int>(action.kind) - static_cast<int>(PaneMenuActionKind::SplitLeft)];
      if (split_pane(tab, pane_index, zone)) {
        tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
        dock_changed = true;
        layout_changed = true;
      }
      break;
    }
    case PaneMenuActionKind::ResetView:
      reset_shared_range(state, *session);
      state->follow_latest = session->data_mode == SessionDataMode::Stream;
      state->suppress_range_side_effects = true;
      clamp_shared_range(state, *session);
      persist_shared_range_to_tab(tab, *state);
      clear_pane_vertical_limits(&tab->panes[static_cast<size_t>(pane_index)]);
      layout_changed = true;
      state->status_text = "Plot view reset";
      break;
    case PaneMenuActionKind::ResetHorizontal:
      reset_shared_range(state, *session);
      state->follow_latest = session->data_mode == SessionDataMode::Stream;
      state->suppress_range_side_effects = true;
      clamp_shared_range(state, *session);
      persist_shared_range_to_tab(tab, *state);
      layout_changed = true;
      state->status_text = "Horizontal zoom reset";
      break;
    case PaneMenuActionKind::ResetVertical:
      clear_pane_vertical_limits(&tab->panes[static_cast<size_t>(pane_index)]);
      layout_changed = true;
      state->status_text = "Vertical zoom reset";
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
  resize_tab_pane_state(tab_state, tab->panes.size());
  bool autosave_ok = true;
  if (layout_changed) {
    state->undo.push(before_layout);
    autosave_ok = mark_layout_dirty(session, state);
  }
  if (autosave_ok) {
    state->status_text = "Workspace updated";
  }
  return true;
}

bool apply_pane_drop_action(AppSession *session, UiState *state, const PaneDropAction &action) {
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  TabUiState *tab_state = app_active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) return false;

  if (!action.special_item_id.empty()) {
    if (action.zone == PaneDropZone::Center) {
      if (action.target_pane_index < 0 || action.target_pane_index >= static_cast<int>(tab->panes.size())) {
        return false;
      }
      if (!pane_can_accept_special_item(tab->panes[static_cast<size_t>(action.target_pane_index)])) {
        state->status_text = std::string(special_item_label(action.special_item_id)) + " can only replace another special pane or use an empty pane";
        return false;
      }
      const SketchLayout before_layout = session->layout;
      bool changed = false;
      if (action.special_item_id == "map") {
        changed = convert_pane_to_map(tab, tab_state, action.target_pane_index);
      } else if (const auto view = camera_view_from_special_item(action.special_item_id)) {
        changed = convert_pane_to_camera(tab, tab_state, action.target_pane_index, *view);
      } else {
        return false;
      }
      if (!changed) {
        state->status_text = std::string(special_item_label(action.special_item_id)) + " already shown in pane";
        return false;
      }
      resize_tab_pane_state(tab_state, tab->panes.size());
      state->undo.push(before_layout);
      if (mark_layout_dirty(session, state)) {
        state->status_text = std::string(special_item_label(action.special_item_id)) + " added to pane";
      }
      return true;
    }
    const SketchLayout before_layout = session->layout;
    if (split_pane(tab, action.target_pane_index, action.zone)) {
      tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      resize_tab_pane_state(tab_state, tab->panes.size());
      bool changed = false;
      if (action.special_item_id == "map") {
        changed = convert_pane_to_map(tab, tab_state, tab_state->active_pane_index);
      } else if (const auto view = camera_view_from_special_item(action.special_item_id)) {
        changed = convert_pane_to_camera(tab, tab_state, tab_state->active_pane_index, *view);
      }
      if (!changed) {
        return false;
      }
      mark_tab_dock_dirty(state, state->active_tab_index);
      state->undo.push(before_layout);
      if (mark_layout_dirty(session, state)) {
        state->status_text = "Split pane and added " + std::string(special_item_label(action.special_item_id));
      }
      return true;
    }
    return false;
  }

  if (action.from_browser) {
    if (action.browser_paths.empty()) return false;
    if (action.zone == PaneDropZone::Center) {
      const int inserted_count = add_path_curves_to_pane(session, state, action.target_pane_index, action.browser_paths);
      if (inserted_count > 0) {
        tab_state->active_pane_index = action.target_pane_index;
      }
      return inserted_count > 0;
    }
    const SketchLayout before_layout = session->layout;
    if (split_pane(tab, action.target_pane_index, action.zone)) {
      tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
      resize_tab_pane_state(tab_state, tab->panes.size());
      int inserted_count = 0;
      for (const std::string &path : action.browser_paths) {
        if (app_find_route_series(*session, path) == nullptr) continue;
        if (add_curve_to_pane(tab, tab_state->active_pane_index,
                              make_curve_for_path(tab->panes[static_cast<size_t>(tab_state->active_pane_index)], path))) {
          ++inserted_count;
        }
      }
      mark_tab_dock_dirty(state, state->active_tab_index);
      if (inserted_count > 0) {
        state->undo.push(before_layout);
        mark_layout_dirty(session, state);
        state->status_text = inserted_count == 1
          ? "Split pane and added " + action.browser_paths.front()
          : "Split pane and added " + std::to_string(inserted_count) + " curves";
        return true;
      }
      return false;
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
    const SketchLayout before_layout = session->layout;
    const bool inserted = add_curve_to_pane(tab, action.target_pane_index, curve);
    tab_state->active_pane_index = action.target_pane_index;
    if (inserted) {
      state->undo.push(before_layout);
      if (mark_layout_dirty(session, state)) {
        state->status_text = "Added " + app_curve_display_name(curve);
      }
    } else {
      state->status_text = "Curve already present";
    }
    return true;
  }
  const SketchLayout before_layout = session->layout;
  if (split_pane(tab, action.target_pane_index, action.zone, curve)) {
    tab_state->active_pane_index = static_cast<int>(tab->panes.size()) - 1;
    resize_tab_pane_state(tab_state, tab->panes.size());
    mark_tab_dock_dirty(state, state->active_tab_index);
    state->undo.push(before_layout);
    if (mark_layout_dirty(session, state)) {
      state->status_text = "Split pane and added " + app_curve_display_name(curve);
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
  const bool size_changed = std::abs(tab_state->last_dockspace_size.x - dockspace_size.x) > 1.0f
                         || std::abs(tab_state->last_dockspace_size.y - dockspace_size.y) > 1.0f;
  if (!tab_state->dock_needs_build && !size_changed) {
    return;
  }

  const ImGuiID dockspace_id = dockspace_id_for_tab(tab_state->runtime_id);
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
  ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);
  build_dock_tree(tab.root, tab, tab_state->runtime_id, dockspace_id);
  ImGui::DockBuilderFinish(dockspace_id);
  tab_state->dock_needs_build = false;
  tab_state->last_dockspace_size = dockspace_size;
}

void draw_pane_windows(AppSession *session, UiState *state) {
  WorkspaceTab *tab = app_active_tab(&session->layout, *state);
  TabUiState *tab_state = app_active_tab_state(state);
  if (tab == nullptr || tab_state == nullptr) {
    return;
  }

  std::optional<std::pair<int, PaneMenuAction>> pending_menu_action;
  std::optional<int> pending_close_pane;
  std::optional<PaneDropAction> pending_drop_action;

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
      if (pane.kind == PaneKind::Map) {
        draw_map_pane(session, state, &pane, static_cast<int>(i));
      } else if (pane.kind == PaneKind::Camera) {
        draw_camera_pane(session, state, tab_state, static_cast<int>(i), pane);
      } else {
        draw_plot(*session, &pane, state);
      }
      draw_pane_frame_overlay();
      close_pane_requested = draw_pane_close_button_overlay();
      menu_action = draw_pane_context_menu(*tab, static_cast<int>(i));
      drop_action = draw_pane_drop_target(state->active_tab_index, static_cast<int>(i), pane);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    if (!pending_menu_action.has_value() && menu_action.has_value()) {
      pending_menu_action = std::make_pair(static_cast<int>(i), *menu_action);
    }
    if (!pending_menu_action.has_value() && !pending_close_pane.has_value() && close_pane_requested) {
      pending_close_pane = static_cast<int>(i);
    }
    if (!pending_menu_action.has_value() && !pending_close_pane.has_value()
        && !pending_drop_action.has_value() && drop_action.has_value()) {
      pending_drop_action = *drop_action;
    }
  }

  if (pending_menu_action.has_value()) {
    apply_pane_menu_action(session, state, pending_menu_action->first, pending_menu_action->second);
    return;
  }
  if (pending_close_pane.has_value()) {
    PaneMenuAction action;
    action.kind = PaneMenuActionKind::Close;
    action.pane_index = *pending_close_pane;
    apply_pane_menu_action(session, state, *pending_close_pane, action);
    return;
  }
  if (pending_drop_action.has_value()) {
    apply_pane_drop_action(session, state, *pending_drop_action);
  }
}

void draw_workspace(AppSession *session, const UiMetrics &ui, UiState *state) {
  state->custom_series.selected = false;
  state->logs.selected = false;
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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
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
      bool suppress_aux_tabs_this_frame = state->request_close_tab && session->layout.tabs.size() == 1;
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
          if (session->layout.tabs.size() == 1) {
            suppress_aux_tabs_this_frame = true;
          }
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
      if (!suppress_aux_tabs_this_frame) {
        ImGuiTabItemFlags logs_flags = ImGuiTabItemFlags_None;
        if (state->logs.request_select) {
          logs_flags |= ImGuiTabItemFlags_SetSelected;
        }
        if (ImGui::BeginTabItem("Logs##workspace_logs", nullptr, logs_flags)) {
          state->logs.request_select = false;
          state->logs.selected = true;
          draw_logs_tab(session, state);
          ImGui::EndTabItem();
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
        const SketchLayout before_layout = session->layout;
        create_runtime_tab(&session->layout, state);
        state->undo.push(before_layout);
        mark_layout_dirty(session, state);
        state->request_new_tab = false;
      } else if (pending_action == TabActionKind::Rename) {
        begin_rename_tab(session->layout, state, pending_tab_index);
      } else if (state->request_duplicate_tab || pending_action == TabActionKind::Duplicate) {
        if (pending_tab_index >= 0) {
          request_tab_selection(state, pending_tab_index);
        }
        const SketchLayout before_layout = session->layout;
        duplicate_runtime_tab(&session->layout, state);
        state->undo.push(before_layout);
        mark_layout_dirty(session, state);
        state->request_duplicate_tab = false;
      } else if (state->request_close_tab || pending_action == TabActionKind::Close) {
        if (pending_tab_index >= 0) {
          request_tab_selection(state, pending_tab_index);
        }
        const SketchLayout before_layout = session->layout;
        close_runtime_tab(&session->layout, state);
        state->undo.push(before_layout);
        mark_layout_dirty(session, state);
        state->request_close_tab = false;
      }
      if (state->requested_tab_index == selection_request) {
        state->requested_tab_index = -1;
      }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

#include "tools/jotpluggler/app_layout_flow.cc"

#include "tools/jotpluggler/app_render_flow.cc"

int run(const Options &options) {
  try {
  const fs::path layout_path = options.layout.empty() ? fs::path() : resolve_layout_path(options.layout);
  AppSession session = {
    .layout_path = layout_path,
    .autosave_path = layout_path.empty() ? fs::path() : autosave_path_for_layout(layout_path),
    .route_name = options.route_name,
    .data_dir = options.data_dir,
    .dbc_override = {},
    .stream_address = options.stream_address,
    .stream_buffer_seconds = options.stream_buffer_seconds,
    .data_mode = options.stream ? SessionDataMode::Stream : SessionDataMode::Route,
    .route_id = options.stream ? RouteIdentifier{} : parse_route_identifier(options.route_name),
    .layout = options.layout.empty() ? make_empty_layout() : load_sketch_layout(layout_path),
  };
  UiState ui_state;
  if (!layout_path.empty() && !session.autosave_path.empty() && fs::exists(session.autosave_path)) {
    session.layout = load_sketch_layout(session.autosave_path);
    ui_state.layout_dirty = true;
  }
  ui_state.undo.reset(session.layout);
  sync_ui_state(&ui_state, session.layout);
  sync_route_buffers(&ui_state, session);
  sync_stream_buffers(&ui_state, session);
  sync_layout_buffers(&ui_state, session);

  session.async_route_loading = session.data_mode == SessionDataMode::Route
    && options.show && options.output_path.empty() && !options.sync_load;
  if (session.data_mode == SessionDataMode::Route && !session.async_route_loading) {
    TerminalRouteProgress route_progress(::isatty(STDERR_FILENO) != 0);
    rebuild_session_route_data(&session, &ui_state, [&](const RouteLoadProgress &update) {
      route_progress.update(update);
    });
    route_progress.finish();
  }

  GlfwRuntime glfw_runtime(options);
  ImGuiRuntime imgui_runtime(glfw_runtime.window());
  configure_style();
  session.map_data = std::make_unique<MapDataManager>();
  for (std::unique_ptr<SidebarCameraFeed> &feed : session.pane_camera_feeds) {
    feed = std::make_unique<SidebarCameraFeed>();
  }
  sync_camera_feeds(&session);

  if (session.async_route_loading) {
    session.route_loader = std::make_unique<AsyncRouteLoader>(::isatty(STDERR_FILENO) != 0);
    start_async_route_load(&session, &ui_state);
  } else if (session.data_mode == SessionDataMode::Stream) {
    session.stream_poller = std::make_unique<StreamPoller>();
    start_stream_session(&session, &ui_state, session.stream_address, session.stream_buffer_seconds);
  }

  const bool should_capture = !options.output_path.empty();
  const fs::path output_path = should_capture ? fs::path(options.output_path) : fs::path();
  const bool capture_has_map = should_capture && active_tab_has_map_pane(session.layout);
  if (options.show) {
    bool captured = false;
    const auto capture_ready_at = std::chrono::steady_clock::now() + (capture_has_map ? std::chrono::milliseconds(1800)
                                                                                      : std::chrono::milliseconds(0));
    while (!glfwWindowShouldClose(glfw_runtime.window())) {
      const bool capture_ready = std::chrono::steady_clock::now() >= capture_ready_at;
      const fs::path *capture_path = (!captured && should_capture && capture_ready) ? &output_path : nullptr;
      render_frame(glfw_runtime.window(), &session, &ui_state, capture_path);
      captured = captured || capture_path != nullptr;
    }
  } else {
    render_frame(glfw_runtime.window(), &session, &ui_state, nullptr);
    if (should_capture) {
      for (int i = 0; i < 3; ++i) {
        render_frame(glfw_runtime.window(), &session, &ui_state, nullptr);
      }
      if (capture_has_map) {
        for (int i = 0; i < 18; ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          render_frame(glfw_runtime.window(), &session, &ui_state, nullptr);
        }
      }
      render_frame(glfw_runtime.window(), &session, &ui_state, &output_path);
    }
  }
  if (session.stream_poller) {
    session.stream_poller->stop();
  }
  session.map_data.reset();
  for (std::unique_ptr<SidebarCameraFeed> &feed : session.pane_camera_feeds) {
    feed.reset();
  }
  return 0;
  } catch (const std::exception &err) {
    std::cerr << err.what() << "\n";
    return 1;
  }
}
