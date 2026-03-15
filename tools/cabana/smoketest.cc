#include "tools/cabana/smoketest.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>

#include "common/timing.h"

namespace cabana::smoketest {

namespace {

struct State {
  bool enabled = std::getenv("CABANA_SMOKETEST") != nullptr;
  std::string stats_path = std::getenv("CABANA_SMOKETEST_STATS") ? std::getenv("CABANA_SMOKETEST_STATS") : "";
  std::string screenshot_path = std::getenv("CABANA_SMOKETEST_SCREENSHOT") ? std::getenv("CABANA_SMOKETEST_SCREENSHOT") : "";
  std::string validation_state_path = std::getenv("CABANA_VALIDATION_STATE") ? std::getenv("CABANA_VALIDATION_STATE") : "";
  bool session_restore_enabled = std::getenv("CABANA_VALIDATION_ALLOW_SESSION_RESTORE") != nullptr;
  std::string route_name;

  uint64_t process_start_ns = 0;
  uint64_t route_load_start_ns = 0;
  uint64_t route_load_done_ns = 0;
  uint64_t main_window_shown_ns = 0;
  uint64_t first_events_merged_ns = 0;
  uint64_t first_msgs_received_ns = 0;
  uint64_t auto_paused_ns = 0;
  uint64_t steady_state_ns = 0;

  double auto_paused_sec = 0.0;
  double steady_state_sec = 0.0;
  double max_ui_gap_ms = 0.0;
  uint64_t last_ui_tick_ns = 0;

  int window_width = 0;
  int window_height = 0;
  bool route_load_success = false;
  bool ready = false;
  std::array<int, 4> ui_gaps_over = {};
};

State &state() {
  static State s;
  return s;
}

uint64_t nowNs() {
  return nanos_since_boot();
}

void writeStats() {
  auto &s = state();
  if (!s.enabled || s.stats_path.empty()) return;

  QJsonObject obj;
  obj["ready"] = s.ready;
  obj["route_load_success"] = s.route_load_success;
  obj["route_name"] = QString::fromStdString(s.route_name);
  obj["process_start_ns"] = QString::number(s.process_start_ns);
  obj["route_load_start_ns"] = QString::number(s.route_load_start_ns);
  obj["route_load_done_ns"] = QString::number(s.route_load_done_ns);
  obj["main_window_shown_ns"] = QString::number(s.main_window_shown_ns);
  obj["first_events_merged_ns"] = QString::number(s.first_events_merged_ns);
  obj["first_msgs_received_ns"] = QString::number(s.first_msgs_received_ns);
  obj["auto_paused_ns"] = QString::number(s.auto_paused_ns);
  obj["steady_state_ns"] = QString::number(s.steady_state_ns);
  obj["auto_paused_sec"] = s.auto_paused_sec;
  obj["steady_state_sec"] = s.steady_state_sec;
  obj["window_width"] = s.window_width;
  obj["window_height"] = s.window_height;
  obj["max_ui_gap_ms"] = s.max_ui_gap_ms;
  obj["ui_gaps_over_16ms"] = s.ui_gaps_over[0];
  obj["ui_gaps_over_33ms"] = s.ui_gaps_over[1];
  obj["ui_gaps_over_50ms"] = s.ui_gaps_over[2];
  obj["ui_gaps_over_100ms"] = s.ui_gaps_over[3];

  auto elapsedMs = [&](uint64_t ts) -> double {
    if (!s.process_start_ns || !ts || ts < s.process_start_ns) return 0.0;
    return (ts - s.process_start_ns) / 1e6;
  };
  obj["route_load_ms"] = elapsedMs(s.route_load_done_ns);
  obj["window_shown_ms"] = elapsedMs(s.main_window_shown_ns);
  obj["first_events_merged_ms"] = elapsedMs(s.first_events_merged_ns);
  obj["first_msgs_received_ms"] = elapsedMs(s.first_msgs_received_ns);
  obj["auto_paused_ms"] = elapsedMs(s.auto_paused_ns);
  obj["steady_state_ms"] = elapsedMs(s.steady_state_ns);

  const auto json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
  const auto compact_json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(s.stats_path).parent_path(), ec);
  std::ofstream f(s.stats_path, std::ios::binary | std::ios::trunc);
  if (f.is_open()) {
    f.write(json.constData(), json.size());
  }
  std::cerr << "CABANA_SMOKETEST_STATS " << compact_json.constData() << std::endl;
}

std::optional<QSize> parseSize(const QString &text) {
  const QStringList parts = text.toLower().split('x');
  if (parts.size() != 2) return std::nullopt;

  bool ok_w = false;
  bool ok_h = false;
  const int w = parts[0].toInt(&ok_w);
  const int h = parts[1].toInt(&ok_h);
  if (!ok_w || !ok_h || w <= 0 || h <= 0) return std::nullopt;
  return QSize(w, h);
}

}  // namespace

bool enabled() {
  return state().enabled;
}

std::optional<QSize> forcedWindowSize() {
  if (!enabled()) return std::nullopt;
  const QString raw = qEnvironmentVariable("CABANA_SMOKETEST_SIZE", "1600x900");
  return parseSize(raw);
}

std::string screenshotPath() {
  return state().enabled ? state().screenshot_path : "";
}

std::string validationStatePath() {
  return state().enabled ? state().validation_state_path : "";
}

bool sessionRestoreEnabled() {
  return state().enabled && state().session_restore_enabled;
}

double maxUiGapMs() {
  return state().max_ui_gap_ms;
}

int uiGapsOver16Ms() {
  return state().ui_gaps_over[0];
}

int uiGapsOver33Ms() {
  return state().ui_gaps_over[1];
}

int uiGapsOver50Ms() {
  return state().ui_gaps_over[2];
}

int uiGapsOver100Ms() {
  return state().ui_gaps_over[3];
}

void recordProcessStart() {
  auto &s = state();
  if (!s.enabled || s.process_start_ns) return;
  s.process_start_ns = nowNs();
  writeStats();
}

void recordRouteLoadStart() {
  auto &s = state();
  if (!s.enabled || s.route_load_start_ns) return;
  s.route_load_start_ns = nowNs();
  writeStats();
}

void recordRouteLoadDone(bool success) {
  auto &s = state();
  if (!s.enabled || s.route_load_done_ns) return;
  s.route_load_done_ns = nowNs();
  s.route_load_success = success;
  writeStats();
}

void recordRouteName(const std::string &route_name) {
  auto &s = state();
  if (!s.enabled) return;
  s.route_name = route_name;
  writeStats();
}

void recordWindowShown(QWidget *window) {
  auto &s = state();
  if (!s.enabled || s.main_window_shown_ns) return;
  s.main_window_shown_ns = nowNs();
  if (window) {
    s.window_width = window->width();
    s.window_height = window->height();
  }
  writeStats();
}

void recordFirstEventsMerged() {
  auto &s = state();
  if (!s.enabled || s.first_events_merged_ns) return;
  s.first_events_merged_ns = nowNs();
  writeStats();
}

void recordFirstMsgsReceived() {
  auto &s = state();
  if (!s.enabled || s.first_msgs_received_ns) return;
  s.first_msgs_received_ns = nowNs();
  writeStats();
}

void recordAutoPaused(double current_sec) {
  auto &s = state();
  if (!s.enabled || s.auto_paused_ns) return;
  s.auto_paused_ns = nowNs();
  s.auto_paused_sec = current_sec;
  writeStats();
}

void markReady(double current_sec) {
  auto &s = state();
  if (!s.enabled || s.ready) return;
  s.ready = true;
  s.steady_state_ns = nowNs();
  s.steady_state_sec = current_sec;
  writeStats();
}

bool readyToFinalize() {
  auto &s = state();
  return s.enabled && !s.ready && s.main_window_shown_ns && s.first_events_merged_ns && s.first_msgs_received_ns;
}

bool isReady() {
  return state().ready;
}

void noteUiTick() {
  auto &s = state();
  if (!s.enabled) return;

  const uint64_t now = nowNs();
  if (!s.last_ui_tick_ns) {
    s.last_ui_tick_ns = now;
    return;
  }

  const double gap_ms = (now - s.last_ui_tick_ns) / 1e6;
  s.last_ui_tick_ns = now;
  s.max_ui_gap_ms = std::max(s.max_ui_gap_ms, gap_ms);

  constexpr std::array<double, 4> thresholds = {16.7, 33.3, 50.0, 100.0};
  for (size_t i = 0; i < thresholds.size(); ++i) {
    if (gap_ms > thresholds[i]) {
      ++s.ui_gaps_over[i];
    }
  }
}

}  // namespace cabana::smoketest
