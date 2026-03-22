#include "core/settings.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "core/app_state.h"
#include "third_party/json11/json11.hpp"

namespace cabana {
namespace settings {

namespace {

namespace fs = std::filesystem;

constexpr const char *kConfigDirName = "cabana_imgui";
constexpr const char *kSettingsFilename = "settings.json";
constexpr const char *kImGuiIniFilename = "imgui.ini";

fs::path configDir() {
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg) / kConfigDirName;
  }
  if (const char *home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".config" / kConfigDirName;
  }
  return fs::temp_directory_path() / kConfigDirName;
}

fs::path ensureConfigDir() {
  fs::path dir = configDir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::optional<MessageId> parseMessageId(const std::string &value) {
  const size_t sep = value.find(':');
  if (sep == std::string::npos) {
    return std::nullopt;
  }

  try {
    size_t source_pos = 0;
    const unsigned long source = std::stoul(value.substr(0, sep), &source_pos, 10);
    if (source_pos != sep || source > 0xFF) {
      return std::nullopt;
    }

    size_t addr_pos = 0;
    const std::string addr_str = value.substr(sep + 1);
    const unsigned long address = std::stoul(addr_str, &addr_pos, 16);
    if (addr_pos != addr_str.size() || address > 0xFFFFFFFFul) {
      return std::nullopt;
    }

    return MessageId{.source = (uint8_t)source, .address = (uint32_t)address};
  } catch (...) {
    return std::nullopt;
  }
}

json11::Json serializeSignal(const ChartSignalRef &signal) {
  return json11::Json::object{
    {"message_id", signal.msg_id.toString()},
    {"signal_name", signal.signal_name},
  };
}

json11::Json serializeChart(const ChartDefinition &chart) {
  json11::Json::array signals;
  signals.reserve(chart.signals.size());
  for (const auto &signal : chart.signals) {
    signals.push_back(serializeSignal(signal));
  }
  return json11::Json::object{{"signals", signals}};
}

json11::Json serializeTab(const ChartTabState &tab) {
  json11::Json::array charts;
  charts.reserve(tab.charts.size());
  for (const auto &chart : tab.charts) {
    charts.push_back(serializeChart(chart));
  }
  return json11::Json::object{
    {"id", tab.id},
    {"charts", charts},
  };
}

json11::Json serializeStringArray(const std::vector<std::string> &values) {
  json11::Json::array items;
  items.reserve(values.size());
  for (const auto &value : values) {
    items.push_back(value);
  }
  return items;
}

json11::Json serializeDbcAssignments(const std::map<int, std::string> &assignments) {
  json11::Json::object object;
  for (const auto &[source, path] : assignments) {
    if (!path.empty()) {
      object[std::to_string(source)] = path;
    }
  }
  return object;
}

const char *detailTabName(DetailTab tab) {
  switch (tab) {
    case DetailTab::Binary: return "binary";
    case DetailTab::Signals: return "signals";
    case DetailTab::History: return "history";
  }
  return "binary";
}

std::optional<DetailTab> parseDetailTab(const std::string &value) {
  if (value == "binary") return DetailTab::Binary;
  if (value == "signals") return DetailTab::Signals;
  if (value == "history") return DetailTab::History;
  return std::nullopt;
}

void parseStringArray(const json11::Json &json, std::vector<std::string> &target) {
  if (!json.is_array()) return;
  target.clear();
  for (const auto &item : json.array_items()) {
    const std::string value = item.string_value();
    if (!value.empty()) {
      target.push_back(value);
    }
  }
}

void parseDbcAssignments(const json11::Json &json, std::map<int, std::string> &target) {
  if (!json.is_object()) return;
  target.clear();
  for (const auto &[key, value] : json.object_items()) {
    if (!value.is_string() || value.string_value().empty()) {
      continue;
    }
    try {
      target[std::stoi(key)] = value.string_value();
    } catch (...) {
    }
  }
}

std::optional<ChartSignalRef> parseSignal(const json11::Json &json) {
  const auto &object = json.object_items();
  auto msg_id_it = object.find("message_id");
  auto signal_name_it = object.find("signal_name");
  if (msg_id_it == object.end() || signal_name_it == object.end()) {
    return std::nullopt;
  }

  auto msg_id = parseMessageId(msg_id_it->second.string_value());
  if (!msg_id.has_value()) {
    return std::nullopt;
  }

  const std::string signal_name = signal_name_it->second.string_value();
  if (signal_name.empty()) {
    return std::nullopt;
  }

  return ChartSignalRef{.msg_id = *msg_id, .signal_name = signal_name};
}

std::optional<ChartDefinition> parseChart(const json11::Json &json) {
  const auto &object = json.object_items();
  auto it = object.find("signals");
  if (it == object.end() || !it->second.is_array()) {
    return std::nullopt;
  }

  ChartDefinition chart;
  for (const auto &signal_json : it->second.array_items()) {
    auto signal = parseSignal(signal_json);
    if (signal.has_value()) {
      chart.signals.push_back(*signal);
    }
  }
  return chart;
}

std::optional<ChartTabState> parseTab(const json11::Json &json) {
  const auto &object = json.object_items();
  auto charts_it = object.find("charts");
  if (charts_it == object.end() || !charts_it->second.is_array()) {
    return std::nullopt;
  }

  ChartTabState tab;
  auto id_it = object.find("id");
  if (id_it != object.end() && id_it->second.is_number()) {
    tab.id = (int)id_it->second.int_value();
  }

  for (const auto &chart_json : charts_it->second.array_items()) {
    auto chart = parseChart(chart_json);
    if (chart.has_value()) {
      tab.charts.push_back(std::move(*chart));
    }
  }
  return tab;
}

}  // namespace

std::string imguiIniPath() {
  return (ensureConfigDir() / kImGuiIniFilename).string();
}

std::string statePath() {
  return (ensureConfigDir() / kSettingsFilename).string();
}

bool load(AppState &state) {
  const fs::path path = statePath();
  if (!fs::exists(path)) {
    return false;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  std::string json_str((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::string err;
  const json11::Json json = json11::Json::parse(json_str, err);
  if (!err.empty() || !json.is_object()) {
    return false;
  }

  const auto &object = json.object_items();

  auto range_it = object.find("chart_range_sec");
  if (range_it != object.end() && range_it->second.is_number()) {
    state.chart_range_sec = std::clamp((float)range_it->second.number_value(), 1.0f, 60.0f);
  }

  auto selected_msg_it = object.find("selected_message");
  if (selected_msg_it != object.end()) {
    auto msg_id = parseMessageId(selected_msg_it->second.string_value());
    if (msg_id.has_value()) {
      state.has_selection = true;
      state.selected_msg = *msg_id;
    }
  }

  auto detail_tab_it = object.find("current_detail_tab");
  if (detail_tab_it != object.end()) {
    auto tab = parseDetailTab(detail_tab_it->second.string_value());
    if (tab.has_value()) {
      state.current_detail_tab = *tab;
    }
  }

  auto active_dbc_it = object.find("active_dbc_file");
  if (active_dbc_it != object.end()) {
    state.active_dbc_file = active_dbc_it->second.string_value();
  }

  auto active_dbcs_it = object.find("active_dbc_files");
  if (active_dbcs_it != object.end()) {
    parseDbcAssignments(active_dbcs_it->second, state.active_dbc_files);
  }
  if (state.active_dbc_files.empty() && !state.active_dbc_file.empty()) {
    state.active_dbc_files[-1] = state.active_dbc_file;
  }

  auto recent_dbc_it = object.find("recent_dbc_files");
  if (recent_dbc_it != object.end()) {
    parseStringArray(recent_dbc_it->second, state.recent_dbc_files);
  }

  auto recent_routes_it = object.find("recent_routes");
  if (recent_routes_it != object.end()) {
    parseStringArray(recent_routes_it->second, state.recent_routes);
  }

  auto current_tab_it = object.find("current_chart_tab");
  if (current_tab_it != object.end() && current_tab_it->second.is_number()) {
    state.current_chart_tab = current_tab_it->second.int_value();
  }

  auto selected_chart_it = object.find("selected_chart");
  if (selected_chart_it != object.end() && selected_chart_it->second.is_number()) {
    state.selected_chart = selected_chart_it->second.int_value();
  }

  auto next_tab_id_it = object.find("next_chart_tab_id");
  if (next_tab_id_it != object.end() && next_tab_id_it->second.is_number()) {
    state.next_chart_tab_id = std::max(1, next_tab_id_it->second.int_value());
  }

  auto tabs_it = object.find("chart_tabs");
  if (tabs_it != object.end() && tabs_it->second.is_array()) {
    state.chart_tabs.clear();
    for (const auto &tab_json : tabs_it->second.array_items()) {
      auto tab = parseTab(tab_json);
      if (tab.has_value()) {
        if (tab->id <= 0) {
          tab->id = state.next_chart_tab_id++;
        } else {
          state.next_chart_tab_id = std::max(state.next_chart_tab_id, tab->id + 1);
        }
        state.chart_tabs.push_back(std::move(*tab));
      }
    }
  }

  state.ensureChartTab();
  state.settings_dirty = false;
  return true;
}

bool save(const AppState &state) {
  const fs::path path = statePath();
  std::ofstream output(path);
  if (!output.is_open()) {
    return false;
  }

  json11::Json::array tabs;
  tabs.reserve(state.chart_tabs.size());
  for (const auto &tab : state.chart_tabs) {
    tabs.push_back(serializeTab(tab));
  }

  const json11::Json json = json11::Json::object{
    {"selected_message", state.has_selection ? state.selected_msg.toString() : std::string()},
    {"current_detail_tab", detailTabName(state.current_detail_tab)},
    {"active_dbc_file", state.active_dbc_file},
    {"active_dbc_files", serializeDbcAssignments(state.active_dbc_files)},
    {"recent_dbc_files", serializeStringArray(state.recent_dbc_files)},
    {"recent_routes", serializeStringArray(state.recent_routes)},
    {"chart_range_sec", state.chart_range_sec},
    {"current_chart_tab", state.current_chart_tab},
    {"selected_chart", state.selected_chart},
    {"next_chart_tab_id", state.next_chart_tab_id},
    {"chart_tabs", tabs},
  };

  output << json.dump();
  output << '\n';
  return output.good();
}

}  // namespace settings
}  // namespace cabana
