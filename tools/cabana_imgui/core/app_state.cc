#include "core/app_state.h"

#include <algorithm>

namespace cabana {

namespace {

bool signal_matches(const ChartSignalRef &signal, const MessageId &msg_id, const std::string &signal_name) {
  return signal.msg_id == msg_id && signal.signal_name == signal_name;
}

}  // namespace

void AppState::markSettingsDirty() {
  settings_dirty = true;
}

ChartTabState &AppState::ensureChartTab() {
  if (chart_tabs.empty()) {
    chart_tabs.push_back({.id = next_chart_tab_id++});
    current_chart_tab = 0;
  }
  current_chart_tab = std::clamp(current_chart_tab, 0, (int)chart_tabs.size() - 1);
  if (selected_chart >= (int)chart_tabs[current_chart_tab].charts.size()) {
    selected_chart = chart_tabs[current_chart_tab].charts.empty() ? -1 : 0;
  }
  return chart_tabs[current_chart_tab];
}

ChartTabState *AppState::activeChartTab() {
  if (chart_tabs.empty()) return nullptr;
  current_chart_tab = std::clamp(current_chart_tab, 0, (int)chart_tabs.size() - 1);
  return &chart_tabs[current_chart_tab];
}

const ChartTabState *AppState::activeChartTab() const {
  if (chart_tabs.empty()) return nullptr;
  int idx = std::clamp(current_chart_tab, 0, (int)chart_tabs.size() - 1);
  return &chart_tabs[idx];
}

void AppState::selectChartTab(int index) {
  ensureChartTab();
  const int next_index = std::clamp(index, 0, (int)chart_tabs.size() - 1);
  if (next_index != current_chart_tab) {
    current_chart_tab = next_index;
    markSettingsDirty();
  }
  auto *tab = activeChartTab();
  if (!tab || tab->charts.empty()) {
    selected_chart = -1;
  } else if (selected_chart < 0 || selected_chart >= (int)tab->charts.size()) {
    selected_chart = 0;
  }
}

void AppState::newChartTab() {
  chart_tabs.push_back({.id = next_chart_tab_id++});
  current_chart_tab = (int)chart_tabs.size() - 1;
  selected_chart = -1;
  markSettingsDirty();
}

void AppState::removeChartTab(int index) {
  ensureChartTab();
  if (chart_tabs.size() == 1) {
    clearCharts();
    return;
  }
  index = std::clamp(index, 0, (int)chart_tabs.size() - 1);
  chart_tabs.erase(chart_tabs.begin() + index);
  current_chart_tab = std::clamp(current_chart_tab, 0, (int)chart_tabs.size() - 1);
  auto *tab = activeChartTab();
  selected_chart = (!tab || tab->charts.empty()) ? -1 : std::clamp(selected_chart, 0, (int)tab->charts.size() - 1);
  markSettingsDirty();
}

void AppState::clearCharts() {
  ensureChartTab();
  chart_tabs.resize(1);
  chart_tabs[0].charts.clear();
  current_chart_tab = 0;
  selected_chart = -1;
  markSettingsDirty();
}

void AppState::addEmptyChart() {
  auto &tab = ensureChartTab();
  tab.charts.emplace_back();
  selected_chart = (int)tab.charts.size() - 1;
  markSettingsDirty();
}

void AppState::removeChart(int chart_idx) {
  auto &tab = ensureChartTab();
  if (chart_idx < 0 || chart_idx >= (int)tab.charts.size()) return;
  tab.charts.erase(tab.charts.begin() + chart_idx);
  if (tab.charts.empty()) {
    selected_chart = -1;
  } else {
    selected_chart = std::clamp(selected_chart, 0, (int)tab.charts.size() - 1);
  }
  markSettingsDirty();
}

void AppState::splitChart(int chart_idx) {
  auto &tab = ensureChartTab();
  if (chart_idx < 0 || chart_idx >= (int)tab.charts.size()) return;
  auto &chart = tab.charts[chart_idx];
  if (chart.signals.size() <= 1) return;

  std::vector<ChartDefinition> splits;
  splits.reserve(chart.signals.size() - 1);
  for (auto it = chart.signals.begin() + 1; it != chart.signals.end(); ++it) {
    splits.push_back({.signals = {*it}});
  }
  chart.signals.erase(chart.signals.begin() + 1, chart.signals.end());
  tab.charts.insert(tab.charts.begin() + chart_idx + 1, splits.begin(), splits.end());
  selected_chart = chart_idx;
  markSettingsDirty();
}

void AppState::addSignalToCharts(const MessageId &msg_id, const std::string &signal_name, bool merge) {
  if (hasChartSignal(msg_id, signal_name)) return;

  auto &tab = ensureChartTab();
  int target_chart = -1;
  if (merge) {
    if (selected_chart >= 0 && selected_chart < (int)tab.charts.size()) {
      target_chart = selected_chart;
    } else if (!tab.charts.empty()) {
      target_chart = 0;
    }
  }

  if (target_chart == -1) {
    tab.charts.emplace_back();
    target_chart = (int)tab.charts.size() - 1;
  }

  tab.charts[target_chart].signals.push_back({.msg_id = msg_id, .signal_name = signal_name});
  selected_chart = target_chart;
  markSettingsDirty();
}

void AppState::removeSignalFromCharts(const MessageId &msg_id, const std::string &signal_name) {
  ensureChartTab();
  for (auto &tab : chart_tabs) {
    for (auto chart_it = tab.charts.begin(); chart_it != tab.charts.end(); ) {
      auto &signals = chart_it->signals;
      signals.erase(std::remove_if(signals.begin(), signals.end(), [&](const auto &signal) {
        return signal_matches(signal, msg_id, signal_name);
      }), signals.end());
      if (signals.empty()) {
        chart_it = tab.charts.erase(chart_it);
      } else {
        ++chart_it;
      }
    }
  }

  auto *tab = activeChartTab();
  if (!tab || tab->charts.empty()) {
    selected_chart = -1;
  } else {
    selected_chart = std::clamp(selected_chart, 0, (int)tab->charts.size() - 1);
  }
  markSettingsDirty();
}

bool AppState::hasChartSignal(const MessageId &msg_id, const std::string &signal_name) const {
  for (const auto &tab : chart_tabs) {
    for (const auto &chart : tab.charts) {
      if (std::any_of(chart.signals.begin(), chart.signals.end(), [&](const auto &signal) {
            return signal_matches(signal, msg_id, signal_name);
          })) {
        return true;
      }
    }
  }
  return false;
}

int AppState::totalChartCount() const {
  int total = 0;
  for (const auto &tab : chart_tabs) {
    total += tab.charts.size();
  }
  return total;
}

AppState &app_state() {
  static AppState s;
  return s;
}

}  // namespace cabana
