#include "ui/tools_windows.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
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

FindSimilarBitsState g_find_similar_bits;
bool g_route_info_open = false;

std::vector<int> available_buses(cabana::ReplaySource *src) {
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

std::vector<MessageOption> source_messages_for_bus(cabana::ReplaySource *src, int bus) {
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

void refresh_find_similar_bits_state(cabana::ReplaySource *src) {
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
  render_find_similar_bits();
  render_route_info();
}

}  // namespace tools_windows
}  // namespace cabana
