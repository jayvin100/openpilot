#include "ui/panes/detail_pane.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
#include "core/command_stack.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"
#include "ui/theme.h"

namespace cabana {
namespace panes {

namespace {

constexpr int kMaxDataBytes = 64;
constexpr float kBitButtonWidth = 20.0f;
constexpr float kBitButtonHeight = 22.0f;
constexpr size_t kNameBufferSize = 256;
constexpr size_t kTextBufferSize = 512;
constexpr size_t kCommentBufferSize = 2048;

struct MessageEditorState {
  bool request_open = false;
  bool focus_name = false;
  MessageId msg_id = {};
  bool existing = false;
  int size = 8;
  char name[kNameBufferSize] = {};
  char transmitter[kTextBufferSize] = {};
  char comment[kCommentBufferSize] = {};
  std::string error;
};

struct SignalEditorState {
  enum class Mode {
    Add = 0,
    Edit,
  };

  bool request_open = false;
  bool focus_name = false;
  Mode mode = Mode::Add;
  MessageId msg_id = {};
  std::string original_name;
  int max_bits = 8;
  int start_bit = 0;
  int size = 1;
  bool is_little_endian = true;
  bool is_signed = false;
  double factor = 1.0;
  double offset = 0.0;
  double min = 0.0;
  double max = 1.0;
  char name[kNameBufferSize] = {};
  char unit[kTextBufferSize] = {};
  char comment[kCommentBufferSize] = {};
  std::string error;
};

MessageEditorState g_message_editor;
SignalEditorState g_signal_editor;

template <typename Fn>
bool apply_dbc_edit_command(const std::string &label, Fn &&fn) {
  auto &dbc_mgr = cabana::dbc::dbc_manager();
  auto &st = cabana::app_state();
  const auto before_dbc = dbc_mgr.captureSnapshot();
  const auto before_app = st.captureEditSnapshot();
  if (!fn()) {
    return false;
  }
  cabana::pushSnapshotCommand(label, before_dbc, before_app, dbc_mgr.captureSnapshot(), st.captureEditSnapshot());
  return true;
}

void copy_to_buffer(char *dst, size_t dst_size, const std::string &src) {
  std::snprintf(dst, dst_size, "%s", src.c_str());
}

std::string trim_copy(const char *text) {
  std::string value = text ? text : "";
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

bool valid_identifier(const std::string &value) {
  if (value.empty()) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = value[i];
    if (i == 0) {
      if (!(std::isalpha(c) || c == '_')) return false;
    } else if (!(std::isalnum(c) || c == '_')) {
      return false;
    }
  }
  return true;
}

int flip_bit_pos(int start_bit) {
  return 8 * (start_bit / 8) + 7 - start_bit % 8;
}

int big_endian_bit_position(int start_bit, int offset) {
  return flip_bit_pos(flip_bit_pos(start_bit) + offset);
}

int signal_highest_bit(const cabana::dbc::Signal &signal) {
  int highest = signal.start_bit;
  for (int i = 1; i < signal.size; ++i) {
    const int bit = signal.is_little_endian ? signal.start_bit + i : big_endian_bit_position(signal.start_bit, i);
    highest = std::max(highest, bit);
  }
  return highest;
}

bool signal_contains_bit(const cabana::dbc::Signal &signal, int bit) {
  if (signal.size <= 0) return false;
  if (signal.is_little_endian) {
    return bit >= signal.start_bit && bit < signal.start_bit + signal.size;
  }
  for (int i = 0; i < signal.size; ++i) {
    if (big_endian_bit_position(signal.start_bit, i) == bit) {
      return true;
    }
  }
  return false;
}

bool selection_contains_bit(const AppState &st, int bit) {
  return st.has_bit_selection &&
         bit >= st.bit_selection_start &&
         bit < st.bit_selection_start + st.bit_selection_size;
}

std::vector<const cabana::dbc::Signal *> signals_for_bit(const cabana::dbc::Message *msg, int bit) {
  std::vector<const cabana::dbc::Signal *> matches;
  if (!msg) return matches;
  for (const auto &signal : msg->signals) {
    if (signal_contains_bit(signal, bit)) {
      matches.push_back(&signal);
    }
  }
  return matches;
}

double default_signal_max(int size) {
  if (size <= 0) return 1.0;
  if (size >= 31) return 2147483647.0;
  return static_cast<double>((1u << size) - 1u);
}

void request_message_editor(const MessageId &msg_id, const MsgLiveData &live) {
  auto &dbc_mgr = cabana::dbc::dbc_manager();
  auto *msg = dbc_mgr.msg(msg_id);
  g_message_editor = {};
  g_message_editor.request_open = true;
  g_message_editor.focus_name = true;
  g_message_editor.msg_id = msg_id;
  g_message_editor.existing = msg != nullptr;
  g_message_editor.size = msg ? static_cast<int>(msg->size) : std::max(1, (int)live.dat.size());
  copy_to_buffer(g_message_editor.name, sizeof(g_message_editor.name),
                 msg ? msg->name : dbc_mgr.nextMessageName(msg_id));
  copy_to_buffer(g_message_editor.transmitter, sizeof(g_message_editor.transmitter),
                 msg ? msg->transmitter : "XXX");
  copy_to_buffer(g_message_editor.comment, sizeof(g_message_editor.comment),
                 msg ? msg->comment : "");
}

void request_signal_editor_add(const MessageId &msg_id, int data_size) {
  auto &st = cabana::app_state();
  auto &dbc_mgr = cabana::dbc::dbc_manager();
  auto *msg = dbc_mgr.msg(msg_id);

  g_signal_editor = {};
  g_signal_editor.request_open = true;
  g_signal_editor.focus_name = true;
  g_signal_editor.mode = SignalEditorState::Mode::Add;
  g_signal_editor.msg_id = msg_id;
  g_signal_editor.max_bits = std::max(1, (msg ? (int)msg->size : data_size) * 8);
  g_signal_editor.start_bit = st.has_bit_selection ? st.bit_selection_start : 0;
  g_signal_editor.size = st.has_bit_selection ? st.bit_selection_size : 1;
  g_signal_editor.is_little_endian = st.has_bit_selection ? st.bit_selection_little_endian : true;
  g_signal_editor.max = default_signal_max(g_signal_editor.size);
  copy_to_buffer(g_signal_editor.name, sizeof(g_signal_editor.name),
                 dbc_mgr.nextSignalName(msg_id));
}

void request_signal_editor_edit(const MessageId &msg_id, const cabana::dbc::Signal &signal, int data_size) {
  auto &st = cabana::app_state();
  g_signal_editor = {};
  g_signal_editor.request_open = true;
  g_signal_editor.focus_name = true;
  g_signal_editor.mode = SignalEditorState::Mode::Edit;
  g_signal_editor.msg_id = msg_id;
  g_signal_editor.original_name = signal.name;
  g_signal_editor.max_bits = std::max(1, data_size * 8);
  g_signal_editor.start_bit = signal.start_bit;
  g_signal_editor.size = signal.size;
  g_signal_editor.is_little_endian = signal.is_little_endian;
  g_signal_editor.is_signed = signal.is_signed;
  g_signal_editor.factor = signal.factor;
  g_signal_editor.offset = signal.offset;
  g_signal_editor.min = signal.min;
  g_signal_editor.max = signal.max;
  copy_to_buffer(g_signal_editor.name, sizeof(g_signal_editor.name), signal.name);
  copy_to_buffer(g_signal_editor.unit, sizeof(g_signal_editor.unit), signal.unit);
  copy_to_buffer(g_signal_editor.comment, sizeof(g_signal_editor.comment), signal.comment);
  st.setBitSelection(signal.start_bit, signal.size, signal.is_little_endian);
}

bool submit_message_editor() {
  auto &dbc_mgr = cabana::dbc::dbc_manager();
  const std::string name = trim_copy(g_message_editor.name);
  const std::string transmitter = trim_copy(g_message_editor.transmitter);
  const std::string comment = trim_copy(g_message_editor.comment);

  g_message_editor.error.clear();
  if (!valid_identifier(name)) {
    g_message_editor.error = "Message name must be a valid DBC identifier.";
    return false;
  }
  if (!transmitter.empty() && !valid_identifier(transmitter)) {
    g_message_editor.error = "Node must be empty or a valid DBC identifier.";
    return false;
  }
  if (dbc_mgr.messageNameExists(name, g_message_editor.msg_id, g_message_editor.msg_id.address)) {
    g_message_editor.error = "Message name already exists in the loaded DBC.";
    return false;
  }
  if (g_message_editor.size < 1 || g_message_editor.size > kMaxDataBytes) {
    g_message_editor.error = "Message size must be between 1 and 64 bytes.";
    return false;
  }

  if (const auto *msg = dbc_mgr.msg(g_message_editor.msg_id)) {
    int required_bits = 0;
    for (const auto &signal : msg->signals) {
      required_bits = std::max(required_bits, signal_highest_bit(signal) + 1);
    }
    if (required_bits > g_message_editor.size * 8) {
      g_message_editor.error = "Message size is smaller than one or more existing signals.";
      return false;
    }
  }

  const char *label = g_message_editor.existing ? "Edit Message" : "Create Message";
  if (!apply_dbc_edit_command(label, [&]() {
        return dbc_mgr.updateMessage(g_message_editor.msg_id, name, g_message_editor.size,
                                     transmitter, comment);
      })) {
    g_message_editor.error = "Failed to update the message definition.";
    return false;
  }
  return true;
}

bool submit_signal_editor() {
  auto &dbc_mgr = cabana::dbc::dbc_manager();
  auto *msg = dbc_mgr.msg(g_signal_editor.msg_id);
  const std::string name = trim_copy(g_signal_editor.name);
  const std::string unit = trim_copy(g_signal_editor.unit);
  const std::string comment = trim_copy(g_signal_editor.comment);

  g_signal_editor.error.clear();
  if (!msg) {
    g_signal_editor.error = "Create a message definition before adding signals.";
    return false;
  }
  if (!valid_identifier(name)) {
    g_signal_editor.error = "Signal name must be a valid DBC identifier.";
    return false;
  }
  for (const auto &signal : msg->signals) {
    if (signal.name == name &&
        !(g_signal_editor.mode == SignalEditorState::Mode::Edit &&
          signal.name == g_signal_editor.original_name)) {
      g_signal_editor.error = "Signal name already exists in this message.";
      return false;
    }
  }
  if (g_signal_editor.start_bit < 0 || g_signal_editor.start_bit >= g_signal_editor.max_bits) {
    g_signal_editor.error = "Start bit is out of range for the message size.";
    return false;
  }
  if (g_signal_editor.size < 1 || g_signal_editor.size > g_signal_editor.max_bits) {
    g_signal_editor.error = "Signal size must be between 1 and the message bit width.";
    return false;
  }
  if (g_signal_editor.is_little_endian &&
      g_signal_editor.start_bit + g_signal_editor.size > g_signal_editor.max_bits) {
    g_signal_editor.error = "Little-endian signal extends past the end of the message.";
    return false;
  }
  if (!g_signal_editor.is_little_endian &&
      signal_highest_bit({
        .name = name,
        .start_bit = g_signal_editor.start_bit,
        .size = g_signal_editor.size,
        .is_little_endian = false,
      }) >= g_signal_editor.max_bits) {
    g_signal_editor.error = "Big-endian signal extends past the end of the message.";
    return false;
  }
  if (g_signal_editor.min > g_signal_editor.max) {
    g_signal_editor.error = "Minimum must be less than or equal to maximum.";
    return false;
  }

  cabana::dbc::Signal signal;
  signal.name = name;
  signal.start_bit = g_signal_editor.start_bit;
  signal.size = g_signal_editor.size;
  signal.is_little_endian = g_signal_editor.is_little_endian;
  signal.is_signed = g_signal_editor.is_signed;
  signal.factor = g_signal_editor.factor;
  signal.offset = g_signal_editor.offset;
  signal.min = g_signal_editor.min;
  signal.max = g_signal_editor.max;
  signal.unit = unit;
  signal.comment = comment;

  const char *label = g_signal_editor.mode == SignalEditorState::Mode::Add ? "Add Signal" : "Edit Signal";
  if (!apply_dbc_edit_command(label, [&]() {
        bool ok = false;
        if (g_signal_editor.mode == SignalEditorState::Mode::Add) {
          ok = dbc_mgr.addSignal(g_signal_editor.msg_id, signal);
        } else {
          ok = dbc_mgr.updateSignal(g_signal_editor.msg_id, g_signal_editor.original_name, signal);
        }
        if (!ok) {
          return false;
        }

        auto &st = cabana::app_state();
        if (g_signal_editor.mode == SignalEditorState::Mode::Edit) {
          st.renameChartSignal(g_signal_editor.msg_id, g_signal_editor.original_name, signal.name);
        }
        st.setBitSelection(signal.start_bit, signal.size, signal.is_little_endian);
        return true;
      })) {
    g_signal_editor.error = "Failed to update the signal definition.";
    return false;
  }
  return true;
}

void render_message_editor_popup() {
  if (g_message_editor.request_open) {
    ImGui::OpenPopup(g_message_editor.existing ? "Edit Message Definition" : "Create Message Definition");
    g_message_editor.request_open = false;
  }

  const char *title = g_message_editor.existing ? "Edit Message Definition" : "Create Message Definition";
  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(title, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }

  ImGui::TextDisabled("0x%X on bus %d", g_message_editor.msg_id.address, g_message_editor.msg_id.source);
  ImGui::Spacing();
  if (g_message_editor.focus_name) {
    ImGui::SetKeyboardFocusHere();
    g_message_editor.focus_name = false;
  }
  const bool submit_from_name = ImGui::InputText("Name", g_message_editor.name, sizeof(g_message_editor.name),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::InputInt("Size", &g_message_editor.size);
  ImGui::InputText("Node", g_message_editor.transmitter, sizeof(g_message_editor.transmitter));
  ImGui::InputTextMultiline("Comment", g_message_editor.comment, sizeof(g_message_editor.comment),
                            ImVec2(420, 96));
  if (!g_message_editor.error.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", g_message_editor.error.c_str());
  }

  ImGui::Spacing();
  if (ImGui::Button(g_message_editor.existing ? "Save Message" : "Create Message", ImVec2(120, 0)) ||
      submit_from_name) {
    if (submit_message_editor()) {
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) {
    g_message_editor.error.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void render_signal_editor_popup() {
  if (g_signal_editor.request_open) {
    ImGui::OpenPopup(g_signal_editor.mode == SignalEditorState::Mode::Add ? "Add Signal Definition"
                                                                          : "Edit Signal Definition");
    g_signal_editor.request_open = false;
  }

  const char *title = g_signal_editor.mode == SignalEditorState::Mode::Add ? "Add Signal Definition"
                                                                            : "Edit Signal Definition";
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(title, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }

  ImGui::TextDisabled("0x%X on bus %d", g_signal_editor.msg_id.address, g_signal_editor.msg_id.source);
  ImGui::Spacing();
  if (g_signal_editor.focus_name) {
    ImGui::SetKeyboardFocusHere();
    g_signal_editor.focus_name = false;
  }
  const bool submit_from_name = ImGui::InputText("Name", g_signal_editor.name, sizeof(g_signal_editor.name),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::InputInt("Start Bit", &g_signal_editor.start_bit);
  ImGui::InputInt("Size", &g_signal_editor.size);
  ImGui::Checkbox("Little Endian", &g_signal_editor.is_little_endian);
  ImGui::SameLine();
  ImGui::Checkbox("Signed", &g_signal_editor.is_signed);
  ImGui::InputDouble("Factor", &g_signal_editor.factor, 0.1, 1.0, "%.6g");
  ImGui::InputDouble("Offset", &g_signal_editor.offset, 0.1, 1.0, "%.6g");
  ImGui::InputDouble("Min", &g_signal_editor.min, 0.1, 1.0, "%.6g");
  ImGui::InputDouble("Max", &g_signal_editor.max, 0.1, 1.0, "%.6g");
  ImGui::InputText("Unit", g_signal_editor.unit, sizeof(g_signal_editor.unit));
  ImGui::InputTextMultiline("Comment", g_signal_editor.comment, sizeof(g_signal_editor.comment),
                            ImVec2(460, 96));
  if (!g_signal_editor.error.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", g_signal_editor.error.c_str());
  }

  ImGui::Spacing();
  if (ImGui::Button(g_signal_editor.mode == SignalEditorState::Mode::Add ? "Add Signal" : "Save Signal",
                    ImVec2(110, 0)) || submit_from_name) {
    if (submit_signal_editor()) {
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) {
    g_signal_editor.error.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void render_splash() {
  auto &st = cabana::app_state();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float cx = avail.x * 0.5f;
  float cy = avail.y * 0.38f;

  ImFont *splash = cabana::theme::font_splash();
  if (splash) {
    const char *text = "CABANA";
    ImGui::PushFont(splash, 52.0f * cabana::theme::dpi_scale());
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImGui::SetCursorPos(ImVec2(cx - sz.x * 0.5f, cy));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.58f, 0.58f, 0.62f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  ImGui::Spacing();
  {
    const char *sub = st.route_loading ? "Loading route data..." :
                     !st.route_load_error.empty() ? "Route load failed" :
                     "Select a message to view bytes, signals, and history";
    float sw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX(cx - sw * 0.5f);
    ImGui::TextDisabled("%s", sub);
  }
  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  auto shortcut = [&](const char *label, const char *key) {
    float lw = ImGui::CalcTextSize(label).x;
    float kw = ImGui::CalcTextSize(key).x + ImGui::GetStyle().FramePadding.x * 2;
    float total = lw + 8 + kw;
    ImGui::SetCursorPosX(cx - total * 0.5f);
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::SmallButton(key);
  };
  shortcut("Pause", "Space");
  shortcut("Help", "F1");
  shortcut("What's This", "Shift+F1");
}

ImU32 byte_color(uint8_t val) {
  if (val == 0) return IM_COL32(40, 40, 40, 255);
  float t = val / 255.0f;
  uint8_t r = (uint8_t)(60 + 180 * t);
  uint8_t g = (uint8_t)(80 + 100 * (1.0f - t));
  uint8_t b = (uint8_t)(120 + 80 * t);
  return IM_COL32(r, g, b, 200);
}

void render_binary_view(const MessageId &id, const uint8_t *data, int data_size) {
  auto &st = cabana::app_state();
  auto *msg = cabana::dbc::dbc_manager().msg(id);

  if (msg) {
    if (st.has_bit_selection) {
      ImGui::TextDisabled("Selected bits [%d|%d] %s", st.bit_selection_start, st.bit_selection_size,
                          st.bit_selection_little_endian ? "LE" : "BE");
      ImGui::SameLine();
      if (ImGui::Button("Add Signal From Selection")) {
        request_signal_editor_add(id, data_size);
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear Selection")) {
        st.clearBitSelection();
      }
    } else {
      ImGui::TextDisabled("Click a bit to begin a selection. Shift-click extends the range.");
    }
  } else {
    ImGui::TextDisabled("Create a message definition before adding signals.");
  }

  ImGui::Spacing();

  if (ImGui::BeginTable("##binary", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Byte", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (int byte_idx = 0; byte_idx < data_size; ++byte_idx) {
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      ImGui::Text("%d", byte_idx);

      ImGui::TableNextColumn();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const float cell_w = 56.0f;
      const float cell_h = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
      dl->AddRectFilled(pos, ImVec2(pos.x + cell_w, pos.y + cell_h), byte_color(data[byte_idx]), 3.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 235, 235, 255));
      ImGui::Text(" %02X ", data[byte_idx]);
      ImGui::PopStyleColor();

      ImGui::TableNextColumn();
      for (int bit_in_byte = 7; bit_in_byte >= 0; --bit_in_byte) {
        const int raw_bit = byte_idx * 8 + bit_in_byte;
        const bool bit_value = ((data[byte_idx] >> bit_in_byte) & 1) != 0;
        const bool selected = selection_contains_bit(st, raw_bit);
        const auto covering = signals_for_bit(msg, raw_bit);
        const bool has_signal = !covering.empty();

        ImGui::PushID(raw_bit);
        if (selected) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.72f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.50f, 0.78f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.46f, 0.74f, 1.0f));
        } else if (has_signal) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.27f, 0.34f, 0.95f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.33f, 0.40f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.29f, 0.36f, 1.0f));
        }

        if (ImGui::Button(bit_value ? "1" : "0", ImVec2(kBitButtonWidth, kBitButtonHeight))) {
          if (ImGui::GetIO().KeyShift && st.has_bit_selection) {
            st.extendBitSelection(raw_bit);
          } else {
            st.setBitSelection(raw_bit, 1, true);
          }
        }

        if (selected || has_signal) {
          ImGui::PopStyleColor(3);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text("Bit %d", raw_bit);
          if (has_signal) {
            ImGui::Separator();
            for (const auto *signal : covering) {
              ImGui::TextUnformatted(signal->name.c_str());
            }
          }
          ImGui::EndTooltip();
        }
        ImGui::PopID();
        if (bit_in_byte > 0) {
          ImGui::SameLine();
        }
      }
    }

    ImGui::EndTable();
  }
}

void render_signal_list(const MessageId &id, const uint8_t *data, int data_size) {
  auto *msg = cabana::dbc::dbc_manager().msg(id);
  auto &st = cabana::app_state();
  if (!msg || msg->signals.empty()) {
    if (msg) {
      if (ImGui::Button(st.has_bit_selection ? "Add Signal From Selection" : "Add Signal")) {
        request_signal_editor_add(id, data_size);
      }
      if (st.has_bit_selection) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%d|%d]", st.bit_selection_start, st.bit_selection_size);
      }
    } else {
      ImGui::TextDisabled("No message definition loaded for this stream message");
    }
    return;
  }

  if (ImGui::Button(st.has_bit_selection ? "Add Signal From Selection" : "Add Signal")) {
    request_signal_editor_add(id, data_size);
  }
  if (st.has_bit_selection) {
    ImGui::SameLine();
    ImGui::TextDisabled("[%d|%d] %s", st.bit_selection_start, st.bit_selection_size,
                        st.bit_selection_little_endian ? "LE" : "BE");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%d Signals", (int)msg->signals.size());
  ImGui::Separator();

  std::string signal_to_remove;
  if (ImGui::BeginTable("##signals", 7,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    for (const auto &sig : msg->signals) {
      double val = sig.getValue(data, data_size);

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(sig.name.c_str(), false)) {
        st.setBitSelection(sig.start_bit, sig.size, sig.is_little_endian);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to highlight this signal in the binary view");
      }

      ImGui::TableNextColumn();
      char vbuf[64];
      if (sig.factor == 1.0 && sig.offset == 0.0) {
        std::snprintf(vbuf, sizeof(vbuf), "%.0f", val);
      } else {
        std::snprintf(vbuf, sizeof(vbuf), "%.2f", val);
      }
      ImGui::TextUnformatted(vbuf);

      ImGui::TableNextColumn();
      if (!sig.unit.empty()) {
        ImGui::TextDisabled("%s", sig.unit.c_str());
      }

      ImGui::TableNextColumn();
      ImGui::TextDisabled("[%d|%d]", sig.start_bit, sig.size);

      ImGui::TableNextColumn();
      bool plotted = st.hasChartSignal(id, sig.name);
      std::string plot_id = "##plot_" + sig.name;
      if (ImGui::Checkbox(plot_id.c_str(), &plotted)) {
        if (plotted) {
          st.addSignalToCharts(id, sig.name, ImGui::GetIO().KeyShift);
        } else {
          st.removeSignalFromCharts(id, sig.name);
        }
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", plotted ? "Close Plot" : "Show Plot\nShift-click to add to selected chart");
      }

      ImGui::TableNextColumn();
      std::string edit_id = "Edit##" + sig.name;
      if (ImGui::SmallButton(edit_id.c_str())) {
        request_signal_editor_edit(id, sig, data_size);
      }

      ImGui::TableNextColumn();
      std::string remove_id = "Delete##" + sig.name;
      if (ImGui::SmallButton(remove_id.c_str())) {
        signal_to_remove = sig.name;
      }
    }

    ImGui::EndTable();
  }

  if (!signal_to_remove.empty()) {
    apply_dbc_edit_command("Remove Signal", [&]() {
      if (!cabana::dbc::dbc_manager().removeSignal(id, signal_to_remove)) {
        return false;
      }
      st.removeSignalFromCharts(id, signal_to_remove);
      if (st.has_bit_selection) {
        st.clearBitSelection();
      }
      return true;
    });
  }
}

std::string format_signal_value(const cabana::dbc::Signal &sig, double value) {
  char buf[64];
  if (sig.factor == 1.0 && sig.offset == 0.0) {
    std::snprintf(buf, sizeof(buf), "%.0f", value);
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f", value);
  }
  return buf;
}

std::string format_hex_data(const uint8_t *data, int data_size) {
  std::string hex;
  hex.reserve(data_size * 3);
  char buf[4];
  for (int i = 0; i < data_size; ++i) {
    if (i > 0) hex.push_back(' ');
    std::snprintf(buf, sizeof(buf), "%02X", data[i]);
    hex += buf;
  }
  return hex;
}

void render_history_view(const MessageId &id, cabana::ReplaySource *src) {
  auto it = src->eventsMap().find(id);
  if (it == src->eventsMap().end() || it->second.empty()) {
    ImGui::TextDisabled("Waiting for indexed CAN events...");
    return;
  }

  const auto *dbc_msg = cabana::dbc::dbc_manager().msg(id);
  const bool has_signals = dbc_msg && !dbc_msg->signals.empty();
  static bool hex_mode = false;
  if (has_signals) {
    ImGui::Checkbox("Hex", &hex_mode);
    ImGui::SameLine();
  }
  const int row_count = std::min<size_t>(it->second.size(), 250);
  ImGui::TextDisabled("Latest %d / %zu events", row_count, it->second.size());
  ImGui::Separator();

  const bool show_hex = hex_mode || !has_signals;
  const int column_count = show_hex ? 2 : 1 + (int)dbc_msg->signals.size();
  if (ImGui::BeginTable("##history", column_count,
                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    if (show_hex) {
      ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch);
    } else {
      for (const auto &sig : dbc_msg->signals) {
        std::string label = sig.unit.empty() ? sig.name : (sig.name + " (" + sig.unit + ")");
        ImGui::TableSetupColumn(label.c_str(), ImGuiTableColumnFlags_WidthFixed, 96.0f);
      }
    }
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(row_count);
    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        const auto *e = it->second[it->second.size() - 1 - row];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", src->replay()->toSeconds(e->mono_time));

        if (show_hex) {
          ImGui::TableNextColumn();
          const std::string hex = format_hex_data(e->dat, e->size);
          ImGui::TextUnformatted(hex.c_str());
        } else {
          for (const auto &sig : dbc_msg->signals) {
            ImGui::TableNextColumn();
            const double value = sig.getValue(e->dat, e->size);
            const std::string formatted = format_signal_value(sig, value);
            ImGui::TextUnformatted(formatted.c_str());
          }
        }
      }
    }

    ImGui::EndTable();
  }
}

void render_detail_tab_button(const char *label, DetailTab tab, AppState &st) {
  const bool selected = st.current_detail_tab == tab;
  ImGui::PushStyleColor(ImGuiCol_Header, selected ? ImVec4(0.17f, 0.30f, 0.45f, 0.95f)
                                                  : ImVec4(0.18f, 0.18f, 0.18f, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.36f, 0.50f, 0.95f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.33f, 0.48f, 1.0f));
  if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0, 22.0f))) {
    st.setCurrentDetailTab(tab);
  }
  ImGui::PopStyleColor(3);
}

}  // namespace

bool canEditSelectedMessage() {
  auto &st = cabana::app_state();
  return st.has_selection && app() && app()->source() &&
         app()->source()->messages().count(st.selected_msg) != 0;
}

bool canAddSignalToSelectedMessage() {
  if (!canEditSelectedMessage()) return false;
  return cabana::dbc::dbc_manager().msg(cabana::app_state().selected_msg) != nullptr;
}

void requestEditSelectedMessage() {
  if (!canEditSelectedMessage()) return;
  auto &st = cabana::app_state();
  const auto &messages = app()->source()->messages();
  auto it = messages.find(st.selected_msg);
  if (it == messages.end()) return;
  request_message_editor(st.selected_msg, it->second);
}

void requestAddSignalForSelectedMessage() {
  if (!canAddSignalToSelectedMessage()) return;
  auto &st = cabana::app_state();
  const auto &messages = app()->source()->messages();
  auto it = messages.find(st.selected_msg);
  if (it == messages.end()) return;
  request_signal_editor_add(st.selected_msg, (int)it->second.dat.size());
}

void detail() {
  ImGui::Begin("Detail");

  auto &st = cabana::app_state();

  if (!st.has_selection) {
    render_splash();
    render_message_editor_popup();
    render_signal_editor_popup();
    ImGui::End();
    return;
  }

  auto *src = app() ? app()->source() : nullptr;
  if (!src) {
    render_splash();
    render_message_editor_popup();
    render_signal_editor_popup();
    ImGui::End();
    return;
  }

  const auto &msgs = src->messages();
  auto it = msgs.find(st.selected_msg);
  if (it == msgs.end()) {
    ImGui::TextDisabled("Message not found in stream");
    render_message_editor_popup();
    render_signal_editor_popup();
    ImGui::End();
    return;
  }

  const auto &id = st.selected_msg;
  const auto &live = it->second;
  const uint8_t *data = live.dat.empty() ? nullptr : live.dat.data();
  const int data_size = (int)live.dat.size();

  auto *dbc_msg = cabana::dbc::dbc_manager().msg(id);

  if (dbc_msg) {
    ImFont *bold = cabana::theme::font_bold();
    if (bold) ImGui::PushFont(bold, 0);
    ImGui::Text("%s", dbc_msg->name.c_str());
    if (bold) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("(0x%X)", id.address);
  } else {
    ImGui::Text("0x%X", id.address);
  }
  ImGui::SameLine(ImGui::GetWindowWidth() - 120);
  ImGui::TextDisabled("Bus: %d  Count: %u", id.source, live.count);

  if (dbc_msg && !dbc_msg->transmitter.empty()) {
    ImGui::TextDisabled("Node: %s", dbc_msg->transmitter.c_str());
    if (!dbc_msg->comment.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("| %s", dbc_msg->comment.c_str());
    }
  } else if (!dbc_msg) {
    ImGui::TextDisabled("No DBC definition for this message yet");
  }

  bool remove_message = false;
  if (ImGui::Button(dbc_msg ? "Edit Message" : "Create Message")) {
    request_message_editor(id, live);
  }
  ImGui::SameLine();
  if (!dbc_msg) ImGui::BeginDisabled();
  if (ImGui::Button("Remove Message")) {
    remove_message = true;
  }
  if (!dbc_msg) ImGui::EndDisabled();

  if (remove_message) {
    if (apply_dbc_edit_command("Remove Message", [&]() {
          if (!cabana::dbc::dbc_manager().removeMessage(id)) {
            return false;
          }
          st.removeChartsForMessage(id);
          st.clearBitSelection();
          return true;
        })) {
      dbc_msg = cabana::dbc::dbc_manager().msg(id);
    }
  }

  ImGui::Separator();

  ImGui::BeginGroup();
  render_detail_tab_button("Binary", DetailTab::Binary, st);
  ImGui::SameLine();
  render_detail_tab_button("Signals", DetailTab::Signals, st);
  ImGui::SameLine();
  render_detail_tab_button("History", DetailTab::History, st);
  ImGui::EndGroup();
  ImGui::Separator();

  switch (st.current_detail_tab) {
    case DetailTab::Binary:
      if (data && data_size > 0) {
        render_binary_view(id, data, data_size);
      } else {
        ImGui::TextDisabled("No data");
      }
      ImGui::Spacing();
      ImGui::Separator();
      if (data && data_size > 0) {
        render_signal_list(id, data, data_size);
      }
      break;
    case DetailTab::Signals:
      if (data && data_size > 0) {
        render_signal_list(id, data, data_size);
      } else {
        ImGui::TextDisabled("No data");
      }
      break;
    case DetailTab::History:
      render_history_view(id, src);
      break;
  }

  render_message_editor_popup();
  render_signal_editor_popup();
  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
