#include "tools/imgui_cabana/chart/chartswidget.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "imgui.h"

namespace imgui_cabana {

std::vector<std::string> plottedSignalIds(const std::vector<MessageData> &messages) {
  std::vector<std::string> ids;
  for (const auto &message : messages) {
    for (const auto &signal : message.signals) {
      if (signal.plotted) ids.push_back(message.messageId() + "/" + signal.name);
    }
  }
  return ids;
}

namespace {

struct PlottedSignalRef {
  const MessageData *message = nullptr;
  const SignalData *signal = nullptr;
};

std::vector<PlottedSignalRef> plottedSignals(const std::vector<MessageData> &messages) {
  std::vector<PlottedSignalRef> refs;
  for (const auto &message : messages) {
    for (const auto &signal : message.signals) {
      if (signal.plotted) refs.push_back({&message, &signal});
    }
  }
  return refs;
}

}  // namespace

void drawChartsPane(ChartsWidgetModel &model, const ChartsWidgetCallbacks &callbacks) {
  ImGui::BeginChild("ChartsToolbar", ImVec2(0.0f, 33.0f), false);
  callbacks.widget.capture_window_rect("ChartsToolbar", "QWidget");
  ImGui::SetCursorPos(ImVec2(6.0f, 6.0f));
  ImGui::TextUnformatted("Charts");
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 168.0f);
  if (ImGui::Button("Reset Zoom", ImVec2(78.0f, 22.0f))) model.time_range->reset();
  callbacks.widget.capture_item("ChartsResetZoomButton", "QToolButton", "Reset Zoom", std::nullopt);
  ImGui::SameLine();
  if (ImGui::Button("Remove All", ImVec2(84.0f, 22.0f))) callbacks.clear_all_charts();
  callbacks.widget.capture_item("ChartsRemoveAllButton", "QToolButton", "Remove All", std::nullopt);
  ImGui::EndChild();

  const auto plotted = plottedSignals(*model.messages);
  const float chart_height = 200.0f;
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  const ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, chart_height);
  ImGui::InvisibleButton("##ChartCanvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
  model.chart_rects->push_back({canvas_pos.x, canvas_pos.y, canvas_size.x, canvas_size.y});

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(255, 255, 255, 255));
  draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(188, 188, 188, 255));

  if (ImGui::IsItemActivated()) *model.drag_start_x = ImGui::GetIO().MousePos.x;
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const float end_x = ImGui::GetIO().MousePos.x;
    if (std::fabs(end_x - *model.drag_start_x) > 8.0f) {
      const float min_x = std::min(*model.drag_start_x, end_x);
      const float max_x = std::max(*model.drag_start_x, end_x);
      const float start_norm = std::clamp((min_x - canvas_pos.x) / std::max(1.0f, canvas_size.x), 0.0f, 1.0f);
      const float end_norm = std::clamp((max_x - canvas_pos.x) / std::max(1.0f, canvas_size.x), 0.0f, 1.0f);
      *model.time_range = {*model.min_sec + start_norm * (*model.max_sec - *model.min_sec),
                           *model.min_sec + end_norm * (*model.max_sec - *model.min_sec)};
    }
  }

  for (int i = 0; i <= 4; ++i) {
    float y = canvas_pos.y + (canvas_size.y / 4.0f) * i;
    draw_list->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_pos.x + canvas_size.x, y), IM_COL32(236, 236, 236, 255));
  }
  for (int i = 0; i <= 6; ++i) {
    float x = canvas_pos.x + (canvas_size.x / 6.0f) * i;
    draw_list->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y), IM_COL32(242, 242, 242, 255));
  }

  if (plotted.empty()) {
    draw_list->AddText(ImVec2(canvas_pos.x + 16.0f, canvas_pos.y + 16.0f), IM_COL32(120, 120, 120, 255), "Plot a signal to start charting.");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    return;
  }

  const auto display_range = model.time_range->value_or(std::make_pair(*model.min_sec, *model.max_sec));
  const double display_min = display_range.first;
  const double display_max = std::max(display_min + 0.001, display_range.second);
  const double display_span = display_max - display_min;
  const std::array<ImU32, 3> colors = {IM_COL32(47, 101, 202, 255), IM_COL32(183, 62, 62, 255), IM_COL32(30, 145, 90, 255)};
  for (size_t s = 0; s < plotted.size(); ++s) {
    const auto &message = *plotted[s].message;
    const auto &signal = *plotted[s].signal;
    if (signal.byte_index < 0) continue;

    std::vector<ImVec2> points;
    const std::size_t target_points = std::max<std::size_t>(2, static_cast<std::size_t>(canvas_size.x / 3.0f));
    const std::size_t step = std::max<std::size_t>(1, message.samples.size() / target_points);
    for (std::size_t i = 0; i < message.samples.size(); i += step) {
      const auto &sample = message.samples[i];
      if (sample.sec < display_min || sample.sec > display_max) continue;
      if (signal.byte_index >= static_cast<int>(sample.bytes.size())) continue;
      const float x_norm = static_cast<float>((sample.sec - display_min) / display_span);
      const float y_norm = static_cast<float>(sample.bytes[signal.byte_index] / 255.0);
      points.push_back(ImVec2(canvas_pos.x + x_norm * canvas_size.x, canvas_pos.y + (1.0f - y_norm) * canvas_size.y));
    }
    if (points.empty()) {
      const auto &sample = message.samples.back();
      if (signal.byte_index < static_cast<int>(sample.bytes.size())) {
        const float y_norm = static_cast<float>(sample.bytes[signal.byte_index] / 255.0);
        points.push_back(ImVec2(canvas_pos.x, canvas_pos.y + (1.0f - y_norm) * canvas_size.y));
        points.push_back(ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + (1.0f - y_norm) * canvas_size.y));
      }
    }
    if (points.size() >= 2) {
      draw_list->AddPolyline(points.data(), static_cast<int>(points.size()), colors[s % colors.size()], 0, 2.0f);
    }
    draw_list->AddText(ImVec2(canvas_pos.x + 10.0f, canvas_pos.y + 10.0f + 16.0f * static_cast<float>(s)),
                       colors[s % colors.size()], signal.name.c_str());
  }

  const float marker_x = canvas_pos.x + static_cast<float>(std::clamp((*model.current_sec - display_min) / display_span, 0.0, 1.0)) * canvas_size.x;
  draw_list->AddLine(ImVec2(marker_x, canvas_pos.y), ImVec2(marker_x, canvas_pos.y + canvas_size.y), IM_COL32(35, 35, 35, 255), 2.0f);
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

}  // namespace imgui_cabana
