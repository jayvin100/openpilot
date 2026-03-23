#include "tools/jotpluggler/app_internal.h"

#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

ImVec4 cabana_window_bg() {
  return color_rgb(53, 53, 53);
}

ImVec4 cabana_panel_bg() {
  return color_rgb(60, 63, 65);
}

ImVec4 cabana_panel_alt_bg() {
  return color_rgb(46, 47, 49);
}

ImVec4 cabana_border_color() {
  return color_rgb(77, 77, 77);
}

ImVec4 cabana_accent() {
  return color_rgb(47, 101, 202);
}

ImVec4 cabana_accent_hover() {
  return color_rgb(64, 120, 224);
}

ImVec4 cabana_accent_active() {
  return color_rgb(74, 132, 236);
}

ImVec4 cabana_muted_text() {
  return color_rgb(153, 153, 153);
}

void push_cabana_mode_style() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, cabana_window_bg());
  ImGui::PushStyleColor(ImGuiCol_ChildBg, cabana_panel_bg());
  ImGui::PushStyleColor(ImGuiCol_PopupBg, color_rgb(45, 45, 48));
  ImGui::PushStyleColor(ImGuiCol_Border, cabana_border_color());
  ImGui::PushStyleColor(ImGuiCol_Separator, cabana_border_color());
  ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(187, 187, 187));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, cabana_muted_text());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, color_rgb(41, 41, 43));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color_rgb(50, 53, 58));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color_rgb(58, 61, 66));
  ImGui::PushStyleColor(ImGuiCol_Button, cabana_panel_bg());
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_rgb(74, 78, 82));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, cabana_accent());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, color_rgb(45, 45, 48));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, color_rgb(92, 96, 101));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, color_rgb(112, 118, 126));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, color_rgb(132, 140, 150));
  ImGui::PushStyleColor(ImGuiCol_Header, cabana_accent());
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, cabana_accent_hover());
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, cabana_accent_active());
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, color_rgb(47, 47, 50));
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, cabana_border_color());
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, color_rgb(69, 69, 72));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, color_rgb(65, 68, 71, 0.35f));
}

void pop_cabana_mode_style() {
  ImGui::PopStyleColor(24);
  ImGui::PopStyleVar(8);
}

void draw_cabana_panel_title(const char *title, std::string_view subtitle) {
  app_push_bold_font();
  ImGui::TextUnformatted(title);
  app_pop_bold_font();
  if (!subtitle.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%.*s", static_cast<int>(subtitle.size()), subtitle.data());
  }
  ImGui::Spacing();
}

bool draw_cabana_bottom_tab(const char *id, const char *label, bool active, float width) {
  ImGui::PushStyleColor(ImGuiCol_Button, active ? cabana_window_bg() : cabana_panel_alt_bg());
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? cabana_window_bg() : color_rgb(67, 70, 73));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, cabana_window_bg());
  const bool clicked = ImGui::Button((std::string(label) + id).c_str(), ImVec2(width, 26.0f));
  ImGui::PopStyleColor(3);
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(active ? cabana_border_color() : color_rgb(92, 96, 101)));
  if (active) {
    draw->AddLine(ImVec2(rect.Min.x + 1.0f, rect.Max.y), ImVec2(rect.Max.x - 1.0f, rect.Max.y),
                  ImGui::GetColorU32(cabana_accent()), 2.0f);
  }
  return clicked;
}

void draw_cabana_detail_tab_strip(UiState *state) {
  const float strip_h = 30.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, cabana_panel_alt_bg());
  ImGui::BeginChild("##cabana_detail_bottom_tabs", ImVec2(0.0f, strip_h), false, ImGuiWindowFlags_NoScrollbar);
  const ImVec2 pos = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const ImRect rect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddLine(ImVec2(rect.Min.x, rect.Min.y + 1.0f), ImVec2(rect.Max.x, rect.Min.y + 1.0f),
                ImGui::GetColorU32(cabana_border_color()));
  ImGui::SetCursorPosX(8.0f);
  if (draw_cabana_bottom_tab("##msg", "Msg", state->cabana.detail_tab == 0, 72.0f)) {
    state->cabana.detail_tab = 0;
    state->cabana.detail_top_auto_fit = true;
  }
  ImGui::SameLine(0.0f, 4.0f);
  if (draw_cabana_bottom_tab("##logs", "Logs", state->cabana.detail_tab == 1, 76.0f)) {
    state->cabana.detail_tab = 1;
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void draw_cabana_welcome_panel() {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float center_x = ImGui::GetCursorPosX() + avail.x * 0.5f;
  ImGui::Dummy(ImVec2(0.0f, std::max(28.0f, avail.y * 0.18f)));
  app_push_bold_font();
  const char *title = "CABANA";
  const float title_w = ImGui::CalcTextSize(title).x;
  ImGui::SetCursorPosX(std::max(0.0f, center_x - title_w * 0.5f));
  ImGui::TextUnformatted(title);
  app_pop_bold_font();
  ImGui::Spacing();
  const char *hint = "<-Select a message to view details";
  const float hint_w = ImGui::CalcTextSize(hint).x;
  ImGui::SetCursorPosX(std::max(0.0f, center_x - hint_w * 0.5f));
  ImGui::TextDisabled("%s", hint);
}

namespace {

void draw_splitter_line(const ImRect &rect, bool hovered) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 color = hovered ? IM_COL32(112, 128, 144, 255) : IM_COL32(194, 198, 204, 255);
  if (rect.GetWidth() > rect.GetHeight()) {
    const float y = (rect.Min.y + rect.Max.y) * 0.5f;
    draw_list->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), color, hovered ? 2.0f : 1.0f);
  } else {
    const float x = (rect.Min.x + rect.Max.x) * 0.5f;
    draw_list->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), color, hovered ? 2.0f : 1.0f);
  }
}

}  // namespace

void draw_vertical_splitter(const char *id,
                            float height,
                            float min_left,
                            float max_left,
                            float *left_width) {
  const ImVec2 size(4.0f, height);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  if (ImGui::IsItemActive()) {
    *left_width = std::clamp(*left_width + ImGui::GetIO().MouseDelta.x, min_left, max_left);
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
}

void draw_right_splitter(const char *id,
                         float height,
                         float min_right,
                         float max_right,
                         float *right_width) {
  const ImVec2 size(4.0f, height);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  if (ImGui::IsItemActive()) {
    *right_width = std::clamp(*right_width - ImGui::GetIO().MouseDelta.x, min_right, max_right);
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
}

bool draw_horizontal_splitter(const char *id,
                              float width,
                              float min_top,
                              float max_top,
                              float *top_height) {
  const ImVec2 size(width, 4.0f);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
  bool changed = false;
  if (ImGui::IsItemActive()) {
    *top_height = std::clamp(*top_height + ImGui::GetIO().MouseDelta.y, min_top, max_top);
    changed = true;
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
  return changed;
}

void draw_payload_bytes(std::string_view data, const std::string *prev_data) {
  app_push_mono_font();
  for (size_t i = 0; i < data.size(); ++i) {
    if (i > 0) ImGui::SameLine(0.0f, 6.0f);
    const bool changed = prev_data != nullptr
                      && i < prev_data->size()
                      && static_cast<unsigned char>((*prev_data)[i]) != static_cast<unsigned char>(data[i]);
    if (changed) ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(199, 74, 59));
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned char>(data[i]));
    ImGui::TextUnformatted(hex);
    if (changed) ImGui::PopStyleColor();
  }
  app_pop_mono_font();
}

void draw_payload_preview_boxes(const char *id, std::string_view data, const std::string *prev_data, float max_width) {
  constexpr float kByteW = 17.0f;
  constexpr float kByteH = 16.0f;
  constexpr float kGap = 2.0f;
  const size_t capacity = std::max<size_t>(1, static_cast<size_t>((max_width + kGap) / (kByteW + kGap)));
  const size_t visible = std::min(data.size(), capacity);
  const bool truncated = visible < data.size();
  const float ellipsis_w = truncated ? 10.0f : 0.0f;
  const float width = std::max(18.0f, visible * (kByteW + kGap) - (visible > 0 ? kGap : 0.0f) + ellipsis_w);
  ImGui::InvisibleButton(id, ImVec2(width, kByteH));
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  app_push_mono_font();
  for (size_t i = 0; i < visible; ++i) {
    const unsigned char after = static_cast<unsigned char>(data[i]);
    const bool has_prev = prev_data != nullptr && i < prev_data->size();
    const unsigned char before = has_prev ? static_cast<unsigned char>((*prev_data)[i]) : after;
    ImU32 fill = ImGui::GetColorU32(color_rgb(67, 70, 74));
    if (has_prev && after != before) {
      fill = ImGui::GetColorU32(after > before ? color_rgb(72, 95, 140) : color_rgb(120, 72, 68));
    }
    const float x0 = rect.Min.x + static_cast<float>(i) * (kByteW + kGap);
    const ImRect box(ImVec2(x0, rect.Min.y), ImVec2(x0 + kByteW, rect.Min.y + kByteH));
    draw->AddRectFilled(box.Min, box.Max, fill, 2.0f);
    draw->AddRect(box.Min, box.Max, ImGui::GetColorU32(color_rgb(105, 110, 116)), 2.0f);
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", after);
    const ImVec2 text_size = ImGui::CalcTextSize(hex);
    draw->AddText(ImGui::GetFont(),
                  ImGui::GetFontSize(),
                  ImVec2(box.Min.x + (box.GetWidth() - text_size.x) * 0.5f,
                         box.Min.y + (box.GetHeight() - text_size.y) * 0.5f - 1.0f),
                  ImGui::GetColorU32(color_rgb(228, 231, 236)),
                  hex);
  }
  if (truncated) {
    draw->AddText(ImVec2(rect.Max.x - 9.0f, rect.Min.y - 1.0f),
                  ImGui::GetColorU32(color_rgb(154, 160, 168)),
                  "...");
  }
  app_pop_mono_font();
}

void draw_signal_overlay_legend(const std::vector<std::pair<const CabanaSignalSummary *, ImU32>> &highlighted) {
  if (highlighted.empty()) {
    return;
  }
  app_push_bold_font();
  ImGui::TextUnformatted("Signals");
  app_pop_bold_font();
  for (size_t i = 0; i < highlighted.size(); ++i) {
    if (i > 0) ImGui::SameLine(0.0f, 12.0f);
    ImGui::ColorButton(("##cabana_signal_color_" + std::to_string(i)).c_str(),
                       ImGui::ColorConvertU32ToFloat4(highlighted[i].second),
                       ImGuiColorEditFlags_NoTooltip,
                       ImVec2(10.0f, 10.0f));
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted(highlighted[i].first->name.c_str());
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextDisabled("[%d|%d]", highlighted[i].first->start_bit, highlighted[i].first->size);
  }
  ImGui::Spacing();
}

ImU32 mix_color(ImU32 a, ImU32 b, float t) {
  const ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
  const ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::GetColorU32(ImVec4(av.x + (bv.x - av.x) * t,
                                   av.y + (bv.y - av.y) * t,
                                   av.z + (bv.z - av.z) * t,
                                   av.w + (bv.w - av.w) * t));
}

void draw_empty_panel(const char *title, const char *message) {
  draw_cabana_panel_title(title);
  ImGui::TextDisabled("%s", message);
}

void draw_cabana_toolbar_button(const char *label, bool enabled, const std::function<void()> &on_click) {
  ImGui::BeginDisabled(!enabled);
  if (ImGui::Button(label)) {
    on_click();
  }
  ImGui::EndDisabled();
}

void draw_cabana_warning_banner(const std::vector<std::string> &warnings) {
  if (warnings.empty()) {
    return;
  }
  const float height = 28.0f + std::max(0.0f, (static_cast<float>(warnings.size()) - 1.0f) * 16.0f);
  ImGui::InvisibleButton("##cabana_warning_banner", ImVec2(ImGui::GetContentRegionAvail().x, height));
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(color_rgb(251, 245, 229)), 4.0f);
  draw->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(color_rgb(221, 191, 121)), 4.0f, 0, 1.0f);
  draw->AddText(ImVec2(rect.Min.x + 10.0f, rect.Min.y + 6.0f),
                ImGui::GetColorU32(color_rgb(164, 106, 28)),
                "!");
  float y = rect.Min.y + 5.0f;
  for (const std::string &warning : warnings) {
    draw->AddText(ImVec2(rect.Min.x + 24.0f, y),
                  ImGui::GetColorU32(color_rgb(109, 82, 34)),
                  warning.c_str());
    y += 16.0f;
  }
}

void draw_signal_sparkline(const AppSession &session,
                           const UiState &state,
                           std::string_view signal_path,
                           bool selected) {
  const RouteSeries *series = app_find_route_series(session, std::string(signal_path));
  const float width = std::max(96.0f, ImGui::GetColumnWidth() - 12.0f);
  const ImVec2 size(width, 24.0f);
  if (series == nullptr || series->times.size() < 2 || series->times.size() != series->values.size()) {
    ImGui::Dummy(size);
    return;
  }

  const std::string id = "##spark_" + std::string(signal_path);
  ImGui::InvisibleButton(id.c_str(), size);
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImU32 bg = ImGui::GetColorU32(selected ? color_rgb(59, 74, 103) : color_rgb(52, 54, 57));
  const ImU32 border = ImGui::GetColorU32(selected ? color_rgb(110, 145, 214) : color_rgb(93, 98, 104));
  const ImU32 line = ImGui::GetColorU32(selected ? color_rgb(109, 163, 255) : color_rgb(162, 170, 182));
  const ImU32 tracker = ImGui::GetColorU32(color_rgb(214, 93, 64));
  draw->AddRectFilled(rect.Min, rect.Max, bg, 4.0f);
  draw->AddRect(rect.Min, rect.Max, border, 4.0f);

  double x_min = series->times.front();
  double x_max = series->times.back();
  if (state.has_shared_range) {
    const double overlap_min = std::max(x_min, state.x_view_min);
    const double overlap_max = std::min(x_max, state.x_view_max);
    if (overlap_max > overlap_min) {
      x_min = overlap_min;
      x_max = overlap_max;
    }
  }
  if (x_max <= x_min) {
    return;
  }

  constexpr int kSamples = 40;
  std::array<double, kSamples> sampled = {};
  std::array<bool, kSamples> valid = {};
  bool found = false;
  double y_min = 0.0;
  double y_max = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    const double t = x_min + (x_max - x_min) * static_cast<double>(i) / static_cast<double>(kSamples - 1);
    const std::optional<double> value = app_sample_xy_value_at_time(series->times, series->values, false, t);
    if (!value.has_value() || !std::isfinite(*value)) continue;
    sampled[static_cast<size_t>(i)] = *value;
    valid[static_cast<size_t>(i)] = true;
    if (!found) {
      y_min = y_max = *value;
      found = true;
    } else {
      y_min = std::min(y_min, *value);
      y_max = std::max(y_max, *value);
    }
  }
  if (!found) {
    return;
  }
  if (y_max <= y_min) {
    const double pad = std::max(0.1, std::abs(y_min) * 0.1);
    y_min -= pad;
    y_max += pad;
  } else {
    const double pad = (y_max - y_min) * 0.12;
    y_min -= pad;
    y_max += pad;
  }

  const float left = rect.Min.x + 4.0f;
  const float right = rect.Max.x - 4.0f;
  const float top = rect.Min.y + 4.0f;
  const float bottom = rect.Max.y - 4.0f;
  std::array<ImVec2, kSamples> points = {};
  int point_count = 0;
  for (int i = 0; i < kSamples; ++i) {
    if (!valid[static_cast<size_t>(i)]) {
      if (point_count > 1) draw->AddPolyline(points.data(), point_count, line, 0, selected ? 2.0f : 1.5f);
      point_count = 0;
      continue;
    }
    const float x = left + (right - left) * static_cast<float>(i) / static_cast<float>(kSamples - 1);
    const float frac = static_cast<float>((sampled[static_cast<size_t>(i)] - y_min) / (y_max - y_min));
    const float y = bottom - (bottom - top) * std::clamp(frac, 0.0f, 1.0f);
    points[static_cast<size_t>(point_count++)] = ImVec2(x, y);
  }
  if (point_count > 1) draw->AddPolyline(points.data(), point_count, line, 0, selected ? 2.0f : 1.5f);

  if (state.has_tracker_time && state.tracker_time >= x_min && state.tracker_time <= x_max) {
    const float marker_x = left + (right - left) * static_cast<float>((state.tracker_time - x_min) / (x_max - x_min));
    draw->AddLine(ImVec2(marker_x, top), ImVec2(marker_x, bottom), tracker, 1.5f);
  }
}
