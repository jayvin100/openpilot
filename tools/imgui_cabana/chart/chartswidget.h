#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "tools/imgui_cabana/messageswidget.h"

namespace imgui_cabana {

struct ChartsWidgetModel {
  std::vector<MessageData> *messages = nullptr;
  double *current_sec = nullptr;
  double *min_sec = nullptr;
  double *max_sec = nullptr;
  std::optional<std::pair<double, double>> *time_range = nullptr;
  float *drag_start_x = nullptr;
  std::vector<Rect> *chart_rects = nullptr;
};

struct ChartsWidgetCallbacks {
  WidgetCallbacks widget;
  std::function<void()> clear_all_charts;
};

std::vector<std::string> plottedSignalIds(const std::vector<MessageData> &messages);
void drawChartsPane(ChartsWidgetModel &model, const ChartsWidgetCallbacks &callbacks);

}  // namespace imgui_cabana
