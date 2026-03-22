#pragma once

#include <atomic>
#include <map>
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

enum class DetailTab {
  Binary = 0,
  Signals,
  History,
};

struct AppState {
  struct EditSnapshot {
    bool has_selection = false;
    MessageId selected_msg;
    bool has_bit_selection = false;
    int bit_selection_anchor = 0;
    int bit_selection_start = 0;
    int bit_selection_size = 0;
    bool bit_selection_little_endian = true;
    DetailTab current_detail_tab = DetailTab::Binary;
    float chart_range_sec = 7.0f;
    int current_chart_tab = 0;
    int selected_chart = -1;
    int next_chart_tab_id = 1;
    std::vector<ChartTabState> chart_tabs;
  };

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
  bool has_bit_selection = false;
  int bit_selection_anchor = 0;
  int bit_selection_start = 0;
  int bit_selection_size = 0;
  bool bit_selection_little_endian = true;
  DetailTab current_detail_tab = DetailTab::Binary;
  bool reset_layout_requested = false;
  bool show_help_overlay = false;
  bool settings_dirty = false;

  // Persisted recent state
  std::string active_dbc_file;
  std::map<int, std::string> active_dbc_files;
  std::vector<std::string> recent_dbc_files;
  std::vector<std::string> recent_routes;

  // Chart state
  float chart_range_sec = 7.0f;
  int current_chart_tab = 0;
  int selected_chart = -1;
  int next_chart_tab_id = 1;
  std::vector<ChartTabState> chart_tabs;

  void setSelectedMessage(const MessageId &msg_id);
  void clearSelection();
  void setBitSelection(int start_bit, int size, bool little_endian);
  void extendBitSelection(int end_bit);
  void clearBitSelection();
  void setCurrentDetailTab(DetailTab tab);
  void rememberRecentDbc(const std::string &path);
  void setDbcAssignments(const SourceSet &sources, const std::string &path);
  void clearDbcAssignments(const SourceSet &sources);
  void clearAllDbcAssignments();
  void clearDbcFileAssignments(const std::string &path);
  std::string dbcPathForSource(int source) const;
  void rememberRecentRoute(const std::string &route);
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
  void renameChartSignal(const MessageId &msg_id, const std::string &old_name, const std::string &new_name);
  void removeChartsForMessage(const MessageId &msg_id);
  bool hasChartSignal(const MessageId &msg_id, const std::string &signal_name) const;
  int totalChartCount() const;
  EditSnapshot captureEditSnapshot() const;
  void restoreEditSnapshot(const EditSnapshot &snapshot);
  void markSettingsDirty();
};

AppState &app_state();

}  // namespace cabana
