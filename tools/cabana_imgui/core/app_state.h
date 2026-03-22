#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "core/types.h"

namespace cabana {

struct ChartSignalRef {
  MessageId msg_id;
  std::string signal_name;
};

struct ChartDefinition {
  std::vector<ChartSignalRef> signals;
};

struct ChartTabState {
  int id = 0;
  std::vector<ChartDefinition> charts;
};

struct AppState {
  std::atomic<bool> quit_requested{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> segments_merged{false};

  // Route info (set after load)
  std::string route_name;
  std::string car_fingerprint;
  bool route_loading = false;
  std::string route_load_error;
  double current_sec = 0;
  double min_sec = 0;
  double max_sec = 0;
  float speed = 1.0f;
  int cached_minutes = 30;
  int fps = 10;

  // Selection state
  bool has_selection = false;
  MessageId selected_msg;
  bool reset_layout_requested = false;
  bool show_help_overlay = false;
  bool settings_dirty = false;

  // Chart state
  float chart_range_sec = 7.0f;
  int current_chart_tab = 0;
  int selected_chart = -1;
  int next_chart_tab_id = 1;
  std::vector<ChartTabState> chart_tabs;

  ChartTabState &ensureChartTab();
  ChartTabState *activeChartTab();
  const ChartTabState *activeChartTab() const;
  void selectChartTab(int index);
  void newChartTab();
  void removeChartTab(int index);
  void clearCharts();
  void addEmptyChart();
  void removeChart(int chart_idx);
  void splitChart(int chart_idx);
  void addSignalToCharts(const MessageId &msg_id, const std::string &signal_name, bool merge);
  void removeSignalFromCharts(const MessageId &msg_id, const std::string &signal_name);
  bool hasChartSignal(const MessageId &msg_id, const std::string &signal_name) const;
  int totalChartCount() const;
  void markSettingsDirty();
};

AppState &app_state();

}  // namespace cabana
