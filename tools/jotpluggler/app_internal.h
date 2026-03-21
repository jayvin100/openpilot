#pragma once

#include "tools/jotpluggler/app.h"
#include "tools/jotpluggler/sketch_layout.h"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jotpluggler {

class AsyncRouteLoader;
class SidebarCameraFeed;

struct BrowserNode {
  std::string label;
  std::string full_path;
  std::vector<BrowserNode> children;
};

struct AppSession {
  std::filesystem::path layout_path;
  std::filesystem::path autosave_path;
  std::string route_name;
  std::string data_dir;
  SketchLayout layout;
  RouteData route_data;
  std::unordered_map<std::string, const RouteSeries *> series_by_path;
  std::vector<BrowserNode> browser_nodes;
  std::unique_ptr<AsyncRouteLoader> route_loader;
  std::unique_ptr<SidebarCameraFeed> camera_feed;
  bool async_route_loading = false;
};

struct TabUiState {
  bool dock_needs_build = true;
  int active_pane_index = 0;
  int runtime_id = 0;
};

struct CustomSeriesEditorState {
  bool open = false;
  bool open_help = false;
  bool request_select = false;
  bool selected = false;
  bool focus_name = false;
  int selected_template = 0;
  int selected_additional_source = -1;
  std::string name;
  std::string linked_source;
  std::vector<std::string> additional_sources;
  std::string globals_code;
  std::string function_code = "return value";
  std::string preview_label;
  std::vector<double> preview_xs;
  std::vector<double> preview_ys;
  bool preview_is_result = false;
};

enum class LogTimeMode : uint8_t {
  Route,
  Boot,
  WallClock,
};

struct LogsUiState {
  bool selected = false;
  bool request_select = false;
  bool all_sources = true;
  uint32_t enabled_levels_mask = 0b11110;
  int expanded_index = -1;
  std::string search;
  std::vector<std::string> selected_sources;
  double last_auto_scroll_time = -1.0;
  LogTimeMode time_mode = LogTimeMode::Route;
};

struct UiState {
  bool open_open_route = false;
  bool open_load_layout = false;
  bool open_save_layout = false;
  bool request_close = false;
  bool request_reset_layout = false;
  bool request_save_layout = false;
  bool request_new_tab = false;
  bool request_duplicate_tab = false;
  bool request_close_tab = false;
  bool follow_latest = false;
  bool has_shared_range = false;
  bool has_tracker_time = false;
  bool layout_dirty = false;
  bool playback_loop = false;
  bool playback_playing = false;
  bool show_deprecated_fields = false;
  bool suppress_range_side_effects = false;
  int active_tab_index = 0;
  int next_tab_runtime_id = 1;
  int requested_tab_index = -1;
  int rename_tab_index = -1;
  bool focus_rename_tab_input = false;
  std::vector<TabUiState> tabs;
  std::array<char, 128> route_buffer = {};
  std::array<char, 128> rename_tab_buffer = {};
  std::array<char, 128> browser_filter = {};
  std::array<char, 512> data_dir_buffer = {};
  std::array<char, 512> load_layout_buffer = {};
  std::array<char, 512> save_layout_buffer = {};
  std::string selected_browser_path;
  std::vector<std::string> selected_browser_paths;
  std::string browser_selection_anchor;
  std::string error_text;
  bool open_error_popup = false;
  std::string status_text = "Ready";
  float sidebar_width = 282.0f;
  double route_x_min = 0.0;
  double route_x_max = 1.0;
  double x_view_min = 0.0;
  double x_view_max = 1.0;
  double tracker_time = 0.0;
  double playback_rate = 1.0;
  double playback_step = 0.1;
  CustomSeriesEditorState custom_series;
  LogsUiState logs;
};

inline ImVec4 color_rgb(int r, int g, int b, float alpha = 1.0f) {
  return ImVec4(static_cast<float>(r) / 255.0f,
                static_cast<float>(g) / 255.0f,
                static_cast<float>(b) / 255.0f,
                alpha);
}

inline ImVec4 color_rgb(const std::array<uint8_t, 3> &color, float alpha = 1.0f) {
  return color_rgb(color[0], color[1], color[2], alpha);
}

const WorkspaceTab *app_active_tab(const SketchLayout &layout, const UiState &state);
WorkspaceTab *app_active_tab(SketchLayout *layout, const UiState &state);
TabUiState *app_active_tab_state(UiState *state);

std::string app_curve_display_name(const Curve &curve);
std::array<uint8_t, 3> app_next_curve_color(const Pane &pane);
const RouteSeries *app_find_route_series(const AppSession &session, const std::string &path);
void app_decimate_samples(const std::vector<double> &xs_in,
                          const std::vector<double> &ys_in,
                          int max_points,
                          std::vector<double> *xs_out,
                          std::vector<double> *ys_out);

}  // namespace jotpluggler
