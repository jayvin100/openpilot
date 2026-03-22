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

}  // namespace

void charts() {
  ImGui::Begin("Charts");

  auto &st = cabana::app_state();
  auto *src = app() ? app()->source() : nullptr;

  static float chart_range = 7.0f;

  ImGui::BeginDisabled();
  icon_button("new_chart", "file-plus");
  ImGui::SameLine();
  icon_button("new_tab", "window-stack");
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (!st.has_selection) {
    ImGui::Text("Charts: 0");
  } else {
    const auto *dbc_msg = cabana::dbc::dbc_manager().msg(st.selected_msg.address);
    const int signal_count = dbc_msg ? (int)dbc_msg->signals.size() : 0;
    ImGui::Text("Charts: %d", signal_count);
  }

  ImGui::SameLine();
  ImGui::TextDisabled("Selected Message");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##range", &chart_range, 1.0f, 60.0f, "%.0f s");

  ImGui::Separator();

  if (!st.has_selection) {
    ImGui::TextDisabled("Select a message to plot decoded signals.");
    ImGui::End();
    return;
  }

  if (!src) {
    ImGui::TextDisabled(st.route_loading ? "Route is still loading..." : "No stream loaded.");
    ImGui::End();
    return;
  }

  auto events_it = src->eventsMap().find(st.selected_msg);
  if (events_it == src->eventsMap().end() || events_it->second.empty()) {
    ImGui::TextDisabled("Waiting for indexed CAN events...");
    ImGui::End();
    return;
  }

  const auto *dbc_msg = cabana::dbc::dbc_manager().msg(st.selected_msg.address);
  if (!dbc_msg || dbc_msg->signals.empty()) {
    ImGui::TextDisabled("Selected message has no decoded signals to chart.");
    ImGui::End();
    return;
  }

  const double end_sec = std::clamp(st.current_sec > 0.0 ? st.current_sec : st.max_sec, st.min_sec, st.max_sec);
  const double start_sec = std::max(st.min_sec, end_sec - chart_range);
  const auto &events = events_it->second;

  std::vector<double> xs;
  std::vector<double> ys;

  ImGui::BeginChild("##chart_scroll");
  for (int i = 0; i < (int)dbc_msg->signals.size(); ++i) {
    const auto &sig = dbc_msg->signals[i];
    collect_signal_points(events, sig, src->routeStartNanos(), start_sec, end_sec, xs, ys);

    const std::string plot_id = sig.name + "##plot";
    if (ImPlot::BeginPlot(plot_id.c_str(), ImVec2(-1, 180), ImPlotFlags_NoLegend)) {
      ImPlot::SetupAxes("Time (s)", sig.unit.empty() ? "Value" : sig.unit.c_str(),
                        ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxisLimits(ImAxis_X1, start_sec, end_sec, ImPlotCond_Always);

      if (!xs.empty()) {
        ImPlot::PlotLine(sig.name.c_str(), xs.data(), ys.data(), (int)xs.size());
      }

      ImPlot::EndPlot();
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
