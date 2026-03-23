#include "tools/jotpluggler/app_internal.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr float kSplitterThickness = 10.0f;
constexpr float kMinMessagesWidth = 240.0f;
constexpr float kMinCenterWidth = 320.0f;
constexpr float kMinRightWidth = 280.0f;
constexpr float kMinTopHeight = 140.0f;
constexpr float kMinBottomHeight = 120.0f;
constexpr std::array<std::array<uint8_t, 3>, 4> kSignalHighlightColors = {{
  {69, 137, 255},
  {55, 171, 112},
  {232, 171, 44},
  {198, 89, 71},
}};

const fs::path &cabana_repo_root() {
  static const fs::path root = []() {
#ifdef JOTP_REPO_ROOT
    return fs::path(JOTP_REPO_ROOT);
#else
    return fs::current_path();
#endif
  }();
  return root;
}

bool parse_can_message_path(std::string_view path,
                            std::string_view *service,
                            int *bus,
                            std::string_view *message,
                            std::string_view *signal) {
  if (path.empty() || path.front() != '/') {
    return false;
  }
  size_t a = path.find('/', 1);
  if (a == std::string_view::npos) return false;
  size_t b = path.find('/', a + 1);
  if (b == std::string_view::npos) return false;
  size_t c = path.find('/', b + 1);
  if (c == std::string_view::npos) return false;
  size_t d = path.find('/', c + 1);
  if (d != std::string_view::npos) return false;

  *service = path.substr(1, a - 1);
  if (*service != "can" && *service != "sendcan") {
    return false;
  }
  try {
    *bus = std::stoi(std::string(path.substr(a + 1, b - a - 1)));
  } catch (...) {
    return false;
  }
  *message = path.substr(b + 1, c - b - 1);
  *signal = path.substr(c + 1);
  return !message->empty() && !signal->empty();
}

std::optional<CanServiceKind> parse_can_service_kind(std::string_view service) {
  if (service == "can") return CanServiceKind::Can;
  if (service == "sendcan") return CanServiceKind::Sendcan;
  return std::nullopt;
}

const char *can_service_name(CanServiceKind service) {
  return service == CanServiceKind::Can ? "can" : "sendcan";
}

std::string format_can_address(uint32_t address) {
  char text[32];
  std::snprintf(text, sizeof(text), "0x%X", address);
  return text;
}

std::string can_message_key(CanServiceKind service, uint8_t bus, uint32_t address) {
  return "/" + std::string(can_service_name(service)) + "/" + std::to_string(bus) + "/" + format_can_address(address);
}

std::string sanitize_filename_component(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out.empty() ? "untitled" : out;
}

fs::path cabana_export_dir() {
  const char *home = std::getenv("HOME");
  fs::path root = home != nullptr ? fs::path(home) : fs::current_path();
  const fs::path downloads = root / "Downloads";
  return (fs::exists(downloads) ? downloads : root) / "jotpluggler_exports";
}

std::string csv_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (char c : text) {
    if (c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string payload_hex(const std::string &data) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(data.size() * 2);
  for (unsigned char byte : data) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0xF]);
  }
  return out;
}

fs::path cabana_export_path(const AppSession &session,
                            const CabanaMessageSummary &message,
                            std::string_view kind) {
  const std::string route_part = sanitize_filename_component(session.route_name.empty() ? "stream" : session.route_name);
  char filename[256];
  std::snprintf(filename, sizeof(filename), "%s_bus%d_0x%X_%.*s.csv",
                sanitize_filename_component(message.name).c_str(),
                message.bus,
                message.address,
                static_cast<int>(kind.size()),
                kind.data());
  return cabana_export_dir() / route_part / filename;
}

std::optional<dbc::Database> load_active_dbc(const AppSession &session) {
  const std::string &dbc_name = !session.dbc_override.empty() ? session.dbc_override : session.route_data.dbc_name;
  if (dbc_name.empty()) {
    return std::nullopt;
  }
  for (const fs::path &candidate : {
         cabana_repo_root() / "opendbc" / "dbc" / (dbc_name + ".dbc"),
         cabana_repo_root() / "tools" / "jotpluggler" / "generated_dbcs" / (dbc_name + ".dbc"),
       }) {
    if (fs::exists(candidate)) {
      return std::optional<dbc::Database>(std::in_place, candidate);
    }
  }
  return std::nullopt;
}

struct DbcNameLookup {
  std::unordered_map<std::string, uint32_t> unique_name_to_address;
};

struct BitBehaviorStats {
  double ones_ratio = 0.0;
  double flip_ratio = 0.0;
  size_t samples = 0;
};

DbcNameLookup build_dbc_name_lookup(const std::optional<dbc::Database> &db) {
  DbcNameLookup out;
  if (!db.has_value()) {
    return out;
  }
  std::unordered_set<std::string> duplicates;
  for (const auto &[address, message] : db->messages()) {
    auto [it, inserted] = out.unique_name_to_address.emplace(message.name, address);
    if (!inserted) {
      duplicates.insert(message.name);
      out.unique_name_to_address.erase(it);
    }
  }
  for (const std::string &name : duplicates) {
    out.unique_name_to_address.erase(name);
  }
  return out;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const std::string hay = lowercase(haystack);
  const std::string ndl = lowercase(needle);
  return hay.find(ndl) != std::string::npos;
}

const CanMessageData *find_message_data(const AppSession &session, const CabanaMessageSummary &message) {
  const std::optional<CanServiceKind> service = parse_can_service_kind(message.service);
  if (!service.has_value()) {
    return nullptr;
  }
  const CanMessageData key{.id = CanMessageId{*service, static_cast<uint8_t>(message.bus), message.address}};
  auto it = std::lower_bound(session.route_data.can_messages.begin(),
                             session.route_data.can_messages.end(),
                             key,
                             [](const CanMessageData &a, const CanMessageData &b) {
                               return std::make_tuple(a.id.service, a.id.bus, a.id.address)
                                    < std::make_tuple(b.id.service, b.id.bus, b.id.address);
                             });
  if (it == session.route_data.can_messages.end()
      || it->id.service != key.id.service
      || it->id.bus != key.id.bus
      || it->id.address != key.id.address) {
    return nullptr;
  }
  return &*it;
}

const CabanaSignalSummary *find_signal_summary(const CabanaMessageSummary &message, std::string_view path) {
  auto it = std::find_if(message.signals.begin(), message.signals.end(), [&](const CabanaSignalSummary &signal) {
    return signal.path == path;
  });
  return it == message.signals.end() ? nullptr : &*it;
}

void open_cabana_signal_editor(const AppSession &session,
                               UiState *state,
                               const CabanaMessageSummary &message,
                               const CabanaSignalSummary &signal) {
  const std::optional<dbc::Database> db = load_active_dbc(session);
  const dbc::Message *dbc_message = db.has_value() ? db->message(message.address) : nullptr;
  if (dbc_message == nullptr) {
    state->error_text = "No active DBC message available for editing";
    state->open_error_popup = true;
    return;
  }
  auto it = std::find_if(dbc_message->signals.begin(), dbc_message->signals.end(), [&](const dbc::Signal &dbc_signal) {
    return dbc_signal.name == signal.name;
  });
  if (it == dbc_message->signals.end()) {
    state->error_text = "Signal not found in active DBC";
    state->open_error_popup = true;
    return;
  }

  CabanaSignalEditorState &editor = state->cabana_signal_editor;
  editor.open = true;
  editor.loaded = true;
  editor.creating = false;
  editor.message_root = message.root_path;
  editor.message_name = message.name;
  editor.service = message.service;
  editor.bus = message.bus;
  editor.message_address = message.address;
  editor.original_signal_name = it->name;
  editor.signal_name = it->name;
  editor.start_bit = it->start_bit;
  editor.size = it->size;
  editor.factor = it->factor;
  editor.offset = it->offset;
  editor.min = it->min;
  editor.max = it->max;
  editor.is_signed = it->is_signed;
  editor.is_little_endian = it->is_little_endian;
  editor.type = static_cast<int>(it->type);
  editor.multiplex_value = it->multiplex_value;
  editor.receiver_name = it->receiver_name;
  editor.unit = it->unit;
}

void open_cabana_new_signal_editor(const AppSession &session,
                                   UiState *state,
                                   const CabanaMessageSummary &message,
                                   int byte_index,
                                   int bit_index) {
  const std::optional<dbc::Database> db = load_active_dbc(session);
  const dbc::Message *dbc_message = db.has_value() ? db->message(message.address) : nullptr;
  if (dbc_message == nullptr) {
    state->error_text = "No active DBC message available for creating a signal";
    state->open_error_popup = true;
    return;
  }

  std::string base_name = "bit_" + std::to_string(byte_index) + "_" + std::to_string(bit_index);
  std::string signal_name = base_name;
  int suffix = 2;
  auto exists = [&](std::string_view candidate) {
    return std::any_of(dbc_message->signals.begin(), dbc_message->signals.end(), [&](const dbc::Signal &signal) {
      return signal.name == candidate;
    });
  };
  while (exists(signal_name)) {
    signal_name = base_name + "_" + std::to_string(suffix++);
  }

  CabanaSignalEditorState &editor = state->cabana_signal_editor;
  editor.open = true;
  editor.loaded = true;
  editor.creating = true;
  editor.message_root = message.root_path;
  editor.message_name = message.name;
  editor.service = message.service;
  editor.bus = message.bus;
  editor.message_address = message.address;
  editor.original_signal_name.clear();
  editor.signal_name = signal_name;
  editor.start_bit = byte_index * 8 + bit_index;
  editor.size = 1;
  editor.factor = 1.0;
  editor.offset = 0.0;
  editor.min = 0.0;
  editor.max = 1.0;
  editor.is_signed = false;
  editor.is_little_endian = true;
  editor.type = static_cast<int>(dbc::Signal::Type::Normal);
  editor.multiplex_value = 0;
  editor.receiver_name = "XXX";
  editor.unit.clear();
}

const CabanaMessageSummary *find_selected_message(const AppSession &session, const UiState &state) {
  auto it = std::find_if(session.cabana_messages.begin(), session.cabana_messages.end(), [&](const CabanaMessageSummary &message) {
    return message.root_path == state.cabana.selected_message_root;
  });
  return it == session.cabana_messages.end() ? nullptr : &*it;
}

bool similar_bit_results_match_selection(const UiState &state) {
  return state.cabana.has_bit_selection
      && state.cabana.similar_bits_source_root == state.cabana.selected_message_root
      && state.cabana.similar_bits_source_byte == state.cabana.selected_bit_byte
      && state.cabana.similar_bits_source_bit == state.cabana.selected_bit_index;
}

void sync_cabana_selection(AppSession *session, UiState *state) {
  if (!state->cabana_mode_initialized) {
    state->cabana.camera_view = sidebar_preview_camera_view(*session);
    state->cabana_mode_initialized = true;
  }
  if (session->cabana_messages.empty()) {
    state->cabana.selected_message_root.clear();
    state->cabana.chart_signal_paths.clear();
    state->cabana.has_bit_selection = false;
    state->cabana.similar_bit_matches.clear();
    return;
  }
  const CabanaMessageSummary *selected = find_selected_message(*session, *state);
  if (selected == nullptr) {
    state->cabana.selected_message_root = session->cabana_messages.front().root_path;
    state->cabana.has_bit_selection = false;
    state->cabana.similar_bit_matches.clear();
    selected = &session->cabana_messages.front();
  }

  std::unordered_set<std::string> allowed;
  for (const CabanaSignalSummary &signal : selected->signals) {
    allowed.insert(signal.path);
  }
  state->cabana.chart_signal_paths.erase(
    std::remove_if(state->cabana.chart_signal_paths.begin(), state->cabana.chart_signal_paths.end(),
                   [&](const std::string &path) { return !allowed.count(path); }),
    state->cabana.chart_signal_paths.end());

  if (state->cabana.chart_signal_paths.empty()) {
    for (size_t i = 0; i < std::min<size_t>(4, selected->signals.size()); ++i) {
      state->cabana.chart_signal_paths.push_back(selected->signals[i].path);
    }
  }
}

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

void draw_vertical_splitter(const char *id,
                            float height,
                            float min_left,
                            float max_left,
                            float *left_width) {
  const ImVec2 size(kSplitterThickness, height);
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
  const ImVec2 size(kSplitterThickness, height);
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

void draw_horizontal_splitter(const char *id,
                              float width,
                              float min_top,
                              float max_top,
                              float *top_height) {
  const ImVec2 size(width, kSplitterThickness);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
  if (ImGui::IsItemActive()) {
    *top_height = std::clamp(*top_height + ImGui::GetIO().MouseDelta.y, min_top, max_top);
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
}

std::optional<double> cabana_message_value_at_time(const AppSession &session, std::string_view path, double tracker_time) {
  const RouteSeries *series = app_find_route_series(session, std::string(path));
  if (series == nullptr || series->times.empty() || series->values.empty()) {
    return std::nullopt;
  }
  return app_sample_xy_value_at_time(series->times, series->values, false, tracker_time);
}

std::string cabana_value_label(const AppSession &session, std::string_view path, double tracker_time) {
  const auto value = cabana_message_value_at_time(session, path, tracker_time);
  const auto format_it = session.route_data.series_formats.find(std::string(path));
  const auto enum_it = session.route_data.enum_info.find(std::string(path));
  if (!value.has_value() || format_it == session.route_data.series_formats.end()) {
    return "--";
  }
  return format_display_value(*value,
                              format_it->second,
                              enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second);
}

bool export_raw_can_csv(const AppSession &session,
                        const CabanaMessageSummary &message,
                        std::string *error,
                        fs::path *output_path) {
  const CanMessageData *message_data = find_message_data(session, message);
  if (message_data == nullptr || message_data->samples.empty()) {
    if (error != nullptr) *error = "No raw CAN frames available";
    return false;
  }

  const fs::path output = cabana_export_path(session, message, "raw");
  fs::create_directories(output.parent_path());
  std::ofstream out(output);
  if (!out.is_open()) {
    if (error != nullptr) *error = "Failed to open raw CSV";
    return false;
  }

  out << "mono_time,bus_time,dt_ms,data_hex\n";
  for (size_t i = 0; i < message_data->samples.size(); ++i) {
    const CanFrameSample &sample = message_data->samples[i];
    out << sample.mono_time << ','
        << sample.bus_time << ',';
    if (i > 0) {
      out << 1000.0 * (sample.mono_time - message_data->samples[i - 1].mono_time);
    }
    out << ',' << csv_escape(payload_hex(sample.data)) << '\n';
  }

  if (!out.good()) {
    if (error != nullptr) *error = "Failed while writing raw CSV";
    return false;
  }
  if (output_path != nullptr) *output_path = output;
  return true;
}

bool export_decoded_can_csv(const AppSession &session,
                            const CabanaMessageSummary &message,
                            std::string *error,
                            fs::path *output_path) {
  const CanMessageData *message_data = find_message_data(session, message);
  if (message_data == nullptr || message_data->samples.empty()) {
    if (error != nullptr) *error = "No raw CAN frames available";
    return false;
  }
  if (message.signals.empty()) {
    if (error != nullptr) *error = "No decoded signals for this message";
    return false;
  }

  std::vector<const CabanaSignalSummary *> export_signals;
  std::vector<const RouteSeries *> export_series;
  std::vector<const SeriesFormat *> export_formats;
  std::vector<const EnumInfo *> export_enums;
  export_signals.reserve(message.signals.size());
  export_series.reserve(message.signals.size());
  export_formats.reserve(message.signals.size());
  export_enums.reserve(message.signals.size());

  for (const CabanaSignalSummary &signal : message.signals) {
    const RouteSeries *series = app_find_route_series(session, signal.path);
    if (series == nullptr) {
      continue;
    }
    export_signals.push_back(&signal);
    export_series.push_back(series);
    auto format_it = session.route_data.series_formats.find(signal.path);
    export_formats.push_back(format_it == session.route_data.series_formats.end() ? nullptr : &format_it->second);
    auto enum_it = session.route_data.enum_info.find(signal.path);
    export_enums.push_back(enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second);
  }

  if (export_series.empty()) {
    if (error != nullptr) *error = "No decoded signal data available";
    return false;
  }

  const fs::path output = cabana_export_path(session, message, "decoded");
  fs::create_directories(output.parent_path());
  std::ofstream out(output);
  if (!out.is_open()) {
    if (error != nullptr) *error = "Failed to open decoded CSV";
    return false;
  }

  out << "mono_time,bus_time,dt_ms,data_hex";
  for (const CabanaSignalSummary *signal : export_signals) {
    out << ',' << csv_escape(signal->name);
  }
  out << '\n';

  for (size_t i = 0; i < message_data->samples.size(); ++i) {
    const CanFrameSample &sample = message_data->samples[i];
    out << sample.mono_time << ','
        << sample.bus_time << ',';
    if (i > 0) {
      out << 1000.0 * (sample.mono_time - message_data->samples[i - 1].mono_time);
    }
    out << ',' << csv_escape(payload_hex(sample.data));
    for (size_t j = 0; j < export_series.size(); ++j) {
      out << ',';
      const std::optional<double> value = app_sample_xy_value_at_time(
        export_series[j]->times, export_series[j]->values, false, sample.mono_time);
      if (value.has_value()) {
        out << csv_escape(export_formats[j] != nullptr
                            ? format_display_value(*value, *export_formats[j], export_enums[j])
                            : std::to_string(*value));
      }
    }
    out << '\n';
  }

  if (!out.good()) {
    if (error != nullptr) *error = "Failed while writing decoded CSV";
    return false;
  }
  if (output_path != nullptr) *output_path = output;
  return true;
}

size_t closest_can_sample_index(const CanMessageData &message, double tracker_time) {
  if (message.samples.empty()) {
    return 0;
  }
  auto it = std::lower_bound(message.samples.begin(), message.samples.end(), tracker_time,
                             [](const CanFrameSample &sample, double time) {
                               return sample.mono_time < time;
                             });
  if (it == message.samples.begin()) {
    return 0;
  }
  if (it == message.samples.end()) {
    return message.samples.size() - 1;
  }
  const size_t upper = static_cast<size_t>(it - message.samples.begin());
  const size_t lower = upper - 1;
  return std::abs(message.samples[upper].mono_time - tracker_time) < std::abs(message.samples[lower].mono_time - tracker_time)
    ? upper
    : lower;
}

uint8_t can_bit(const std::string &data, size_t byte_index, int bit_index) {
  if (byte_index >= data.size() || bit_index < 0 || bit_index > 7) {
    return 0;
  }
  return (static_cast<uint8_t>(data[byte_index]) >> bit_index) & 0x1;
}

BitBehaviorStats bit_behavior_stats(const CanMessageData &message, size_t byte_index, int bit_index) {
  BitBehaviorStats stats;
  if (message.samples.empty()) {
    return stats;
  }
  size_t ones = 0;
  size_t flips = 0;
  uint8_t prev = can_bit(message.samples.front().data, byte_index, bit_index);
  ones += prev;
  for (size_t i = 1; i < message.samples.size(); ++i) {
    const uint8_t bit = can_bit(message.samples[i].data, byte_index, bit_index);
    ones += bit;
    flips += bit != prev;
    prev = bit;
  }
  stats.samples = message.samples.size();
  stats.ones_ratio = static_cast<double>(ones) / static_cast<double>(stats.samples);
  stats.flip_ratio = stats.samples > 1 ? static_cast<double>(flips) / static_cast<double>(stats.samples - 1) : 0.0;
  return stats;
}

size_t can_message_payload_width(const CanMessageData &message) {
  size_t width = 0;
  for (const CanFrameSample &sample : message.samples) {
    width = std::max(width, sample.data.size());
  }
  return width;
}

std::string format_can_payload(const std::string &data) {
  std::string text;
  text.reserve(data.size() * 3);
  for (size_t i = 0; i < data.size(); ++i) {
    if (!text.empty()) text.push_back(' ');
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned char>(data[i]));
    text += hex;
  }
  return text;
}

bool signal_contains_bit(const CabanaSignalSummary &signal, size_t byte_index, int bit_index) {
  if (!signal.has_bit_range || bit_index < 0 || bit_index > 7) {
    return false;
  }
  const int msb_byte = signal.msb / 8;
  const int lsb_byte = signal.lsb / 8;
  if (msb_byte == lsb_byte) {
    return static_cast<int>(byte_index) == msb_byte
        && bit_index >= (signal.lsb & 7)
        && bit_index <= (signal.msb & 7);
  }
  for (int i = msb_byte, step = signal.is_little_endian ? -1 : 1;; i += step) {
    const int hi = i == msb_byte ? (signal.msb & 7) : 7;
    const int lo = i == lsb_byte ? (signal.lsb & 7) : 0;
    if (static_cast<int>(byte_index) == i && bit_index >= lo && bit_index <= hi) {
      return true;
    }
    if (i == lsb_byte) {
      return false;
    }
  }
}

std::vector<std::pair<const CabanaSignalSummary *, ImU32>> highlighted_signals(const CabanaMessageSummary &message, const UiState &state) {
  std::vector<std::pair<const CabanaSignalSummary *, ImU32>> out;
  for (const std::string &path : state.cabana.chart_signal_paths) {
    const CabanaSignalSummary *signal = find_signal_summary(message, path);
    if (signal == nullptr || !signal->has_bit_range) {
      continue;
    }
    const auto &rgb = kSignalHighlightColors[out.size() % kSignalHighlightColors.size()];
    out.push_back({signal, ImGui::GetColorU32(color_rgb(rgb, 0.26f))});
  }
  return out;
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

bool cabana_bit_selected(const UiState &state, size_t byte_index, int bit_index) {
  return state.cabana.has_bit_selection
      && state.cabana.selected_bit_byte == static_cast<int>(byte_index)
      && state.cabana.selected_bit_index == bit_index;
}

std::vector<const CabanaSignalSummary *> selected_bit_signals(const CabanaMessageSummary &message, const UiState &state) {
  std::vector<const CabanaSignalSummary *> out;
  if (!state.cabana.has_bit_selection) {
    return out;
  }
  for (const CabanaSignalSummary &signal : message.signals) {
    if (signal_contains_bit(signal,
                            static_cast<size_t>(state.cabana.selected_bit_byte),
                            state.cabana.selected_bit_index)) {
      out.push_back(&signal);
    }
  }
  return out;
}

std::vector<CabanaSimilarBitMatch> find_similar_bits(const AppSession &session,
                                                     const CabanaMessageSummary &source_message,
                                                     const CanMessageData &source_data,
                                                     size_t source_byte,
                                                     int source_bit) {
  const BitBehaviorStats target = bit_behavior_stats(source_data, source_byte, source_bit);
  std::vector<CabanaSimilarBitMatch> matches;
  for (const CabanaMessageSummary &message : session.cabana_messages) {
    const CanMessageData *message_data = find_message_data(session, message);
    if (message_data == nullptr || message_data->samples.size() < 2) {
      continue;
    }
    for (size_t byte = 0; byte < can_message_payload_width(*message_data); ++byte) {
      for (int bit = 0; bit < 8; ++bit) {
        if (message.root_path == source_message.root_path
            && static_cast<int>(byte) == static_cast<int>(source_byte)
            && bit == source_bit) {
          continue;
        }
        const BitBehaviorStats stats = bit_behavior_stats(*message_data, byte, bit);
        if (stats.samples < 2) {
          continue;
        }
        const double ones_diff = std::abs(stats.ones_ratio - target.ones_ratio);
        const double flip_diff = std::abs(stats.flip_ratio - target.flip_ratio);
        const double score = ones_diff * 0.65 + flip_diff * 0.35;
        matches.push_back({
          .message_root = message.root_path,
          .label = message.name,
          .bus = message.bus,
          .address = message.address,
          .byte_index = static_cast<int>(byte),
          .bit_index = bit,
          .score = score,
          .ones_ratio = stats.ones_ratio,
          .flip_ratio = stats.flip_ratio,
        });
      }
    }
  }
  std::sort(matches.begin(), matches.end(), [](const CabanaSimilarBitMatch &a, const CabanaSimilarBitMatch &b) {
    return std::tie(a.score, a.label, a.byte_index, a.bit_index)
         < std::tie(b.score, b.label, b.byte_index, b.bit_index);
  });
  if (matches.size() > 12) {
    matches.resize(12);
  }
  return matches;
}

void draw_bit_selection_panel(AppSession *session, const CabanaMessageSummary &message, UiState *state) {
  if (!state->cabana.has_bit_selection) {
    return;
  }
  app_push_bold_font();
  ImGui::Text("Selected Bit: B%d.%d", state->cabana.selected_bit_byte, state->cabana.selected_bit_index);
  app_pop_bold_font();
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    state->cabana.has_bit_selection = false;
    state->cabana.similar_bit_matches.clear();
    return;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Find Similar Bits")) {
    const CanMessageData *message_data = find_message_data(*session, message);
    if (message_data != nullptr) {
      state->cabana.similar_bit_matches = find_similar_bits(*session,
                                                            message,
                                                            *message_data,
                                                            static_cast<size_t>(state->cabana.selected_bit_byte),
                                                            state->cabana.selected_bit_index);
      state->cabana.similar_bits_source_root = message.root_path;
      state->cabana.similar_bits_source_byte = state->cabana.selected_bit_byte;
      state->cabana.similar_bits_source_bit = state->cabana.selected_bit_index;
    }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Create Signal...")) {
    open_cabana_new_signal_editor(*session,
                                  state,
                                  message,
                                  state->cabana.selected_bit_byte,
                                  state->cabana.selected_bit_index);
  }
  const auto overlaps = selected_bit_signals(message, *state);
  if (overlaps.empty()) {
    ImGui::TextDisabled("No decoded signals cover this bit.");
  } else {
    ImGui::TextDisabled("Signals covering this bit:");
    for (size_t i = 0; i < overlaps.size(); ++i) {
      if (i > 0) ImGui::SameLine(0.0f, 8.0f);
      if (ImGui::SmallButton(overlaps[i]->name.c_str())) {
        state->cabana.chart_signal_paths = {overlaps[i]->path};
      }
    }
  }

  if (similar_bit_results_match_selection(*state) && !state->cabana.similar_bit_matches.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("Similar bits:");
    if (ImGui::BeginTable("##cabana_similar_bits", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 2.2f);
      ImGui::TableSetupColumn("Bit", ImGuiTableColumnFlags_WidthFixed, 58.0f);
      ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 58.0f);
      ImGui::TableSetupColumn("1s", ImGuiTableColumnFlags_WidthFixed, 52.0f);
      ImGui::TableSetupColumn("Flip", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableHeadersRow();
      for (const CabanaSimilarBitMatch &match : state->cabana.similar_bit_matches) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string label = match.label + "##" + match.message_root + "_" + std::to_string(match.byte_index) + "_" + std::to_string(match.bit_index);
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          state->cabana.selected_message_root = match.message_root;
          state->cabana.chart_signal_paths.clear();
          state->cabana.has_bit_selection = true;
          state->cabana.selected_bit_byte = match.byte_index;
          state->cabana.selected_bit_index = match.bit_index;
          sync_cabana_selection(session, state);
        }
        ImGui::TableNextColumn();
        ImGui::Text("B%d.%d", match.byte_index, match.bit_index);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", match.score);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", 100.0 * match.ones_ratio);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", 100.0 * match.flip_ratio);
      }
      ImGui::EndTable();
    }
  }
  ImGui::Spacing();
}

void draw_payload_bytes(std::string_view data, const std::string *prev_data = nullptr) {
  app_push_mono_font();
  for (size_t i = 0; i < data.size(); ++i) {
    if (i > 0) ImGui::SameLine(0.0f, 6.0f);
    const bool changed = prev_data != nullptr
                      && i < prev_data->size()
                      && static_cast<unsigned char>((*prev_data)[i]) != static_cast<unsigned char>(data[i]);
    if (changed) {
      ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(199, 74, 59));
    }
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned char>(data[i]));
    ImGui::TextUnformatted(hex);
    if (changed) {
      ImGui::PopStyleColor();
    }
  }
  app_pop_mono_font();
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
  const ImU32 bg = ImGui::GetColorU32(selected ? color_rgb(234, 241, 252) : color_rgb(246, 248, 250));
  const ImU32 border = ImGui::GetColorU32(selected ? color_rgb(102, 140, 214) : color_rgb(209, 214, 220));
  const ImU32 line = ImGui::GetColorU32(selected ? color_rgb(69, 137, 255) : color_rgb(111, 126, 146));
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
    const double t = kSamples == 1 ? x_min : x_min + (x_max - x_min) * static_cast<double>(i) / static_cast<double>(kSamples - 1);
    const std::optional<double> value = app_sample_xy_value_at_time(series->times, series->values, false, t);
    if (!value.has_value() || !std::isfinite(*value)) {
      continue;
    }
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
      if (point_count > 1) {
        draw->AddPolyline(points.data(), point_count, line, 0, selected ? 2.0f : 1.5f);
      }
      point_count = 0;
      continue;
    }
    const float x = left + (right - left) * static_cast<float>(i) / static_cast<float>(kSamples - 1);
    const float frac = static_cast<float>((sampled[static_cast<size_t>(i)] - y_min) / (y_max - y_min));
    const float y = bottom - (bottom - top) * std::clamp(frac, 0.0f, 1.0f);
    points[static_cast<size_t>(point_count++)] = ImVec2(x, y);
  }
  if (point_count > 1) {
    draw->AddPolyline(points.data(), point_count, line, 0, selected ? 2.0f : 1.5f);
  }

  if (state.has_tracker_time && state.tracker_time >= x_min && state.tracker_time <= x_max) {
    const float marker_x = left + (right - left) * static_cast<float>((state.tracker_time - x_min) / (x_max - x_min));
    draw->AddLine(ImVec2(marker_x, top), ImVec2(marker_x, bottom), tracker, 1.5f);
  }
}

ImU32 mix_color(ImU32 a, ImU32 b, float t) {
  const ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
  const ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::GetColorU32(ImVec4(av.x + (bv.x - av.x) * t,
                                   av.y + (bv.y - av.y) * t,
                                   av.z + (bv.z - av.z) * t,
                                   av.w + (bv.w - av.w) * t));
}

void draw_can_heatmap(const CanMessageData &message,
                      const std::vector<std::pair<const CabanaSignalSummary *, ImU32>> &highlighted,
                      double tracker_time) {
  if (message.samples.empty() || message.samples.front().data.empty()) {
    return;
  }

  app_push_bold_font();
  ImGui::TextUnformatted("History Heatmap");
  app_pop_bold_font();
  ImGui::TextDisabled("aggregated over all frames");
  ImGui::Spacing();

  const size_t byte_count = message.samples.front().data.size();
  const size_t row_count = byte_count * 8;
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float label_w = 42.0f;
  const float row_h = std::clamp(160.0f / std::max<float>(1.0f, static_cast<float>(row_count)), 10.0f, 16.0f);
  const float grid_h = row_h * static_cast<float>(row_count);
  const float grid_w = std::max(120.0f, avail_w - label_w - 8.0f);
  const int columns = std::max(1, std::min<int>(std::min<size_t>(220, message.samples.size()), static_cast<int>(grid_w / 4.0f)));
  const float cell_w = grid_w / static_cast<float>(columns);
  const size_t tracker_index = closest_can_sample_index(message, tracker_time);

  ImGui::InvisibleButton("##cabana_heatmap", ImVec2(label_w + grid_w, grid_h + 4.0f));
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  const ImVec2 grid_min(rect.Min.x + label_w, rect.Min.y);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImU32 grid_bg = ImGui::GetColorU32(color_rgb(246, 247, 249));
  const ImU32 low = ImGui::GetColorU32(color_rgb(234, 238, 243));
  const ImU32 high = ImGui::GetColorU32(color_rgb(69, 116, 201));
  const ImU32 border = ImGui::GetColorU32(color_rgb(204, 209, 214));
  draw->AddRectFilled(ImVec2(grid_min.x, rect.Min.y), ImVec2(grid_min.x + grid_w, rect.Min.y + grid_h), grid_bg, 4.0f);

  for (size_t row = 0; row < row_count; ++row) {
    const size_t byte_index = row / 8;
    const int bit_index = 7 - static_cast<int>(row % 8);
    const float y0 = rect.Min.y + static_cast<float>(row) * row_h;
    const float y1 = y0 + row_h;
    if (row % 8 == 0) {
      const std::string label = "B" + std::to_string(byte_index);
      draw->AddText(ImVec2(rect.Min.x, y0 + 1.0f), ImGui::GetColorU32(color_rgb(92, 100, 112)), label.c_str());
      if (row > 0) {
        draw->AddLine(ImVec2(rect.Min.x, y0), ImVec2(grid_min.x + grid_w, y0), border, 1.0f);
      }
    }
    for (int col = 0; col < columns; ++col) {
      const size_t start = (message.samples.size() * static_cast<size_t>(col)) / static_cast<size_t>(columns);
      const size_t end = std::max(start + 1,
                                  (message.samples.size() * static_cast<size_t>(col + 1)) / static_cast<size_t>(columns));
      size_t ones = 0;
      for (size_t i = start; i < std::min(end, message.samples.size()); ++i) {
        ones += can_bit(message.samples[i].data, byte_index, bit_index);
      }
      const float frac = static_cast<float>(ones) / static_cast<float>(std::max<size_t>(1, std::min(end, message.samples.size()) - start));
      ImU32 color = mix_color(low, high, frac);
      for (const auto &[signal, signal_color] : highlighted) {
        if (signal_contains_bit(*signal, byte_index, bit_index)) {
          color = mix_color(color, signal_color, 0.65f);
          break;
        }
      }
      const float x0 = grid_min.x + static_cast<float>(col) * cell_w;
      const float x1 = x0 + cell_w + 0.5f;
      draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
    }
  }

  const float tracker_x = grid_min.x + cell_w * ((static_cast<float>(tracker_index) + 0.5f) * static_cast<float>(columns)
                                                  / static_cast<float>(std::max<size_t>(1, message.samples.size())));
  draw->AddLine(ImVec2(tracker_x, rect.Min.y), ImVec2(tracker_x, rect.Min.y + grid_h),
                ImGui::GetColorU32(color_rgb(36, 42, 50, 0.9f)), 2.0f);
  draw->AddRect(ImVec2(grid_min.x, rect.Min.y), ImVec2(grid_min.x + grid_w, rect.Min.y + grid_h), border, 4.0f);
}

void draw_can_frame_view(const CanMessageData &message,
                         AppSession *session,
                         const CabanaMessageSummary &summary,
                         UiState *state,
                         double tracker_time) {
  if (message.samples.empty()) {
    app_push_bold_font();
    ImGui::TextUnformatted("Message View");
    app_pop_bold_font();
    ImGui::Spacing();
    ImGui::TextDisabled("No raw CAN frames available.");
    return;
  }
  const size_t sample_index = closest_can_sample_index(message, tracker_time);
  const CanFrameSample &sample = message.samples[sample_index];
  const CanFrameSample *prev = sample_index > 0 ? &message.samples[sample_index - 1] : nullptr;
  const auto highlighted = highlighted_signals(summary, *state);

  app_push_bold_font();
  ImGui::TextUnformatted("Frame View");
  app_pop_bold_font();
  ImGui::Spacing();
  ImGui::TextDisabled("tracker %.3fs", tracker_time);
  ImGui::SameLine();
  ImGui::TextDisabled("frame %.3fs", sample.mono_time);
  ImGui::SameLine();
  ImGui::TextDisabled("len %zu", sample.data.size());
  ImGui::SameLine();
  ImGui::TextDisabled("bus %u", sample.bus_time);
  if (prev != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("dt %.1f ms", 1000.0 * (sample.mono_time - prev->mono_time));
  }
  ImGui::Spacing();
  app_push_mono_font();
  ImGui::TextWrapped("%s", format_can_payload(sample.data).c_str());
  app_pop_mono_font();
  ImGui::Spacing();
  draw_signal_overlay_legend(highlighted);
  draw_bit_selection_panel(session, summary, state);

  const float table_height = std::min(170.0f, ImGui::GetContentRegionAvail().y * 0.55f);
  if (ImGui::BeginTable("##cabana_frame_bytes", 3,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingFixedFit,
                        ImVec2(0.0f, table_height))) {
    ImGui::TableSetupColumn("Byte", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < sample.data.size(); ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%zu", i);
      ImGui::TableNextColumn();
      app_push_mono_font();
      for (const auto &[signal, color] : highlighted) {
        if (signal_contains_bit(*signal, i, 0) || signal_contains_bit(*signal, i, 7)) {
          const ImVec2 min = ImGui::GetCursorScreenPos();
          const ImVec2 max = ImVec2(min.x + ImGui::GetColumnWidth(), min.y + ImGui::GetTextLineHeightWithSpacing());
          ImGui::GetWindowDrawList()->AddRectFilled(min, max, color, 3.0f);
          break;
        }
      }
      ImGui::Text("%02X", static_cast<unsigned char>(sample.data[i]));
      app_pop_mono_font();
      ImGui::TableNextColumn();
      app_push_mono_font();
      for (int bit = 7; bit >= 0; --bit) {
        const bool changed = prev != nullptr && can_bit(prev->data, i, bit) != can_bit(sample.data, i, bit);
        for (const auto &[signal, color] : highlighted) {
          if (signal_contains_bit(*signal, i, bit)) {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max = ImVec2(min.x + ImGui::CalcTextSize("0").x + 4.0f, min.y + ImGui::GetTextLineHeightWithSpacing());
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, color, 3.0f);
            break;
          }
        }
        if (changed) {
          ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(199, 74, 59));
        }
        ImGui::TextUnformatted(can_bit(sample.data, i, bit) ? "1" : "0");
        if (cabana_bit_selected(*state, i, bit)) {
          const ImVec2 min = ImGui::GetItemRectMin();
          const ImVec2 max = ImGui::GetItemRectMax();
          ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::GetColorU32(color_rgb(36, 42, 50, 0.9f)), 2.0f, 0, 2.0f);
        }
        if (changed) {
          ImGui::PopStyleColor();
        }
        if (bit > 0) ImGui::SameLine(0.0f, 6.0f);
      }
      app_pop_mono_font();
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  app_push_bold_font();
  ImGui::TextUnformatted("Bit Grid");
  app_pop_bold_font();
  if (ImGui::BeginTable("##cabana_bits", static_cast<int>(sample.data.size()) + 1,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_SizingFixedFit |
                          ImGuiTableFlags_NoHostExtendX,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("bit", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    for (size_t i = 0; i < sample.data.size(); ++i) {
      const std::string label = "B" + std::to_string(i);
      ImGui::TableSetupColumn(label.c_str(), ImGuiTableColumnFlags_WidthFixed, 34.0f);
    }
    ImGui::TableHeadersRow();
    for (int bit = 7; bit >= 0; --bit) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", bit);
      for (size_t byte = 0; byte < sample.data.size(); ++byte) {
        ImGui::TableNextColumn();
        const bool changed = prev != nullptr && can_bit(prev->data, byte, bit) != can_bit(sample.data, byte, bit);
        const float cell_w = ImGui::GetColumnWidth();
        const float cell_h = ImGui::GetTextLineHeightWithSpacing();
        ImGui::PushID(static_cast<int>(byte * 16 + static_cast<size_t>(bit)));
        ImGui::InvisibleButton("##cabana_bit_cell", ImVec2(cell_w, cell_h));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
          state->cabana.has_bit_selection = true;
          state->cabana.selected_bit_byte = static_cast<int>(byte);
          state->cabana.selected_bit_index = bit;
        }
        const ImRect cell(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        for (const auto &[signal, color] : highlighted) {
          if (signal_contains_bit(*signal, byte, bit)) {
            ImGui::GetWindowDrawList()->AddRectFilled(cell.Min, cell.Max, color);
            break;
          }
        }
        if (changed) {
          ImGui::GetWindowDrawList()->AddRect(cell.Min, cell.Max, ImGui::GetColorU32(color_rgb(199, 74, 59, 0.9f)), 0.0f, 0, 1.5f);
        }
        if (cabana_bit_selected(*state, byte, bit)) {
          ImGui::GetWindowDrawList()->AddRect(cell.Min, cell.Max, ImGui::GetColorU32(color_rgb(36, 42, 50, 0.95f)), 2.0f, 0, 2.0f);
        } else if (hovered) {
          ImGui::GetWindowDrawList()->AddRect(cell.Min, cell.Max, ImGui::GetColorU32(color_rgb(36, 42, 50, 0.35f)), 2.0f, 0, 1.0f);
        }
        app_push_mono_font();
        const char bit_text[2] = {static_cast<char>(can_bit(sample.data, byte, bit) ? '1' : '0'), '\0'};
        const ImVec2 text_size = ImGui::CalcTextSize(bit_text);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),
                                            ImGui::GetFontSize(),
                                            ImVec2(cell.Min.x + (cell_w - text_size.x) * 0.5f,
                                                   cell.Min.y + (cell_h - text_size.y) * 0.5f),
                                            ImGui::GetColorU32(color_rgb(24, 28, 34)),
                                            bit_text);
        app_pop_mono_font();
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::Spacing();
  draw_can_heatmap(message, highlighted, tracker_time);
}

void draw_empty_panel(const char *title, const char *message) {
  app_push_bold_font();
  ImGui::TextUnformatted(title);
  app_pop_bold_font();
  ImGui::Spacing();
  ImGui::TextDisabled("%s", message);
}

void draw_messages_panel(AppSession *session, UiState *state) {
  app_push_bold_font();
  ImGui::TextUnformatted("Messages");
  app_pop_bold_font();
  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##cabana_message_filter", "Filter messages...", state->cabana.message_filter.data(),
                           state->cabana.message_filter.size());
  ImGui::Spacing();

  if (session->cabana_messages.empty()) {
    ImGui::TextDisabled("No CAN messages in this route.");
    return;
  }

  if (ImGui::BeginTable("##cabana_messages", 6,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp,
                        ImGui::GetContentRegionAvail())) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Hz", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Signals", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableHeadersRow();

    const std::string filter = trim_copy(state->cabana.message_filter.data());
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(session->cabana_messages.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const CabanaMessageSummary &message = session->cabana_messages[static_cast<size_t>(i)];
        char addr_buf[16] = "--";
        if (message.has_address) {
          std::snprintf(addr_buf, sizeof(addr_buf), "0x%03X", message.address);
        }
        if (!filter.empty()
            && !contains_case_insensitive(message.name, filter)
            && !contains_case_insensitive(message.service, filter)
            && !contains_case_insensitive(addr_buf, filter)
            && !contains_case_insensitive(std::to_string(message.bus), filter)) {
          continue;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = state->cabana.selected_message_root == message.root_path;
        if (ImGui::Selectable((message.name + "##" + message.root_path).c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          state->cabana.selected_message_root = message.root_path;
          state->cabana.chart_signal_paths.clear();
          sync_cabana_selection(session, state);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(message.service.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", message.bus);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(addr_buf);
        ImGui::TableNextColumn();
        if (message.frequency_hz > 0.0) {
          ImGui::Text("%.1f", message.frequency_hz);
        } else {
          ImGui::TextDisabled("--");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%zu", message.signals.size());
      }
    }
    ImGui::EndTable();
  }
}

void draw_signal_selection_table(AppSession *session, UiState *state, const CabanaMessageSummary &message) {
  if (message.signals.empty()) {
    ImGui::TextDisabled("No decoded signals for this message.");
    return;
  }
  if (ImGui::BeginTable("##cabana_signals", 6,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_BordersInnerV,
                        ImGui::GetContentRegionAvail())) {
    ImGui::TableSetupColumn("Chart", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Trend", ImGuiTableColumnFlags_WidthFixed, 132.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableHeadersRow();
    for (const CabanaSignalSummary &signal : message.signals) {
      const bool selected = std::find(state->cabana.chart_signal_paths.begin(), state->cabana.chart_signal_paths.end(), signal.path)
                         != state->cabana.chart_signal_paths.end();
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      bool checked = selected;
      const std::string checkbox_id = "##chart_" + signal.path;
      if (ImGui::Checkbox(checkbox_id.c_str(), &checked)) {
        if (checked) {
          state->cabana.chart_signal_paths.push_back(signal.path);
        } else {
          state->cabana.chart_signal_paths.erase(
            std::remove(state->cabana.chart_signal_paths.begin(), state->cabana.chart_signal_paths.end(), signal.path),
            state->cabana.chart_signal_paths.end());
        }
      }
      ImGui::TableNextColumn();
      if (ImGui::Selectable((signal.name + "##" + signal.path).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        state->cabana.chart_signal_paths = {signal.path};
      }
      ImGui::TableNextColumn();
      if (signal.has_bit_range) {
        ImGui::TextDisabled("%d|%d", signal.start_bit, signal.size);
      } else {
        ImGui::TextDisabled("--");
      }
      ImGui::TableNextColumn();
      draw_signal_sparkline(*session, *state, signal.path, selected);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(cabana_value_label(*session, signal.path, state->tracker_time).c_str());
      ImGui::TableNextColumn();
      if (ImGui::SmallButton(("Edit##" + signal.path).c_str())) {
        open_cabana_signal_editor(*session, state, message, signal);
      }
    }
    ImGui::EndTable();
  }
}

void draw_message_history(const AppSession &session, UiState *state, const CabanaMessageSummary &message) {
  const CanMessageData *message_data = find_message_data(session, message);
  if (message_data == nullptr || message_data->samples.empty()) {
    ImGui::TextDisabled("No frame history available.");
    return;
  }

  const std::string signal_path = !state->cabana.chart_signal_paths.empty()
    ? state->cabana.chart_signal_paths.front()
    : std::string();
  const RouteSeries *series = signal_path.empty() ? nullptr : app_find_route_series(session, signal_path);
  const auto format_it = signal_path.empty() ? session.route_data.series_formats.end() : session.route_data.series_formats.find(signal_path);
  const auto enum_it = signal_path.empty() ? session.route_data.enum_info.end() : session.route_data.enum_info.find(signal_path);
  const size_t current_index = closest_can_sample_index(*message_data, state->tracker_time);

  app_push_bold_font();
  ImGui::Text("History: %s", message.name.c_str());
  app_pop_bold_font();
  ImGui::Spacing();

  const int columns = series != nullptr ? 4 : 3;
  if (ImGui::BeginTable("##cabana_history", columns,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_BordersInnerV,
                        ImGui::GetContentRegionAvail())) {
    ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("dt", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("data", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    if (series != nullptr) {
      ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 108.0f);
    }
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(message_data->samples.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const CanFrameSample &sample = message_data->samples[static_cast<size_t>(i)];
        const CanFrameSample *prev = i > 0 ? &message_data->samples[static_cast<size_t>(i - 1)] : nullptr;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = static_cast<size_t>(i) == current_index;
        char label[32];
        std::snprintf(label, sizeof(label), "%.3f##frame_%d", sample.mono_time, i);
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          state->tracker_time = sample.mono_time;
          state->has_tracker_time = true;
        }

        ImGui::TableNextColumn();
        if (prev != nullptr) {
          ImGui::Text("%.1fms", 1000.0 * (sample.mono_time - prev->mono_time));
        } else {
          ImGui::TextDisabled("--");
        }

        ImGui::TableNextColumn();
        draw_payload_bytes(sample.data, prev == nullptr ? nullptr : &prev->data);

        if (series != nullptr) {
          ImGui::TableNextColumn();
          const std::optional<double> value = app_sample_xy_value_at_time(series->times, series->values, false, sample.mono_time);
          if (value.has_value()) {
            if (format_it != session.route_data.series_formats.end()) {
              ImGui::TextUnformatted(format_display_value(*value,
                                                          format_it->second,
                                                          enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second).c_str());
            } else {
              ImGui::Text("%.4f", *value);
            }
          } else {
            ImGui::TextDisabled("--");
          }
        }
      }
    }
    ImGui::EndTable();
  }
}

void draw_detail_panel(AppSession *session, UiState *state, const CabanaMessageSummary &message, float top_height) {
  if (ImGui::BeginTabBar("##cabana_detail_tabs", ImGuiTabBarFlags_None)) {
    if (ImGui::BeginTabItem("Msg")) {
      state->cabana.detail_tab = 0;
      app_push_bold_font();
      ImGui::TextUnformatted(message.name.c_str());
      app_pop_bold_font();
      ImGui::SameLine();
      ImGui::TextDisabled("%s bus %d", message.service.c_str(), message.bus);
      if (message.has_address) {
        ImGui::SameLine();
        ImGui::TextDisabled("0x%03X", message.address);
      }
      ImGui::Spacing();
      ImGui::TextDisabled("%zu decoded signals", message.signals.size());
      const CanMessageData *message_data = find_message_data(*session, message);
      if (message_data != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("  %zu raw frames", message_data->samples.size());
      }
      ImGui::SameLine();
      if (message.frequency_hz > 0.0) {
        ImGui::TextDisabled("  %.1f Hz", message.frequency_hz);
      }
      ImGui::Spacing();
      if (ImGui::SmallButton("Export Raw CSV")) {
        fs::path output_path;
        std::string error;
        if (export_raw_can_csv(*session, message, &error, &output_path)) {
          state->status_text = "Exported raw CSV " + output_path.filename().string();
        } else {
          state->status_text = error;
        }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Export Decoded CSV")) {
        fs::path output_path;
        std::string error;
        if (export_decoded_can_csv(*session, message, &error, &output_path)) {
          state->status_text = "Exported decoded CSV " + output_path.filename().string();
        } else {
          state->status_text = error;
        }
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", cabana_export_dir().string().c_str());
      ImGui::Spacing();
      ImGui::BeginChild("##cabana_msg_top", ImVec2(0.0f, top_height), true);
      if (message_data != nullptr) {
        draw_can_frame_view(*message_data, session, message, state, state->tracker_time);
      } else {
        draw_empty_panel("Message View", "No raw CAN frames available for this message.");
      }
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::TextDisabled("Current DBC: %s", session->route_data.dbc_name.empty() ? "Auto / none" : session->route_data.dbc_name.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Edit DBC...")) {
        state->dbc_editor.open = true;
        state->dbc_editor.loaded = false;
      }
      ImGui::EndChild();
      draw_horizontal_splitter("##cabana_detail_splitter", ImGui::GetContentRegionAvail().x, kMinTopHeight,
                               std::max(kMinTopHeight, ImGui::GetContentRegionAvail().y - kMinBottomHeight),
                               &state->cabana.detail_top_height);
      ImGui::BeginChild("##cabana_signals_bottom", ImVec2(0.0f, 0.0f), true);
      draw_signal_selection_table(session, state, message);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("History")) {
      state->cabana.detail_tab = 1;
      draw_message_history(*session, state, message);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

void draw_video_panel(AppSession *session, UiState *state, float height) {
  const auto &views = camera_view_specs();
  std::vector<const CameraViewSpec *> available_views;
  available_views.reserve(views.size());
  for (const CameraViewSpec &spec : views) {
    if (!(session->route_data.*(spec.route_member)).entries.empty()) {
      available_views.push_back(&spec);
    }
  }

  app_push_bold_font();
  ImGui::TextUnformatted("Video");
  app_pop_bold_font();
  ImGui::Spacing();

  if (available_views.empty()) {
    ImGui::BeginChild("##cabana_video_empty", ImVec2(0.0f, height), true);
    ImGui::TextDisabled("No camera streams available.");
    ImGui::EndChild();
    return;
  }

  if (std::none_of(available_views.begin(), available_views.end(), [&](const CameraViewSpec *spec) {
        return spec->view == state->cabana.camera_view;
      })) {
    state->cabana.camera_view = available_views.front()->view;
  }

  if (ImGui::BeginTabBar("##cabana_video_tabs", ImGuiTabBarFlags_None)) {
    for (const CameraViewSpec *spec : available_views) {
      if (ImGui::BeginTabItem(spec->label)) {
        state->cabana.camera_view = spec->view;
        CameraFeedView *feed = session->pane_camera_feeds[static_cast<size_t>(spec->view)].get();
        if (feed != nullptr && state->has_tracker_time) {
          feed->update(state->tracker_time);
        }
        if (feed != nullptr) {
          feed->drawSized(ImVec2(ImGui::GetContentRegionAvail().x, std::max(0.0f, height - 36.0f)),
                          session->async_route_loading,
                          true);
        } else {
          ImGui::TextDisabled("Camera unavailable");
        }
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
}

void draw_chart_panel(AppSession *session, UiState *state, const CabanaMessageSummary *message) {
  app_push_bold_font();
  ImGui::TextUnformatted("Charts");
  app_pop_bold_font();
  ImGui::Spacing();
  if (message == nullptr || state->cabana.chart_signal_paths.empty()) {
    ImGui::BeginChild("##cabana_chart_empty", ImVec2(0.0f, 0.0f), true);
    ImGui::TextDisabled("Select one or more signals to chart.");
    ImGui::EndChild();
    return;
  }

  Pane pane;
  pane.kind = PaneKind::Plot;
  pane.title = message->name;
  for (const std::string &path : state->cabana.chart_signal_paths) {
    Curve curve;
    curve.name = path;
    curve.color = app_next_curve_color(pane);
    pane.curves.push_back(std::move(curve));
  }
  ImGui::BeginChild("##cabana_chart_plot", ImVec2(0.0f, 0.0f), true);
  draw_plot(*session, &pane, state);
  ImGui::EndChild();
}

}  // namespace

void rebuild_cabana_messages(AppSession *session) {
  std::vector<CabanaMessageSummary> messages;
  const std::optional<dbc::Database> db = load_active_dbc(*session);
  const DbcNameLookup dbc_lookup = build_dbc_name_lookup(db);
  std::unordered_map<std::string, std::vector<std::string>> signal_paths_by_key;

  for (const std::string &path : session->route_data.paths) {
    std::string_view service_text;
    std::string_view message_name;
    std::string_view signal_name;
    int bus = -1;
    if (!parse_can_message_path(path, &service_text, &bus, &message_name, &signal_name)) {
      continue;
    }
    const std::optional<CanServiceKind> service = parse_can_service_kind(service_text);
    if (!service.has_value()) {
      continue;
    }
    auto addr_it = dbc_lookup.unique_name_to_address.find(std::string(message_name));
    if (addr_it == dbc_lookup.unique_name_to_address.end()) {
      continue;
    }
    signal_paths_by_key[can_message_key(*service, static_cast<uint8_t>(bus), addr_it->second)].push_back(path);
  }

  messages.reserve(session->route_data.can_messages.size());
  for (const CanMessageData &message_data : session->route_data.can_messages) {
    const dbc::Message *dbc_message = db.has_value() ? db->message(message_data.id.address) : nullptr;
    const std::string key = can_message_key(message_data.id.service, message_data.id.bus, message_data.id.address);
    CabanaMessageSummary message{
      .root_path = key,
      .service = can_service_name(message_data.id.service),
      .name = dbc_message != nullptr ? dbc_message->name : format_can_address(message_data.id.address),
      .bus = static_cast<int>(message_data.id.bus),
      .address = message_data.id.address,
      .has_address = true,
      .sample_count = message_data.samples.size(),
    };
    auto signal_it = signal_paths_by_key.find(key);
    if (signal_it != signal_paths_by_key.end()) {
      std::unordered_map<std::string, const dbc::Signal *> signals_by_name;
      if (dbc_message != nullptr) {
        for (const dbc::Signal &signal : dbc_message->signals) {
          signals_by_name.emplace(signal.name, &signal);
        }
      }
      message.signals.reserve(signal_it->second.size());
      for (std::string &path : signal_it->second) {
        const size_t slash = path.find_last_of('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        CabanaSignalSummary signal_summary{
          .path = std::move(path),
          .name = name,
        };
        auto dbc_signal = signals_by_name.find(name);
        if (dbc_signal != signals_by_name.end()) {
          signal_summary.start_bit = dbc_signal->second->start_bit;
          signal_summary.msb = dbc_signal->second->msb;
          signal_summary.lsb = dbc_signal->second->lsb;
          signal_summary.size = dbc_signal->second->size;
          signal_summary.is_little_endian = dbc_signal->second->is_little_endian;
          signal_summary.has_bit_range = true;
        }
        message.signals.push_back(std::move(signal_summary));
      }
    }
    if (message_data.samples.size() > 1
        && message_data.samples.back().mono_time > message_data.samples.front().mono_time) {
      message.frequency_hz = static_cast<double>(message_data.samples.size() - 1)
                           / (message_data.samples.back().mono_time - message_data.samples.front().mono_time);
    }
    messages.push_back(std::move(message));
  }

  std::sort(messages.begin(), messages.end(), [](const CabanaMessageSummary &a, const CabanaMessageSummary &b) {
    return std::make_tuple(a.service, a.bus, a.has_address ? 0 : 1, a.address, a.name)
         < std::make_tuple(b.service, b.bus, b.has_address ? 0 : 1, b.address, b.name);
  });
  session->cabana_messages = std::move(messages);
}

void draw_cabana_mode(AppSession *session, const UiMetrics &ui, UiState *state) {
  sync_cabana_selection(session, state);

  ImGui::SetNextWindowPos(ImVec2(ui.content_x, ui.content_y));
  ImGui::SetNextWindowSize(ImVec2(ui.content_w, ui.content_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(244, 246, 248));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(186, 191, 198));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##cabana_mode_host", nullptr, flags)) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float messages_width = std::clamp(state->cabana.messages_width, kMinMessagesWidth,
                                      std::max(kMinMessagesWidth, avail.x - kMinCenterWidth - kMinRightWidth - 2.0f * kSplitterThickness));
    float right_width = std::clamp(state->cabana.right_width, kMinRightWidth,
                                   std::max(kMinRightWidth, avail.x - messages_width - kMinCenterWidth - 2.0f * kSplitterThickness));
    const float max_messages = std::max(kMinMessagesWidth, avail.x - right_width - kMinCenterWidth - 2.0f * kSplitterThickness);
    messages_width = std::min(messages_width, max_messages);
    const float center_width = std::max(kMinCenterWidth, avail.x - messages_width - right_width - 2.0f * kSplitterThickness);
    right_width = avail.x - messages_width - center_width - 2.0f * kSplitterThickness;
    state->cabana.messages_width = messages_width;
    state->cabana.right_width = right_width;

    ImGui::BeginChild("##cabana_messages_panel", ImVec2(messages_width, avail.y), true);
    draw_messages_panel(session, state);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    draw_vertical_splitter("##cabana_left_splitter", avail.y, kMinMessagesWidth,
                           std::max(kMinMessagesWidth, avail.x - kMinCenterWidth - right_width - 2.0f * kSplitterThickness),
                           &state->cabana.messages_width);
    ImGui::SameLine(0.0f, 0.0f);

    const CabanaMessageSummary *message = find_selected_message(*session, *state);
    const float center_height = avail.y;
    ImGui::BeginChild("##cabana_detail_panel", ImVec2(center_width, center_height), true);
    if (message == nullptr) {
      draw_empty_panel("Detail", "Select a decoded CAN message from the left panel.");
    } else {
      state->cabana.detail_top_height = std::clamp(state->cabana.detail_top_height, kMinTopHeight,
                                                   std::max(kMinTopHeight, ImGui::GetContentRegionAvail().y - kMinBottomHeight - kSplitterThickness));
      draw_detail_panel(session, state, *message, state->cabana.detail_top_height);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    draw_right_splitter("##cabana_right_splitter", avail.y, kMinRightWidth,
                        std::max(kMinRightWidth, avail.x - state->cabana.messages_width - kMinCenterWidth - 2.0f * kSplitterThickness),
                        &state->cabana.right_width);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginChild("##cabana_right_panel", ImVec2(0.0f, center_height), true);
    const float right_avail_y = ImGui::GetContentRegionAvail().y;
    state->cabana.right_top_height = std::clamp(state->cabana.right_top_height, kMinTopHeight,
                                                std::max(kMinTopHeight, right_avail_y - kMinBottomHeight - kSplitterThickness));
    draw_video_panel(session, state, state->cabana.right_top_height);
    draw_horizontal_splitter("##cabana_right_hsplit", ImGui::GetContentRegionAvail().x, kMinTopHeight,
                             std::max(kMinTopHeight, ImGui::GetContentRegionAvail().y - kMinBottomHeight),
                             &state->cabana.right_top_height);
    draw_chart_panel(session, state, message);
    ImGui::EndChild();
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}
