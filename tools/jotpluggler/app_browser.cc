#include "tools/jotpluggler/app_browser.h"

#include "tools/jotpluggler/app_custom_series.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace jotpluggler {
namespace {

constexpr float kBrowserValueWidth = 88.0f;

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

bool is_deprecated_browser_path(const std::string &path) {
  return path.find("DEPRECATED") != std::string::npos;
}

std::vector<std::string> visible_browser_paths(const RouteData &route_data, bool show_deprecated_fields) {
  if (show_deprecated_fields) {
    return route_data.paths;
  }
  std::vector<std::string> filtered;
  filtered.reserve(route_data.paths.size());
  for (const std::string &path : route_data.paths) {
    if (!is_deprecated_browser_path(path)) {
      filtered.push_back(path);
    }
  }
  return filtered;
}

bool browser_selection_contains(const UiState &state, std::string_view path) {
  return std::find(state.selected_browser_paths.begin(), state.selected_browser_paths.end(), path)
    != state.selected_browser_paths.end();
}

std::vector<std::string> browser_drag_paths(const UiState &state, const std::string &dragged_path) {
  if (browser_selection_contains(state, dragged_path) && !state.selected_browser_paths.empty()) {
    return state.selected_browser_paths;
  }
  return {dragged_path};
}

std::string encode_browser_drag_payload(const std::vector<std::string> &paths) {
  std::string payload;
  for (size_t i = 0; i < paths.size(); ++i) {
    if (i != 0) {
      payload.push_back('\n');
    }
    payload += paths[i];
  }
  return payload;
}

void set_browser_selection_single(UiState *state, const std::string &path) {
  state->selected_browser_paths = {path};
  state->selected_browser_path = path;
  state->browser_selection_anchor = path;
}

void toggle_browser_selection(UiState *state, const std::string &path) {
  auto it = std::find(state->selected_browser_paths.begin(), state->selected_browser_paths.end(), path);
  if (it == state->selected_browser_paths.end()) {
    state->selected_browser_paths.push_back(path);
  } else {
    state->selected_browser_paths.erase(it);
  }
  state->selected_browser_path = path;
  state->browser_selection_anchor = path;
  if (state->selected_browser_paths.empty()) {
    state->selected_browser_path.clear();
  }
}

void select_browser_range(UiState *state, const std::vector<std::string> &visible_paths, const std::string &clicked_path) {
  if (visible_paths.empty()) {
    set_browser_selection_single(state, clicked_path);
    return;
  }

  const std::string anchor = state->browser_selection_anchor.empty() ? clicked_path : state->browser_selection_anchor;
  const auto anchor_it = std::find(visible_paths.begin(), visible_paths.end(), anchor);
  const auto clicked_it = std::find(visible_paths.begin(), visible_paths.end(), clicked_path);
  if (clicked_it == visible_paths.end()) {
    return;
  }
  if (anchor_it == visible_paths.end()) {
    set_browser_selection_single(state, clicked_path);
    return;
  }

  const auto [begin_it, end_it] = std::minmax(anchor_it, clicked_it);
  std::vector<std::string> selected;
  selected.reserve(static_cast<size_t>(std::distance(begin_it, end_it)) + 1);
  for (auto it = begin_it; it != end_it + 1; ++it) {
    selected.push_back(*it);
  }
  state->selected_browser_paths = std::move(selected);
  state->selected_browser_path = clicked_path;
}

void prune_browser_selection(UiState *state, const std::vector<std::string> &visible_paths) {
  auto is_visible = [&](const std::string &path) {
    return std::find(visible_paths.begin(), visible_paths.end(), path) != visible_paths.end();
  };

  state->selected_browser_paths.erase(
    std::remove_if(state->selected_browser_paths.begin(), state->selected_browser_paths.end(),
                   [&](const std::string &path) { return !is_visible(path); }),
    state->selected_browser_paths.end());

  if (!state->selected_browser_path.empty() && !is_visible(state->selected_browser_path)) {
    state->selected_browser_path.clear();
  }
  if (!state->browser_selection_anchor.empty() && !is_visible(state->browser_selection_anchor)) {
    state->browser_selection_anchor.clear();
  }
  if (state->selected_browser_paths.empty()) {
    state->selected_browser_path.clear();
  } else if (state->selected_browser_path.empty()) {
    state->selected_browser_path = state->selected_browser_paths.back();
  }
}

BrowserSeriesDisplayInfo compute_browser_display_info_impl(const AppSession &session, const RouteSeries &series) {
  const bool enum_like = session.route_data.enum_info.find(series.path) != session.route_data.enum_info.end();
  BrowserSeriesDisplayInfo info;
  if (series.values.empty()) {
    return info;
  }
  const size_t sample_limit = 128;
  const size_t step = std::max<size_t>(1, series.values.size() / sample_limit);
  double peak_abs = 0.0;
  bool integer_like = enum_like;
  std::vector<int> unique_levels;
  unique_levels.reserve(8);
  for (size_t i = 0; i < series.values.size(); i += step) {
    const double value = series.values[i];
    if (!std::isfinite(value)) {
      continue;
    }
    peak_abs = std::max(peak_abs, std::abs(value));
    const double rounded = std::round(value);
    if (std::abs(value - rounded) > 1.0e-6) {
      integer_like = false;
      continue;
    }
    const int level = static_cast<int>(rounded);
    if (std::find(unique_levels.begin(), unique_levels.end(), level) == unique_levels.end()) {
      unique_levels.push_back(level);
      if (unique_levels.size() > 8) {
        integer_like = false;
      }
    }
  }

  info.integer_like = integer_like;
  if (integer_like || enum_like) {
    info.decimals = 0;
    return info;
  }
  if (peak_abs >= 1000.0) {
    info.decimals = 0;
  } else if (peak_abs >= 100.0) {
    info.decimals = 1;
  } else if (peak_abs >= 10.0) {
    info.decimals = 2;
  } else if (peak_abs >= 0.01) {
    info.decimals = 3;
  } else {
    info.decimals = 4;
  }
  return info;
}

std::string format_display_value_impl(double display_value,
                                      const BrowserSeriesDisplayInfo &display_info,
                                      const EnumInfo *enum_info) {
  if (!std::isfinite(display_value)) {
    return {};
  }
  if (enum_info != nullptr) {
    const int idx = static_cast<int>(std::llround(display_value));
    if (idx >= 0 && std::abs(display_value - static_cast<double>(idx)) < 0.01
        && static_cast<size_t>(idx) < enum_info->names.size()
        && !enum_info->names[static_cast<size_t>(idx)].empty()) {
      return enum_info->names[static_cast<size_t>(idx)];
    }
  }

  char buf[64] = {};
  if (display_info.integer_like) {
    std::snprintf(buf, sizeof(buf), "%.0f", std::round(display_value));
  } else if (std::abs(display_value) < 1.0e-6) {
    std::snprintf(buf, sizeof(buf), "0");
  } else {
    std::snprintf(buf, sizeof(buf), "%.*f", display_info.decimals, display_value);
  }
  return buf;
}

std::optional<double> sample_route_series_value(const RouteSeries &series, double tm, bool stairs) {
  if (series.times.empty() || series.times.size() != series.values.size()) {
    return std::nullopt;
  }
  if (tm <= series.times.front()) {
    return series.values.front();
  }
  if (tm >= series.times.back()) {
    return series.values.back();
  }

  const auto upper = std::lower_bound(series.times.begin(), series.times.end(), tm);
  if (upper == series.times.begin()) {
    return series.values.front();
  }
  if (upper == series.times.end()) {
    return series.values.back();
  }

  const size_t upper_index = static_cast<size_t>(std::distance(series.times.begin(), upper));
  const size_t lower_index = upper_index - 1;
  const double x0 = series.times[lower_index];
  const double x1 = series.times[upper_index];
  const double y0 = series.values[lower_index];
  const double y1 = series.values[upper_index];
  if (stairs || std::abs(tm - x1) >= 1.0e-9) {
    return y0;
  }
  if (x1 <= x0) {
    return y0;
  }
  const double alpha = (tm - x0) / (x1 - x0);
  return y0 + (y1 - y0) * alpha;
}

std::string browser_series_value_text(const AppSession &session, const UiState &state, std::string_view path) {
  auto it = session.series_by_path.find(std::string(path));
  if (it == session.series_by_path.end() || it->second == nullptr) {
    return {};
  }

  const RouteSeries &series = *it->second;
  if (series.values.empty()) {
    return {};
  }

  const auto enum_it = session.route_data.enum_info.find(series.path);
  const EnumInfo *enum_info = enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second;
  const bool stairs = enum_info != nullptr;

  std::optional<double> value;
  if (state.has_tracker_time) {
    value = sample_route_series_value(series, state.tracker_time, stairs);
  } else {
    value = series.values.back();
  }
  if (!value.has_value()) {
    return {};
  }

  const auto display_it = session.browser_display_by_path.find(series.path);
  const BrowserSeriesDisplayInfo display_info = display_it == session.browser_display_by_path.end()
    ? BrowserSeriesDisplayInfo{}
    : display_it->second;

  return format_display_value_impl(*value, display_info, enum_info);
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

}  // namespace

BrowserSeriesDisplayInfo compute_browser_display_info(const AppSession &session, const RouteSeries &series) {
  return compute_browser_display_info_impl(session, series);
}

std::string format_display_value(double display_value,
                                 const BrowserSeriesDisplayInfo &display_info,
                                 const EnumInfo *enum_info) {
  return format_display_value_impl(display_value, display_info, enum_info);
}

std::vector<std::string> decode_browser_drag_payload(std::string_view payload) {
  std::vector<std::string> out;
  size_t begin = 0;
  while (begin <= payload.size()) {
    const size_t end = payload.find('\n', begin);
    const size_t length = (end == std::string_view::npos ? payload.size() : end) - begin;
    if (length > 0) {
      out.emplace_back(payload.substr(begin, length));
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return out;
}

void collect_visible_leaf_paths(const BrowserNode &node,
                                const std::string &filter,
                                std::vector<std::string> *out) {
  if (!browser_node_matches(node, filter)) {
    return;
  }
  if (node.children.empty()) {
    if (!node.full_path.empty()) {
      out->push_back(node.full_path);
    }
    return;
  }
  for (const BrowserNode &child : node.children) {
    collect_visible_leaf_paths(child, filter, out);
  }
}

void rebuild_browser_nodes(AppSession *session, UiState *state) {
  const std::vector<std::string> paths = visible_browser_paths(session->route_data, state->show_deprecated_fields);
  session->browser_nodes = build_browser_tree(paths);
  prune_browser_selection(state, paths);
}

void rebuild_route_index(AppSession *session) {
  session->series_by_path.clear();
  session->browser_display_by_path.clear();
  for (const RouteSeries &series : session->route_data.series) {
    session->series_by_path.emplace(series.path, &series);
    session->browser_display_by_path.emplace(series.path, compute_browser_display_info_impl(*session, series));
  }
}

void draw_browser_node(AppSession *session,
                       const BrowserNode &node,
                       UiState *state,
                       const std::string &filter,
                       const std::vector<std::string> &visible_paths) {
  if (!browser_node_matches(node, filter)) {
    return;
  }

  if (node.children.empty()) {
    const bool selected = browser_selection_contains(*state, node.full_path);
    const std::string value_text = browser_series_value_text(*session, *state, node.full_path);
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 row_size(std::max(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight());
    ImGui::PushID(node.full_path.c_str());
    const bool clicked = ImGui::InvisibleButton("##browser_leaf", row_size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    if (selected || hovered) {
      const ImU32 bg = ImGui::GetColorU32(selected
        ? (held ? ImGuiCol_HeaderActive : ImGuiCol_Header)
        : ImGuiCol_HeaderHovered);
      draw_list->AddRectFilled(rect.Min, rect.Max, bg, 0.0f);
    }

    const float value_right = rect.Max.x - style.FramePadding.x;
    const float value_left = value_right - (value_text.empty() ? 0.0f : kBrowserValueWidth);
    const float label_left = rect.Min.x + style.FramePadding.x;
    const float label_right = value_text.empty()
      ? rect.Max.x - style.FramePadding.x
      : std::max(label_left + 40.0f, value_left - 10.0f);
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(label_left, rect.Min.y + style.FramePadding.y),
                              ImVec2(label_right, rect.Max.y),
                              label_right,
                              node.label.c_str(),
                              nullptr,
                              nullptr);
    if (!value_text.empty()) {
      app_push_mono_font();
      ImGui::PushStyleColor(ImGuiCol_Text, selected ? color_rgb(70, 77, 86) : color_rgb(116, 124, 133));
      ImGui::RenderTextClipped(ImVec2(value_left, rect.Min.y + style.FramePadding.y),
                               ImVec2(value_right, rect.Max.y),
                               value_text.c_str(),
                               nullptr,
                               nullptr,
                               ImVec2(1.0f, 0.0f));
      ImGui::PopStyleColor();
      app_pop_mono_font();
    }

    if (clicked) {
      const bool shift_down = ImGui::GetIO().KeyShift;
      const bool ctrl_down = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
      if (shift_down) {
        select_browser_range(state, visible_paths, node.full_path);
      } else if (ctrl_down) {
        toggle_browser_selection(state, node.full_path);
      } else {
        set_browser_selection_single(state, node.full_path);
      }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(0)) {
      set_browser_selection_single(state, node.full_path);
      app_add_curve_to_active_pane(session, state, node.full_path);
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      const std::vector<std::string> drag_paths = browser_drag_paths(*state, node.full_path);
      const std::string payload = encode_browser_drag_payload(drag_paths);
      ImGui::SetDragDropPayload("JOTP_BROWSER_PATHS", payload.c_str(), payload.size() + 1);
      if (drag_paths.size() == 1) {
        ImGui::TextUnformatted(drag_paths.front().c_str());
      } else {
        ImGui::Text("%zu timeseries", drag_paths.size());
        ImGui::TextUnformatted(drag_paths.front().c_str());
      }
      ImGui::EndDragDropSource();
    }
    ImGui::PopID();
    return;
  }

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
  if (!filter.empty()) {
    flags |= ImGuiTreeNodeFlags_DefaultOpen;
  }
  const bool open = ImGui::TreeNodeEx(node.label.c_str(), flags);
  if (open) {
    for (const BrowserNode &child : node.children) {
      draw_browser_node(session, child, state, filter, visible_paths);
    }
    ImGui::TreePop();
  }
}

}  // namespace jotpluggler
