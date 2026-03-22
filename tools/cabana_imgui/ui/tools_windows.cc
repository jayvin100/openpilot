#include "ui/tools_windows.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
#include "core/command_stack.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"

namespace cabana {
namespace tools_windows {

namespace {

struct MessageOption {
  MessageId id = {};
  std::string label;
  int data_size = 0;
};

struct SimilarBitMatch {
  MessageId id = {};
  uint32_t byte_idx = 0;
  uint32_t bit_idx = 0;
  uint32_t mismatches = 0;
  uint32_t total = 0;
  float perc = 0.0f;
};

struct FindSimilarBitsState {
  bool open = false;
  int src_bus = -1;
  int find_bus = -1;
  bool has_selected_message = false;
  MessageId selected_message = {};
  int byte_idx = 0;
  int bit_idx = 0;
  bool equal = true;
  int min_msgs = 100;
  int selected_result = -1;
  std::vector<int> buses;
  std::vector<MessageOption> source_messages;
  std::vector<SimilarBitMatch> results;
  std::string status;
  std::string error;
};

constexpr size_t kFilterBufferSize = 128;

struct FindSignalResult {
  MessageId id = {};
  uint64_t mono_time = 0;
  cabana::dbc::Signal sig = {};
  int data_size = 0;
  std::vector<std::string> matches;
};

struct FindSignalState {
  bool open = false;
  bool focus_value_input = false;
  int compare_mode = 0;
  int min_size = 8;
  int max_size = 8;
  bool little_endian = true;
  bool is_signed = false;
  int selected_result = -1;
  uint64_t last_time = std::numeric_limits<uint64_t>::max();
  std::vector<FindSignalResult> initial_results;
  std::vector<std::vector<FindSignalResult>> histories;
  std::vector<FindSignalResult> results;
  char bus_filter[kFilterBufferSize] = {};
  char address_filter[kFilterBufferSize] = {};
  char first_time[kFilterBufferSize] = "0";
  char last_time_text[kFilterBufferSize] = "MAX";
  char factor[kFilterBufferSize] = "1.0";
  char offset[kFilterBufferSize] = "0.0";
  char value1[kFilterBufferSize] = "0";
  char value2[kFilterBufferSize] = {};
  std::string status;
  std::string error;
};

FindSignalState g_find_signal;
FindSimilarBitsState g_find_similar_bits;
bool g_route_info_open = false;

std::string trim_copy(const char *text) {
  std::string value = text ? text : "";
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

void copy_text(char *buffer, size_t size, const std::string &value) {
  std::snprintf(buffer, size, "%s", value.c_str());
}

std::string format_double(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.6g", value);
  return buffer;
}

double find_signal_default_max(int size) {
  return std::pow(2.0, std::min(size, 30)) - 1.0;
}

bool parse_csv_integers(const char *text, int base, std::set<int> &out, std::string &error) {
  out.clear();
  const std::string trimmed = trim_copy(text);
  if (trimmed.empty()) {
    return true;
  }

  std::stringstream stream(trimmed);
  std::string token;
  while (std::getline(stream, token, ',')) {
    const std::string piece = trim_copy(token.c_str());
    if (piece.empty()) continue;

    std::string parse_text = piece;
    if (base == 16 && parse_text.size() > 2 &&
        parse_text[0] == '0' && (parse_text[1] == 'x' || parse_text[1] == 'X')) {
      parse_text = parse_text.substr(2);
    }

    char *end = nullptr;
    long value = std::strtol(parse_text.c_str(), &end, base);
    if (!end || *end != '\0') {
      error = "Invalid filter list.";
      return false;
    }
    out.insert((int)value);
  }
  return true;
}

bool parse_time_value(const char *text, bool allow_max, double &out, std::string &error) {
  const std::string trimmed = trim_copy(text);
  if (trimmed.empty()) {
    out = allow_max ? std::numeric_limits<double>::infinity() : 0.0;
    return true;
  }
  if (allow_max && (trimmed == "MAX" || trimmed == "max")) {
    out = std::numeric_limits<double>::infinity();
    return true;
  }

  char *end = nullptr;
  const double value = std::strtod(trimmed.c_str(), &end);
  if (!end || *end != '\0') {
    error = "Invalid time range.";
    return false;
  }
  out = value;
  return true;
}

std::string result_label(const FindSignalResult &result) {
  return result.matches.empty() ? std::string() : result.matches.back();
}

std::vector<int> available_buses(cabana::Source *src) {
  std::set<int> buses;
  if (!src) return {};

  for (const auto &[id, _] : src->messages()) {
    if (id.source < 64) {
      buses.insert(id.source);
    }
  }
  for (const auto &[id, _] : src->eventsMap()) {
    if (id.source < 64) {
      buses.insert(id.source);
    }
  }
  return {buses.begin(), buses.end()};
}

bool contains_bus(const std::vector<int> &buses, int bus) {
  return std::find(buses.begin(), buses.end(), bus) != buses.end();
}

std::string message_label(const MessageId &id) {
  char addr[32];
  std::snprintf(addr, sizeof(addr), "0x%X", id.address);
  if (const char *name = cabana::dbc::dbc_manager().msgName(id)) {
    return std::string(name) + " (" + addr + ")";
  }
  return addr;
}

std::vector<MessageOption> source_messages_for_bus(cabana::Source *src, int bus) {
  std::map<MessageId, int> sizes;
  std::vector<MessageOption> options;
  if (!src || bus < 0) return options;

  for (const auto &[id, live] : src->messages()) {
    if (id.source == bus) {
      sizes[id] = std::max(sizes[id], (int)live.dat.size());
    }
  }
  for (const auto &[id, events] : src->eventsMap()) {
    if (id.source == bus && !events.empty()) {
      sizes[id] = std::max(sizes[id], (int)events.back()->size);
    }
  }

  options.reserve(sizes.size());
  for (const auto &[id, size] : sizes) {
    options.push_back({.id = id, .label = message_label(id), .data_size = size});
  }
  std::sort(options.begin(), options.end(), [](const MessageOption &a, const MessageOption &b) {
    if (a.label != b.label) return a.label < b.label;
    return a.id < b.id;
  });
  return options;
}

bool contains_message(const std::vector<MessageOption> &messages, const MessageId &id) {
  return std::any_of(messages.begin(), messages.end(), [&](const auto &message) {
    return message.id == id;
  });
}

const MessageOption *selected_message_option(const FindSimilarBitsState &state) {
  auto it = std::find_if(state.source_messages.begin(), state.source_messages.end(), [&](const auto &message) {
    return message.id == state.selected_message;
  });
  return it == state.source_messages.end() ? nullptr : &*it;
}

void refresh_find_similar_bits_state(cabana::Source *src) {
  auto &state = g_find_similar_bits;
  state.buses = available_buses(src);
  if (state.buses.empty()) {
    state.src_bus = -1;
    state.find_bus = -1;
    state.source_messages.clear();
    state.has_selected_message = false;
    return;
  }

  const auto &app_st = cabana::app_state();
  const int preferred_bus = app_st.has_selection ? app_st.selected_msg.source : state.buses.front();
  if (!contains_bus(state.buses, state.src_bus)) {
    state.src_bus = contains_bus(state.buses, preferred_bus) ? preferred_bus : state.buses.front();
  }
  if (!contains_bus(state.buses, state.find_bus)) {
    state.find_bus = state.buses.size() > 1 && state.buses.front() == state.src_bus ? state.buses[1] : state.buses.front();
  }

  state.source_messages = source_messages_for_bus(src, state.src_bus);
  if (state.source_messages.empty()) {
    state.has_selected_message = false;
    return;
  }

  if (!state.has_selected_message || !contains_message(state.source_messages, state.selected_message)) {
    if (app_st.has_selection && app_st.selected_msg.source == state.src_bus &&
        contains_message(state.source_messages, app_st.selected_msg)) {
      state.selected_message = app_st.selected_msg;
    } else {
      state.selected_message = state.source_messages.front().id;
    }
    state.has_selected_message = true;
  }

  const auto *message = selected_message_option(state);
  const int max_byte = message ? std::max(0, message->data_size - 1) : 0;
  state.byte_idx = std::clamp(state.byte_idx, 0, max_byte);
  state.bit_idx = std::clamp(state.bit_idx, 0, 7);
  state.min_msgs = std::max(1, state.min_msgs);
}

void open_message(const MessageId &id) {
  cabana::app_state().setSelectedMessage(id);
}

void reset_find_signal_results(FindSignalState &state) {
  state.initial_results.clear();
  state.histories.clear();
  state.results.clear();
  state.selected_result = -1;
  state.last_time = std::numeric_limits<uint64_t>::max();
  state.status.clear();
  state.error.clear();
}

void seed_find_signal_filters_from_selection(FindSignalState &state) {
  auto &app_st = cabana::app_state();
  if (app_st.has_selection) {
    copy_text(state.bus_filter, sizeof(state.bus_filter), std::to_string(app_st.selected_msg.source));
    char address[32];
    std::snprintf(address, sizeof(address), "0x%X", app_st.selected_msg.address);
    copy_text(state.address_filter, sizeof(state.address_filter), address);
  } else {
    state.bus_filter[0] = '\0';
    state.address_filter[0] = '\0';
  }
  copy_text(state.first_time, sizeof(state.first_time), "0");
  copy_text(state.last_time_text, sizeof(state.last_time_text), "MAX");
  copy_text(state.factor, sizeof(state.factor), "1.0");
  copy_text(state.offset, sizeof(state.offset), "0.0");
  copy_text(state.value1, sizeof(state.value1), "0");
  state.value2[0] = '\0';
  state.compare_mode = 0;
  state.min_size = 8;
  state.max_size = 8;
  state.little_endian = true;
  state.is_signed = false;
  state.focus_value_input = true;
}

bool initialize_find_signal_results(FindSignalState &state, cabana::Source *src) {
  state.initial_results.clear();
  state.last_time = std::numeric_limits<uint64_t>::max();

  std::set<int> bus_filters;
  std::set<int> address_filters;
  if (!parse_csv_integers(state.bus_filter, 10, bus_filters, state.error)) {
    return false;
  }
  if (!parse_csv_integers(state.address_filter, 16, address_filters, state.error)) {
    return false;
  }

  double first_sec = 0.0;
  double last_sec = std::numeric_limits<double>::infinity();
  if (!parse_time_value(state.first_time, false, first_sec, state.error) ||
      !parse_time_value(state.last_time_text, true, last_sec, state.error)) {
    return false;
  }
  if (std::isfinite(last_sec) && last_sec < first_sec) {
    std::swap(first_sec, last_sec);
  }

  double factor = 1.0;
  double offset = 0.0;
  if (!parse_time_value(state.factor, false, factor, state.error) ||
      !parse_time_value(state.offset, false, offset, state.error)) {
    state.error = "Invalid factor or offset.";
    return false;
  }

  const int min_size = std::clamp(std::min(state.min_size, state.max_size), 1, 64);
  const int max_size = std::clamp(std::max(state.min_size, state.max_size), 1, 64);
  state.min_size = min_size;
  state.max_size = max_size;

  const uint64_t first_time = src->routeStartNanos() + (uint64_t)(std::max(0.0, first_sec) * 1e9);
  if (std::isfinite(last_sec)) {
    state.last_time = src->routeStartNanos() + (uint64_t)(std::max(0.0, last_sec) * 1e9);
  }

  for (const auto &[id, events] : src->eventsMap()) {
    if (events.empty()) continue;
    if (!bus_filters.empty() && bus_filters.count(id.source) == 0) continue;
    if (!address_filters.empty() && address_filters.count((int)id.address) == 0) continue;

    auto event = std::lower_bound(events.begin(), events.end(), first_time, CompareCanEvent());
    if (event == events.end()) continue;

    const int total_bits = (*event)->size * 8;
    for (int size = min_size; size <= max_size; ++size) {
      if (size > total_bits) break;
      for (int start = 0; start <= total_bits - size; ++start) {
        FindSignalResult result;
        result.id = id;
        result.mono_time = first_time;
        result.data_size = (*event)->size;
        result.sig.start_bit = start;
        result.sig.size = size;
        result.sig.is_little_endian = state.little_endian;
        result.sig.is_signed = state.is_signed;
        result.sig.factor = factor;
        result.sig.offset = offset;
        state.initial_results.push_back(std::move(result));
      }
    }
  }

  if (state.initial_results.empty()) {
    state.error = "No candidate signals matched the current filters.";
    return false;
  }
  return true;
}

bool build_find_signal_compare(const FindSignalState &state,
                               std::function<bool(double)> &cmp,
                               std::string &error) {
  double value1 = 0.0;
  double value2 = 0.0;
  if (!parse_time_value(state.value1, false, value1, error)) {
    error = "Invalid search value.";
    return false;
  }
  if (state.compare_mode == 6 && !parse_time_value(state.value2, false, value2, error)) {
    error = "Invalid upper bound.";
    return false;
  }

  switch (state.compare_mode) {
    case 0: cmp = [value1](double value) { return value == value1; }; break;
    case 1: cmp = [value1](double value) { return value > value1; }; break;
    case 2: cmp = [value1](double value) { return value >= value1; }; break;
    case 3: cmp = [value1](double value) { return value != value1; }; break;
    case 4: cmp = [value1](double value) { return value < value1; }; break;
    case 5: cmp = [value1](double value) { return value <= value1; }; break;
    case 6:
      if (value2 < value1) std::swap(value1, value2);
      cmp = [value1, value2](double value) { return value >= value1 && value <= value2; };
      break;
    default:
      error = "Unknown comparison mode.";
      return false;
  }
  return true;
}

void run_find_signal_search() {
  auto &state = g_find_signal;
  state.error.clear();

  auto *application = app();
  auto *src = application ? application->source() : nullptr;
  if (!src) {
    state.error = "Open a route replay before searching.";
    return;
  }

  src->mergeAllSegments();
  if (state.histories.empty() && !initialize_find_signal_results(state, src)) {
    state.results.clear();
    state.selected_result = -1;
    return;
  }

  std::function<bool(double)> cmp;
  if (!build_find_signal_compare(state, cmp, state.error)) {
    return;
  }

  const auto &previous = state.histories.empty() ? state.initial_results : state.histories.back();
  state.results.clear();
  state.results.reserve(previous.size());

  for (const auto &result : previous) {
    const auto *events = src->events(result.id);
    if (!events || events->empty()) {
      continue;
    }

    auto first = std::upper_bound(events->begin(), events->end(), result.mono_time, CompareCanEvent());
    auto last = events->end();
    if (state.last_time < std::numeric_limits<uint64_t>::max()) {
      last = std::upper_bound(events->begin(), events->end(), state.last_time, CompareCanEvent());
    }

    auto match = std::find_if(first, last, [&](const CanEvent *event) {
      return cmp(result.sig.getValue(event->dat, event->size));
    });
    if (match == last) {
      continue;
    }

    FindSignalResult next = result;
    next.mono_time = (*match)->mono_time;
    next.data_size = (*match)->size;
    next.matches.push_back("(" + format_double(src->toSeconds((*match)->mono_time)) + ", " +
                           format_double(result.sig.getValue((*match)->dat, (*match)->size)) + ")");
    state.results.push_back(std::move(next));
  }

  state.histories.push_back(state.results);
  state.selected_result = state.results.empty() ? -1 : 0;
  state.status = std::to_string(state.results.size()) + " matches";
}

void undo_find_signal_search() {
  auto &state = g_find_signal;
  if (state.histories.empty()) return;

  state.histories.pop_back();
  state.results = state.histories.empty() ? std::vector<FindSignalResult>{} : state.histories.back();
  state.selected_result = state.results.empty() ? -1 : 0;
  state.status = state.results.empty() ? "0 matches" : std::to_string(state.results.size()) + " matches";
  state.error.clear();
}

void create_signal_from_find_result() {
  auto &state = g_find_signal;
  if (state.selected_result < 0 || state.selected_result >= (int)state.results.size()) {
    return;
  }

  auto result = state.results[state.selected_result];
  auto *application = app();
  auto *src = application ? application->source() : nullptr;
  if (!src) {
    state.error = "Open a route replay before creating a signal.";
    return;
  }

  auto &dbc_mgr = cabana::dbc::dbc_manager();
  auto &app_st = cabana::app_state();
  const auto before_dbc = dbc_mgr.captureSnapshot();
  const auto before_app = app_st.captureEditSnapshot();

  result.sig.name = dbc_mgr.nextSignalName(result.id);
  result.sig.max = find_signal_default_max(result.sig.size);

  bool ok = true;
  if (!dbc_mgr.msg(result.id)) {
    ok = dbc_mgr.updateMessage(result.id, dbc_mgr.nextMessageName(result.id),
                               result.data_size > 0 ? result.data_size : 8, "XXX", "");
  }
  if (ok) {
    ok = dbc_mgr.addSignal(result.id, result.sig);
  }
  if (!ok) {
    state.error = "Failed to create signal from search result.";
    return;
  }

  open_message(result.id);
  app_st.setCurrentDetailTab(DetailTab::Signals);
  app_st.setBitSelection(result.sig.start_bit, result.sig.size, result.sig.is_little_endian);
  pushSnapshotCommand("Find Signal Create", before_dbc, before_app,
                      dbc_mgr.captureSnapshot(), app_st.captureEditSnapshot());
  state.status = "Created signal " + result.sig.name;
  state.error.clear();
}

void run_find_similar_bits_search() {
  auto &state = g_find_similar_bits;
  state.results.clear();
  state.selected_result = -1;
  state.error.clear();
  state.status.clear();

  auto *application = app();
  auto *src = application ? application->source() : nullptr;
  if (!src) {
    state.error = "Open a route replay before searching.";
    return;
  }

  src->mergeAllSegments();
  refresh_find_similar_bits_state(src);
  if (!state.has_selected_message) {
    state.error = "No source message is available on the selected bus.";
    return;
  }

  const auto *selected = selected_message_option(state);
  if (!selected || selected->data_size <= state.byte_idx) {
    state.error = "Selected byte index is out of range for the chosen message.";
    return;
  }

  std::unordered_map<uint32_t, std::vector<uint32_t>> mismatches;
  std::unordered_map<uint32_t, uint32_t> msg_count;
  int bit_to_find = -1;

  for (const CanEvent *event : src->allEvents()) {
    if (!event) continue;

    if (event->src == state.src_bus &&
        event->address == state.selected_message.address &&
        event->size > state.byte_idx) {
      bit_to_find = ((event->dat[state.byte_idx] >> (7 - state.bit_idx)) & 1U) != 0;
    }

    if (event->src != state.find_bus) {
      continue;
    }

    ++msg_count[event->address];
    if (bit_to_find == -1) {
      continue;
    }

    auto &mismatch_counts = mismatches[event->address];
    if (mismatch_counts.size() < event->size * 8) {
      mismatch_counts.resize(event->size * 8);
    }

    for (int byte = 0; byte < event->size; ++byte) {
      for (int bit = 0; bit < 8; ++bit) {
        const bool bit_value = ((event->dat[byte] >> (7 - bit)) & 1U) != 0;
        mismatch_counts[byte * 8 + bit] += state.equal ? (bit_value != bit_to_find) : (bit_value == bit_to_find);
      }
    }
  }

  state.results.reserve(mismatches.size());
  for (const auto &[address, mismatch_counts] : mismatches) {
    const uint32_t total = msg_count[address];
    if (total <= (uint32_t)state.min_msgs) {
      continue;
    }
    for (uint32_t index = 0; index < mismatch_counts.size(); ++index) {
      const float perc = (mismatch_counts[index] / (double)total) * 100.0f;
      if (perc < 50.0f) {
        state.results.push_back({
          .id = {.source = (uint8_t)state.find_bus, .address = address},
          .byte_idx = index / 8,
          .bit_idx = index % 8,
          .mismatches = mismatch_counts[index],
          .total = total,
          .perc = perc,
        });
      }
    }
  }

  std::sort(state.results.begin(), state.results.end(), [](const SimilarBitMatch &left, const SimilarBitMatch &right) {
    if (left.perc != right.perc) return left.perc < right.perc;
    if (left.id.address != right.id.address) return left.id.address < right.id.address;
    if (left.byte_idx != right.byte_idx) return left.byte_idx < right.byte_idx;
    return left.bit_idx < right.bit_idx;
  });

  if (!state.results.empty()) {
    state.selected_result = 0;
  }

  char summary[128];
  std::snprintf(summary, sizeof(summary), "%zu matches on bus %d", state.results.size(), state.find_bus);
  state.status = summary;
}

void render_find_signal() {
  auto &state = g_find_signal;
  if (!state.open) {
    return;
  }

  auto *application = app();
  auto *src = application ? application->source() : nullptr;

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(980, 700), ImGuiCond_Appearing);

  if (!ImGui::Begin("Find Signal", &state.open,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (!src) {
    ImGui::TextDisabled("Open a route replay before searching for signals.");
    ImGui::End();
    return;
  }

  if (state.focus_value_input) {
    ImGui::SetKeyboardFocusHere();
    state.focus_value_input = false;
  }

  if (ImGui::BeginTable("##find_signal_inputs", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("Messages");
    ImGui::InputTextWithHint("Bus", "comma-separated buses or blank", state.bus_filter, sizeof(state.bus_filter));
    ImGui::InputTextWithHint("Address", "comma-separated hex addresses or blank",
                             state.address_filter, sizeof(state.address_filter));
    ImGui::InputText("First Time", state.first_time, sizeof(state.first_time));
    ImGui::InputText("Last Time", state.last_time_text, sizeof(state.last_time_text));

    ImGui::TableNextColumn();
    ImGui::TextDisabled("Signal");
    ImGui::InputInt("Min Size", &state.min_size);
    ImGui::InputInt("Max Size", &state.max_size);
    state.min_size = std::clamp(state.min_size, 1, 64);
    state.max_size = std::clamp(state.max_size, 1, 64);
    ImGui::Checkbox("Little Endian", &state.little_endian);
    ImGui::SameLine();
    ImGui::Checkbox("Signed", &state.is_signed);
    ImGui::InputText("Factor", state.factor, sizeof(state.factor));
    ImGui::InputText("Offset", state.offset, sizeof(state.offset));
    ImGui::EndTable();
  }

  ImGui::Separator();
  ImGui::TextDisabled("Find Signal");
  static const char *compare_labels[] = {"=", ">", ">=", "!=", "<", "<=", "between"};
  ImGui::SetNextItemWidth(100.0f);
  ImGui::Combo("Compare", &state.compare_mode, compare_labels, IM_ARRAYSIZE(compare_labels));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120.0f);
  ImGui::InputText("Value 1", state.value1, sizeof(state.value1));
  if (state.compare_mode == 6) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("Value 2", state.value2, sizeof(state.value2));
  }

  const bool has_results = state.selected_result >= 0 && state.selected_result < (int)state.results.size();
  const char *find_label = state.histories.empty() ? "Find" : "Find Next";
  if (ImGui::Button(find_label, ImVec2(120, 0))) {
    run_find_signal_search();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(state.histories.empty());
  if (ImGui::Button("Undo Prev Find", ImVec2(140, 0))) {
    undo_find_signal_search();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(state.histories.empty());
  if (ImGui::Button("Reset", ImVec2(96, 0))) {
    reset_find_signal_results(state);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!has_results);
  if (ImGui::Button("Open Message", ImVec2(130, 0)) && has_results) {
    open_message(state.results[state.selected_result].id);
  }
  ImGui::SameLine();
  if (ImGui::Button("Create Signal", ImVec2(130, 0)) && has_results) {
    create_signal_from_find_result();
  }
  ImGui::EndDisabled();

  if (!state.status.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.status.c_str());
  }
  if (!state.error.empty()) {
    ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", state.error.c_str());
  }

  ImGui::Separator();
  if (state.results.empty()) {
    ImGui::TextDisabled("Run a search to populate results.");
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("##find_signal_results", 5,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Start Bit", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Endian", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Matches", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    const int row_count = std::min<int>(state.results.size(), 300);
    ImGuiListClipper clipper;
    clipper.Begin(row_count);
    while (clipper.Step()) {
      for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
        const auto &result = state.results[index];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string id_label = result.id.toString();
        if (ImGui::Selectable(id_label.c_str(), state.selected_result == index, ImGuiSelectableFlags_AllowDoubleClick)) {
          state.selected_result = index;
          if (ImGui::IsMouseDoubleClicked(0)) {
            open_message(result.id);
          }
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", result.sig.start_bit);
        ImGui::TableNextColumn();
        ImGui::Text("%d", result.sig.size);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(result.sig.is_little_endian ? "LE" : "BE");
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", result_label(result).c_str());
      }
    }
    ImGui::EndTable();
  }

  ImGui::End();
}

void render_find_similar_bits() {
  auto &state = g_find_similar_bits;
  if (!state.open) {
    return;
  }

  auto *application = app();
  auto *src = application ? application->source() : nullptr;
  refresh_find_similar_bits_state(src);

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(860, 560), ImGuiCond_Appearing);

  if (!ImGui::Begin("Find Similar Bits", &state.open,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (!src) {
    ImGui::TextDisabled("Open a route replay to search for similar bits.");
    ImGui::End();
    return;
  }

  if (state.buses.empty()) {
    ImGui::TextDisabled("Waiting for indexed CAN buses...");
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("##similar_bits_controls", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("Find From");

    if (ImGui::BeginCombo("Source Bus", std::to_string(state.src_bus).c_str())) {
      for (int bus : state.buses) {
        const bool selected = state.src_bus == bus;
        if (ImGui::Selectable(std::to_string(bus).c_str(), selected)) {
          state.src_bus = bus;
          state.source_messages = source_messages_for_bus(src, state.src_bus);
          state.has_selected_message = false;
          refresh_find_similar_bits_state(src);
        }
      }
      ImGui::EndCombo();
    }

    const std::string selected_message_label = state.has_selected_message ? message_label(state.selected_message)
                                                                          : std::string("No message");
    if (ImGui::BeginCombo("Message", selected_message_label.c_str())) {
      for (const auto &message : state.source_messages) {
        const bool selected = state.selected_message == message.id;
        if (ImGui::Selectable(message.label.c_str(), selected)) {
          state.selected_message = message.id;
          state.has_selected_message = true;
          state.byte_idx = std::clamp(state.byte_idx, 0, std::max(0, message.data_size - 1));
        }
      }
      ImGui::EndCombo();
    }

    const auto *selected = selected_message_option(state);
    ImGui::InputInt("Byte Index", &state.byte_idx);
    ImGui::InputInt("Bit Index", &state.bit_idx);
    const int max_byte = selected ? std::max(0, selected->data_size - 1) : 0;
    state.byte_idx = std::clamp(state.byte_idx, 0, max_byte);
    state.bit_idx = std::clamp(state.bit_idx, 0, 7);
    if (selected) {
      ImGui::TextDisabled("Message width: %d bytes", selected->data_size);
    }

    ImGui::TableNextColumn();
    ImGui::TextDisabled("Find In");

    if (ImGui::BeginCombo("Target Bus", std::to_string(state.find_bus).c_str())) {
      for (int bus : state.buses) {
        const bool bus_selected = state.find_bus == bus;
        if (ImGui::Selectable(std::to_string(bus).c_str(), bus_selected)) {
          state.find_bus = bus;
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Checkbox("Equal", &state.equal);
    ImGui::InputInt("Min Msg Count", &state.min_msgs);
    state.min_msgs = std::max(1, state.min_msgs);

    ImGui::EndTable();
  }

  const bool can_open = state.selected_result >= 0 && state.selected_result < (int)state.results.size();
  ImGui::SetCursorPosX(18.0f);
  if (ImGui::Button("Find", ImVec2(120, 0))) {
    run_find_similar_bits_search();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_open);
  if (ImGui::Button("Open Message", ImVec2(140, 0)) && can_open) {
    open_message(state.results[state.selected_result].id);
  }
  ImGui::EndDisabled();
  if (!state.status.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.status.c_str());
  }
  if (!state.error.empty()) {
    ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", state.error.c_str());
  }

  ImGui::Separator();
  if (state.results.empty()) {
    ImGui::TextDisabled("Run a search to populate results.");
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("##similar_bits_results", 7,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Byte", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Bit", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Mismatches", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("% Mismatch", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Open", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin((int)state.results.size());
    while (clipper.Step()) {
      for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
        const auto &result = state.results[index];
        char address[32];
        std::snprintf(address, sizeof(address), "0x%X", result.id.address);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(address, state.selected_result == index, ImGuiSelectableFlags_AllowDoubleClick)) {
          state.selected_result = index;
          if (ImGui::IsMouseDoubleClicked(0)) {
            open_message(result.id);
          }
        }

        ImGui::TableNextColumn();
        ImGui::Text("%u", result.byte_idx);
        ImGui::TableNextColumn();
        ImGui::Text("%u", result.bit_idx);
        ImGui::TableNextColumn();
        ImGui::Text("%u", result.mismatches);
        ImGui::TableNextColumn();
        ImGui::Text("%u", result.total);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", result.perc);
        ImGui::TableNextColumn();
        ImGui::PushID(index);
        if (ImGui::SmallButton("Open")) {
          state.selected_result = index;
          open_message(result.id);
        }
        ImGui::PopID();
      }
    }

    ImGui::EndTable();
  }

  ImGui::End();
}

std::string format_duration(double seconds) {
  const int total = std::max(0, (int)std::round(seconds));
  const int hours = total / 3600;
  const int minutes = (total % 3600) / 60;
  const int secs = total % 60;
  char buf[64];
  if (hours > 0) {
    std::snprintf(buf, sizeof(buf), "%dh %02dm %02ds", hours, minutes, secs);
  } else {
    std::snprintf(buf, sizeof(buf), "%dm %02ds", minutes, secs);
  }
  return buf;
}

void info_row(const char *label, const std::string &value) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextDisabled("%s", label);
  ImGui::TableNextColumn();
  ImGui::TextWrapped("%s", value.c_str());
}

void render_route_info() {
  if (!g_route_info_open) {
    return;
  }

  auto *application = app();
  auto *src = application ? application->source() : nullptr;
  auto &st = cabana::app_state();
  auto &dbc = cabana::dbc::dbc_manager();

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_Appearing);

  if (!ImGui::Begin("Route Info", &g_route_info_open,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("##route_info_table", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
    info_row("Route", st.route_name.empty() ? "No route loaded" : st.route_name);
    info_row("Fingerprint", st.car_fingerprint.empty() ? "Unknown" : st.car_fingerprint);
    info_row("Load State", st.route_loading ? "Loading" :
                           !st.route_load_error.empty() ? ("Failed: " + st.route_load_error) :
                           src ? "Loaded" : "Idle");
    info_row("Playback", src ? (st.paused ? "Paused" : "Playing") : "No stream");
    info_row("Current Time", src ? format_duration(st.current_sec) : "0m 00s");
    info_row("Duration", src ? format_duration(st.max_sec - st.min_sec) : "0m 00s");
    info_row("Indexed CAN Events", src ? std::to_string(src->allEvents().size()) : "0");
    info_row("Indexed Messages", src ? std::to_string(src->eventsMap().size()) : "0");
    info_row("Live Messages", src ? std::to_string(src->messages().size()) : "0");
    info_row("DBC Messages", std::to_string(dbc.msgCount()));
    info_row("DBC Signals", std::to_string(dbc.signalCount()));
    info_row("Video", application && application->videoEnabled() ? "Enabled" : "Disabled");
    info_row("Active DBC", st.active_dbc_file.empty() ? "None" : st.active_dbc_file);
    ImGui::EndTable();
  }

  ImGui::Spacing();
  if (ImGui::Button("Close", ImVec2(96, 0))) {
    g_route_info_open = false;
  }

  ImGui::End();
}

}  // namespace

void requestFindSignal() {
  reset_find_signal_results(g_find_signal);
  g_find_signal.open = true;
  seed_find_signal_filters_from_selection(g_find_signal);
  if (auto *application = app(); application && application->source()) {
    application->source()->mergeAllSegments();
  }
}

void requestFindSimilarBits() {
  g_find_similar_bits.open = true;
  g_find_similar_bits.results.clear();
  g_find_similar_bits.status.clear();
  g_find_similar_bits.error.clear();
  g_find_similar_bits.selected_result = -1;
  if (auto *application = app(); application && application->source()) {
    application->source()->mergeAllSegments();
    refresh_find_similar_bits_state(application->source());
  }
}

void requestRouteInfo() {
  g_route_info_open = true;
  if (auto *application = app(); application && application->source()) {
    application->source()->mergeAllSegments();
  }
}

void render() {
  render_find_signal();
  render_find_similar_bits();
  render_route_info();
}

}  // namespace tools_windows
}  // namespace cabana
