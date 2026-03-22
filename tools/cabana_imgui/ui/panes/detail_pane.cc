#include "ui/panes/detail_pane.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "app/application.h"
#include "core/app_state.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"
#include "ui/theme.h"

namespace cabana {
namespace panes {

static void render_splash() {
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
  ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

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

// Color for a byte value (simple heat map)
static ImU32 byte_color(uint8_t val) {
  if (val == 0) return IM_COL32(40, 40, 40, 255);
  float t = val / 255.0f;
  uint8_t r = (uint8_t)(60 + 180 * t);
  uint8_t g = (uint8_t)(80 + 100 * (1.0f - t));
  uint8_t b = (uint8_t)(120 + 80 * t);
  return IM_COL32(r, g, b, 200);
}

static void render_binary_view(const MessageId &id, const uint8_t *data, int data_size) {
  // Header row with byte column numbers
  ImGui::Text("    ");
  for (int col = 0; col < 8 && col < data_size; col++) {
    ImGui::SameLine();
    ImGui::Text("  %d  ", col);
  }

  // Binary grid — one row per byte showing hex value with colored background
  float cell_w = ImGui::CalcTextSize(" FF ").x + 4;
  float cell_h = ImGui::GetTextLineHeightWithSpacing() + 2;

  int num_rows = (data_size + 7) / 8;
  if (num_rows > 8) num_rows = 8;  // cap for display

  ImDrawList *dl = ImGui::GetWindowDrawList();

  for (int row = 0; row < num_rows; row++) {
    ImGui::Text(" %d  ", row);
    for (int col = 0; col < 8; col++) {
      int byte_idx = row * 8 + col;
      if (byte_idx >= data_size) break;

      ImGui::SameLine();
      uint8_t val = data[byte_idx];

      // Draw colored cell background
      ImVec2 pos = ImGui::GetCursorScreenPos();
      dl->AddRectFilled(pos, ImVec2(pos.x + cell_w, pos.y + cell_h - 2), byte_color(val), 2.0f);

      // Draw hex text
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 220, 255));
      ImGui::Text(" %02X ", val);
      ImGui::PopStyleColor();
    }
  }
}

static void render_signal_list(const MessageId &id, const uint8_t *data, int data_size) {
  auto *msg = cabana::dbc::dbc_manager().msg(id.address);
  if (!msg || msg->signals.empty()) {
    ImGui::TextDisabled("No signals defined for this message");
    return;
  }

  ImGui::Text("Signals: %d", (int)msg->signals.size());
  ImGui::Separator();

  if (ImGui::BeginTable("##signals", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableHeadersRow();

    for (const auto &sig : msg->signals) {
      double val = sig.getValue(data, data_size);
      auto &st = cabana::app_state();

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(sig.name.c_str());
      ImGui::TableNextColumn();

      // Format value
      char vbuf[64];
      if (sig.factor == 1.0 && sig.offset == 0.0) {
        snprintf(vbuf, sizeof(vbuf), "%.0f", val);
      } else {
        snprintf(vbuf, sizeof(vbuf), "%.2f", val);
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
    }

    ImGui::EndTable();
  }
}

static std::string format_signal_value(const cabana::dbc::Signal &sig, double value) {
  char buf[64];
  if (sig.factor == 1.0 && sig.offset == 0.0) {
    snprintf(buf, sizeof(buf), "%.0f", value);
  } else {
    snprintf(buf, sizeof(buf), "%.2f", value);
  }
  return buf;
}

static std::string format_hex_data(const uint8_t *data, int data_size) {
  std::string hex;
  hex.reserve(data_size * 3);
  char buf[4];
  for (int i = 0; i < data_size; ++i) {
    if (i > 0) hex.push_back(' ');
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    hex += buf;
  }
  return hex;
}

static void render_history_view(const MessageId &id, cabana::ReplaySource *src) {
  auto it = src->eventsMap().find(id);
  if (it == src->eventsMap().end() || it->second.empty()) {
    ImGui::TextDisabled("Waiting for indexed CAN events...");
    return;
  }

  const auto *dbc_msg = cabana::dbc::dbc_manager().msg(id.address);
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

static void render_detail_tab_button(const char *label, DetailTab tab, AppState &st) {
  const bool selected = st.current_detail_tab == tab;
  ImGui::PushStyleColor(ImGuiCol_Header, selected ? ImVec4(0.17f, 0.30f, 0.45f, 0.95f) : ImVec4(0.18f, 0.18f, 0.18f, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.36f, 0.50f, 0.95f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.33f, 0.48f, 1.0f));
  if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0, 22.0f))) {
    st.setCurrentDetailTab(tab);
  }
  ImGui::PopStyleColor(3);
}

void detail() {
  ImGui::Begin("Detail");

  auto &st = cabana::app_state();

  if (!st.has_selection) {
    render_splash();
    ImGui::End();
    return;
  }

  auto *src = app() ? app()->source() : nullptr;
  if (!src) {
    render_splash();
    ImGui::End();
    return;
  }

  const auto &msgs = src->messages();
  auto it = msgs.find(st.selected_msg);
  if (it == msgs.end()) {
    ImGui::TextDisabled("Message not found in stream");
    ImGui::End();
    return;
  }

  const auto &id = st.selected_msg;
  const auto &live = it->second;
  const uint8_t *data = live.dat.empty() ? nullptr : live.dat.data();
  int data_size = (int)live.dat.size();

  // Message header
  auto *dbc_msg = cabana::dbc::dbc_manager().msg(id.address);
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

  ImGui::Separator();

  // App-owned tab strip keeps persistence deterministic across reloads.
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

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
