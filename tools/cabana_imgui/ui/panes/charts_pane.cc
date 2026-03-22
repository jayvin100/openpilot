#include "ui/panes/charts_pane.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "app/application.h"
#include "core/app_state.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"
#include "ui/bootstrap_icons.h"

namespace cabana {
namespace panes {

namespace {

constexpr int kMaxPlotPoints = 1500;

static bool icon_button(const char *id, std::string_view icon_id) {
  const char *g = cabana::icons::glyph(icon_id);
  if (g[0] == '\0') return ImGui::Button(id);
  std::string label = std::string(g) + "##" + id;
  return ImGui::Button(label.c_str());
}

static const cabana::dbc::Signal *find_signal(const ChartSignalRef &ref) {
  const auto *msg = cabana::dbc::dbc_manager().msg(ref.msg_id.address);
  if (!msg) return nullptr;
  auto it = std::find_if(msg->signals.begin(), msg->signals.end(), [&](const auto &sig) {
    return sig.name == ref.signal_name;
  });
  return it == msg->signals.end() ? nullptr : &(*it);
}

static std::string chart_title(const ChartDefinition &chart) {
  if (chart.signals.empty()) {
    return "Empty Chart";
  }
  if (chart.signals.size() == 1) {
    return chart.signals.front().signal_name;
  }
  return chart.signals.front().signal_name + " +" + std::to_string(chart.signals.size() - 1);
}

static void collect_signal_points(const std::vector<const CanEvent *> &events,
                                  const cabana::dbc::Signal &sig, uint64_t route_start_nanos,
                                  double min_sec, double max_sec,
                                  std::vector<double> &xs, std::vector<double> &ys) {
  xs.clear();
  ys.clear();
  if (events.empty()) return;

  const uint64_t min_mono = route_start_nanos + std::max(0.0, min_sec) * 1e9;
  const uint64_t max_mono = route_start_nanos + std::max(0.0, max_sec) * 1e9;
  auto first = std::lower_bound(events.begin(), events.end(), min_mono,
                                [](const CanEvent *e, uint64_t mono) { return e->mono_time < mono; });
  auto last = std::upper_bound(first, events.end(), max_mono,
                               [](uint64_t mono, const CanEvent *e) { return mono < e->mono_time; });
  if (first == last) return;

  const size_t total_points = std::distance(first, last);
  const size_t stride = std::max<size_t>(1, total_points / kMaxPlotPoints);
  xs.reserve(total_points / stride + 1);
  ys.reserve(total_points / stride + 1);

  for (auto it = first; it < last; it += stride) {
    const auto *e = *it;
    xs.push_back((e->mono_time - route_start_nanos) / 1e9);
    ys.push_back(sig.getValue(e->dat, e->size));
  }

  const auto *tail = *(last - 1);
  const double tail_x = (tail->mono_time - route_start_nanos) / 1e9;
  if (xs.empty() || xs.back() != tail_x) {
    xs.push_back(tail_x);
    ys.push_back(sig.getValue(tail->dat, tail->size));
  }
}

static void render_chart_series_controls(const ChartDefinition &chart, int chart_idx,
                                         ChartSignalRef *pending_remove_signal) {
  for (int i = 0; i < (int)chart.signals.size(); ++i) {
    if (i > 0) ImGui::SameLine();
    const auto &signal = chart.signals[i];
    std::string label = signal.signal_name + " x##chart_" + std::to_string(chart_idx) + "_sig_" + std::to_string(i);
    if (ImGui::SmallButton(label.c_str())) {
      *pending_remove_signal = signal;
    }
  }
}

}  // namespace

void charts() {
  ImGui::Begin("Charts");

  auto &st = cabana::app_state();
  auto *src = app() ? app()->source() : nullptr;
  auto &initial_tab = st.ensureChartTab();
  (void)initial_tab;

  if (icon_button("new_chart", "file-plus")) {
    st.addEmptyChart();
  }
  ImGui::SameLine();
  if (icon_button("new_tab", "window-stack")) {
    st.newChartTab();
  }
  ImGui::SameLine();
  ImGui::Text("Charts: %d", st.totalChartCount());
  ImGui::SameLine();
  ImGui::TextDisabled("Type: Line");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##range", &st.chart_range_sec, 1.0f, 60.0f, "%.0f s");

  ImGui::SameLine();
  ImGui::BeginDisabled(st.selected_chart < 0);
  if (icon_button("split_chart", "layout-split")) {
    st.splitChart(st.selected_chart);
  }
  ImGui::SameLine();
  if (icon_button("remove_chart", "x-lg")) {
    st.removeChart(st.selected_chart);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(st.totalChartCount() == 0);
  if (icon_button("remove_all", "x-square")) {
    st.clearCharts();
  }
  ImGui::EndDisabled();

  int active_tab_index = st.current_chart_tab;
  int remove_tab_index = -1;
  if (ImGui::BeginTabBar("##chart_tabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
    for (int i = 0; i < (int)st.chart_tabs.size(); ++i) {
      auto &tab = st.chart_tabs[i];
      std::string label = "Tab " + std::to_string(i + 1) + " (" + std::to_string(tab.charts.size()) + ")";
      bool open = true;
      bool *open_ptr = st.chart_tabs.size() > 1 ? &open : nullptr;
      if (ImGui::BeginTabItem(label.c_str(), open_ptr)) {
        active_tab_index = i;
        ImGui::EndTabItem();
      }
      if (!open) {
        remove_tab_index = i;
      }
    }
    ImGui::EndTabBar();
  }

  if (remove_tab_index != -1) {
    st.removeChartTab(remove_tab_index);
  }
  st.selectChartTab(active_tab_index);

  const auto *tab = st.activeChartTab();
  if (!tab || tab->charts.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("No charts in this tab. Use the signal plot toggles or create an empty chart and Shift-click a signal.");
    ImGui::End();
    return;
  }

  const double end_sec = std::clamp(st.current_sec > 0.0 ? st.current_sec : st.max_sec, st.min_sec, st.max_sec);
  const double start_sec = std::max(st.min_sec, end_sec - st.chart_range_sec);

  int pending_split = -1;
  int pending_remove_chart = -1;
  ChartSignalRef pending_remove_signal{};
  bool has_pending_remove_signal = false;

  std::vector<double> xs;
  std::vector<double> ys;

  ImGui::Separator();
  ImGui::BeginChild("##chart_scroll");
  for (int chart_idx = 0; chart_idx < (int)tab->charts.size(); ++chart_idx) {
    const auto &chart = tab->charts[chart_idx];
    const bool selected = chart_idx == st.selected_chart;

    ImGui::PushID(chart_idx);
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.80f, 0.55f, 0.25f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }

    ImGui::BeginChild("##chart_card", ImVec2(0, 240), true);
    if (selected) {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
    }

    if (ImGui::Selectable(chart_title(chart).c_str(), selected, 0, ImVec2(-1, 0))) {
      st.selected_chart = chart_idx;
    }

    if (!chart.signals.empty()) {
      render_chart_series_controls(chart, chart_idx, &pending_remove_signal);
      has_pending_remove_signal = has_pending_remove_signal || !pending_remove_signal.signal_name.empty();
    }

    ImGui::SameLine();
    if (chart.signals.size() > 1 && ImGui::SmallButton("Split")) {
      pending_split = chart_idx;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      pending_remove_chart = chart_idx;
    }

    ImGui::Separator();

    if (!src) {
      ImGui::TextDisabled(st.route_loading ? "Route is still loading..." : "No stream loaded.");
    } else if (chart.signals.empty()) {
      ImGui::TextDisabled("Empty chart. Shift-click a signal to add it here.");
    } else if (ImPlot::BeginPlot("##plot", ImVec2(-1, 180), ImPlotFlags_NoLegend)) {
      ImPlot::SetupAxes("Time (s)", "Value", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxisLimits(ImAxis_X1, start_sec, end_sec, ImPlotCond_Always);

      for (int i = 0; i < (int)chart.signals.size(); ++i) {
        const auto &signal_ref = chart.signals[i];
        const auto *sig = find_signal(signal_ref);
        if (!sig) continue;
        auto events_it = src->eventsMap().find(signal_ref.msg_id);
        if (events_it == src->eventsMap().end() || events_it->second.empty()) continue;

        collect_signal_points(events_it->second, *sig, src->routeStartNanos(), start_sec, end_sec, xs, ys);
        if (xs.empty()) continue;

        std::string plot_label = signal_ref.signal_name + "##plot_" + std::to_string(i);
        ImPlot::PlotLine(plot_label.c_str(), xs.data(), ys.data(), (int)xs.size());
      }

      ImPlot::EndPlot();
    }

    ImGui::EndChild();
    ImGui::PopID();

    if (pending_split != -1 || pending_remove_chart != -1 || has_pending_remove_signal) {
      break;
    }
  }
  ImGui::EndChild();

  if (pending_split != -1) {
    st.splitChart(pending_split);
  }
  if (pending_remove_chart != -1) {
    st.removeChart(pending_remove_chart);
  }
  if (has_pending_remove_signal) {
    st.removeSignalFromCharts(pending_remove_signal.msg_id, pending_remove_signal.signal_name);
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
