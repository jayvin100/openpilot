#include "tools/jotpluggler/app_logs.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace jotpluggler {

namespace {

struct LevelOption {
  const char *label;
  int value;
};

constexpr std::array<LevelOption, 5> kLevelOptions = {{
  {"DEBUG", 10},
  {"INFO", 20},
  {"WARNING", 30},
  {"ERROR", 40},
  {"CRITICAL", 50},
}};

int input_text_resize_callback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackResize || data->UserData == nullptr) {
    return 0;
  }
  auto *text = static_cast<std::string *>(data->UserData);
  text->resize(static_cast<size_t>(data->BufTextLen));
  data->Buf = text->data();
  return 0;
}

bool input_text_with_hint_string(const char *label,
                                 const char *hint,
                                 std::string *text,
                                 ImGuiInputTextFlags flags = 0) {
  flags |= ImGuiInputTextFlags_CallbackResize;
  if (text->capacity() == 0) {
    text->reserve(256);
  }
  return ImGui::InputTextWithHint(label,
                                  hint,
                                  text->data(),
                                  text->capacity() + 1,
                                  flags,
                                  input_text_resize_callback,
                                  text);
}

std::string lowercase_copy(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool log_matches_search(const LogEntry &entry, std::string_view query) {
  if (query.empty()) {
    return true;
  }
  const std::string needle = lowercase_copy(query);
  const auto contains = [&](std::string_view haystack) {
    return lowercase_copy(haystack).find(needle) != std::string::npos;
  };
  return contains(entry.message) || contains(entry.source) || contains(entry.func);
}

std::vector<std::string> collect_log_sources(const std::vector<LogEntry> &logs) {
  std::vector<std::string> sources;
  for (const LogEntry &entry : logs) {
    if (entry.source.empty()) {
      continue;
    }
    if (std::find(sources.begin(), sources.end(), entry.source) == sources.end()) {
      sources.push_back(entry.source);
    }
  }
  std::sort(sources.begin(), sources.end());
  return sources;
}

std::vector<int> filter_log_indices(const RouteData &route_data, const LogsUiState &logs_state) {
  std::vector<int> indices;
  indices.reserve(route_data.logs.size());
  for (size_t i = 0; i < route_data.logs.size(); ++i) {
    const LogEntry &entry = route_data.logs[i];
    if (entry.level < logs_state.min_level) {
      continue;
    }
    if (!logs_state.source_filter.empty() && entry.source != logs_state.source_filter) {
      continue;
    }
    if (!log_matches_search(entry, logs_state.search)) {
      continue;
    }
    indices.push_back(static_cast<int>(i));
  }
  return indices;
}

int find_active_log_position(const RouteData &route_data,
                             const std::vector<int> &filtered_indices,
                             double tracker_time) {
  if (filtered_indices.empty()) {
    return -1;
  }
  auto it = std::lower_bound(filtered_indices.begin(), filtered_indices.end(), tracker_time,
                             [&](int log_index, double tm) {
                               return route_data.logs[static_cast<size_t>(log_index)].mono_time < tm;
                             });
  if (it == filtered_indices.begin()) {
    return static_cast<int>(std::distance(filtered_indices.begin(), it));
  }
  if (it == filtered_indices.end()) {
    return static_cast<int>(filtered_indices.size()) - 1;
  }
  if (route_data.logs[static_cast<size_t>(*it)].mono_time > tracker_time) {
    --it;
  }
  return static_cast<int>(std::distance(filtered_indices.begin(), it));
}

std::string format_route_time(double seconds) {
  if (seconds < 0.0) {
    seconds = 0.0;
  }
  const int minutes = static_cast<int>(seconds / 60.0);
  const double remaining = seconds - static_cast<double>(minutes) * 60.0;
  char buf[32] = {};
  std::snprintf(buf, sizeof(buf), "%d:%06.3f", minutes, remaining);
  return buf;
}

std::string format_boot_time(double seconds) {
  char buf[32] = {};
  std::snprintf(buf, sizeof(buf), "%.3f", seconds);
  return buf;
}

std::string format_wall_time(double seconds) {
  if (seconds <= 0.0) {
    return "--";
  }
  const time_t wall_seconds = static_cast<time_t>(seconds);
  std::tm wall_tm = {};
  localtime_r(&wall_seconds, &wall_tm);
  const int millis = static_cast<int>(std::llround((seconds - std::floor(seconds)) * 1000.0));
  char buf[32] = {};
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                wall_tm.tm_hour, wall_tm.tm_min, wall_tm.tm_sec, millis);
  return buf;
}

std::string format_log_time(const LogEntry &entry, LogTimeMode mode) {
  switch (mode) {
    case LogTimeMode::Route:
      return format_route_time(entry.mono_time);
    case LogTimeMode::Boot:
      return format_boot_time(entry.boot_time);
    case LogTimeMode::WallClock:
      return format_wall_time(entry.wall_time);
  }
  return format_route_time(entry.mono_time);
}

const char *time_mode_label(LogTimeMode mode) {
  switch (mode) {
    case LogTimeMode::Route: return "Route";
    case LogTimeMode::Boot: return "Boot";
    case LogTimeMode::WallClock: return "Wall clock";
  }
  return "Route";
}

const char *level_label(const LogEntry &entry) {
  if (entry.origin == LogOrigin::Alert) {
    return "ALRT";
  }
  if (entry.level >= 50) return "CRIT";
  if (entry.level >= 40) return "ERR";
  if (entry.level >= 30) return "WARN";
  if (entry.level >= 20) return "INFO";
  return "DBG";
}

ImVec4 level_text_color(const LogEntry &entry, bool active) {
  if (active) {
    return color_rgb(46, 54, 63);
  }
  if (entry.origin == LogOrigin::Alert) {
    return color_rgb(50, 100, 200);
  }
  if (entry.level >= 50) return color_rgb(176, 26, 18);
  if (entry.level >= 40) return color_rgb(200, 50, 40);
  if (entry.level >= 30) return color_rgb(200, 130, 0);
  if (entry.level >= 20) return color_rgb(80, 86, 94);
  return color_rgb(126, 133, 141);
}

ImU32 row_bg_color(const LogEntry &entry, bool active) {
  if (active) {
    return IM_COL32(80, 140, 210, 38);
  }
  if (entry.origin == LogOrigin::Alert) {
    return IM_COL32(50, 100, 200, 15);
  }
  if (entry.level >= 50) return IM_COL32(255, 30, 20, 31);
  if (entry.level >= 40) return IM_COL32(255, 60, 50, 20);
  if (entry.level >= 30) return IM_COL32(255, 200, 50, 20);
  return 0;
}

void set_tracker_to_log(UiState *state, const LogEntry &entry) {
  state->tracker_time = entry.mono_time;
  state->has_tracker_time = true;
  state->logs.last_auto_scroll_time = -1.0;
}

void draw_log_expansion_row(const LogEntry &entry) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted("");
  ImGui::TableSetColumnIndex(1);
  ImGui::TextUnformatted("");
  ImGui::TableSetColumnIndex(2);
  ImGui::TextUnformatted(entry.func.empty() ? "" : entry.func.c_str());
  ImGui::TableSetColumnIndex(3);
  ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(96, 104, 113));
  ImGui::TextWrapped("%s", entry.message.c_str());
  if (!entry.func.empty()) {
    ImGui::TextWrapped("func: %s", entry.func.c_str());
  }
  if (!entry.context.empty()) {
    ImGui::TextWrapped("ctx: %s", entry.context.c_str());
  }
  ImGui::PopStyleColor();
}

void draw_log_row(const LogEntry &entry,
                  int log_index,
                  bool active,
                  UiState *state) {
  const ImU32 bg = row_bg_color(entry, active);
  ImGui::TableNextRow();
  if (bg != 0) {
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);
  }

  const std::string time_text = std::string(active ? "\xE2\x96\xB6 " : "  ") + format_log_time(entry, state->logs.time_mode);
  const auto clickable_text = [&](const char *id, const std::string &text, ImVec4 color = color_rgb(74, 80, 88)) {
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color_rgb(229, 235, 241, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, color_rgb(214, 223, 232, 0.95f));
    const bool clicked = ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return clicked;
  };

  bool clicked = false;
  ImGui::TableSetColumnIndex(0);
  clicked = clickable_text("time", time_text);

  ImGui::TableSetColumnIndex(1);
  clicked = clickable_text("level", level_label(entry), level_text_color(entry, active)) || clicked;

  ImGui::TableSetColumnIndex(2);
  clicked = clickable_text("source", entry.source) || clicked;

  ImGui::TableSetColumnIndex(3);
  clicked = clickable_text("message", entry.message) || clicked;

  if (clicked) {
    set_tracker_to_log(state, entry);
    state->logs.expanded_index = state->logs.expanded_index == log_index ? -1 : log_index;
  }
}

}  // namespace

void draw_logs_tab(AppSession *session, UiState *state) {
  LogsUiState &logs_state = state->logs;
  const RouteData &route_data = session->route_data;

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
  ImGui::SetNextItemWidth(110.0f);
  if (ImGui::BeginCombo("##logs_level", [&]() -> const char * {
        for (const LevelOption &option : kLevelOptions) {
          if (option.value == logs_state.min_level) {
            return option.label;
          }
        }
        return "INFO";
      }())) {
    for (const LevelOption &option : kLevelOptions) {
      const bool selected = option.value == logs_state.min_level;
      if (ImGui::Selectable(option.label, selected)) {
        logs_state.min_level = option.value;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();

  ImGui::SetNextItemWidth(150.0f);
  input_text_with_hint_string("##logs_search", "Search...", &logs_state.search);
  ImGui::SameLine();

  const std::vector<std::string> sources = collect_log_sources(route_data.logs);
  const char *source_label = logs_state.source_filter.empty() ? "All sources" : logs_state.source_filter.c_str();
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::BeginCombo("##logs_source", source_label)) {
    const bool all_selected = logs_state.source_filter.empty();
    if (ImGui::Selectable("All sources", all_selected)) {
      logs_state.source_filter.clear();
    }
    if (all_selected) {
      ImGui::SetItemDefaultFocus();
    }
    for (const std::string &source : sources) {
      const bool selected = source == logs_state.source_filter;
      if (ImGui::Selectable(source.c_str(), selected)) {
        logs_state.source_filter = source;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();

  ImGui::SetNextItemWidth(110.0f);
  if (ImGui::BeginCombo("##logs_time_mode", time_mode_label(logs_state.time_mode))) {
    for (LogTimeMode mode : {LogTimeMode::Route, LogTimeMode::Boot, LogTimeMode::WallClock}) {
      const bool selected = logs_state.time_mode == mode;
      if (ImGui::Selectable(time_mode_label(mode), selected)) {
        logs_state.time_mode = mode;
      }
    }
    ImGui::EndCombo();
  }

  const std::vector<int> filtered_indices = filter_log_indices(route_data, logs_state);
  const bool have_tracker = state->has_tracker_time && !filtered_indices.empty();
  const int active_pos = have_tracker ? find_active_log_position(route_data, filtered_indices, state->tracker_time) : -1;

  ImGui::SameLine();
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 110.0f));
  ImGui::Text("%zu / %zu", filtered_indices.size(), route_data.logs.size());
  ImGui::PopStyleVar();

  if (route_data.logs.empty()) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(116, 124, 133));
    ImGui::TextWrapped("No text logs available for this route.");
    ImGui::PopStyleColor();
    return;
  }

  if (ImGui::BeginChild("##logs_table_child", ImVec2(0.0f, 0.0f), false)) {
    if (have_tracker && std::abs(logs_state.last_auto_scroll_time - state->tracker_time) > 1.0e-6) {
      const float row_height = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
      const float visible_h = std::max(1.0f, ImGui::GetWindowHeight());
      const float target = std::max(0.0f, static_cast<float>(active_pos) * row_height - visible_h * 0.45f);
      ImGui::SetScrollY(target);
      logs_state.last_auto_scroll_time = state->tracker_time;
    }

    if (ImGui::BeginTable("##logs_table",
                          4,
                          ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 120.0f);
      ImGui::TableSetupColumn("Lvl", ImGuiTableColumnFlags_WidthFixed, 58.0f);
      ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 180.0f);
      ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(filtered_indices.size()));
      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
          const int log_index = filtered_indices[static_cast<size_t>(i)];
          const LogEntry &entry = route_data.logs[static_cast<size_t>(log_index)];
          draw_log_row(entry, log_index, i == active_pos, state);
          if (logs_state.expanded_index == log_index) {
            draw_log_expansion_row(entry);
          }
        }
      }

      ImGui::EndTable();
    }
  }
  ImGui::EndChild();
}

}  // namespace jotpluggler
