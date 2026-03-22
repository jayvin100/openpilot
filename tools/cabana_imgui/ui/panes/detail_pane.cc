#include "ui/panes/detail_pane.h"

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
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 0.50f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  ImGui::Spacing();
  {
    const char *sub = st.route_loading ? "Loading route data..." :
                     !st.route_load_error.empty() ? "Route load failed" :
                     "<-Select a message to view details";
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
  shortcut("WhatsThis", "Shift+F1");
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

  if (ImGui::BeginTable("##signals", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableHeadersRow();

    for (const auto &sig : msg->signals) {
      double val = sig.getValue(data, data_size);

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
    }

    ImGui::EndTable();
  }
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

  // Tab bar: Binary | Signals | History
  if (ImGui::BeginTabBar("##detail_tabs")) {
    if (ImGui::BeginTabItem("Binary")) {
      if (data && data_size > 0) {
        render_binary_view(id, data, data_size);
      } else {
        ImGui::TextDisabled("No data");
      }

      // Show signals below binary view
      ImGui::Spacing();
      ImGui::Separator();
      if (data && data_size > 0) {
        render_signal_list(id, data, data_size);
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Signals")) {
      if (data && data_size > 0) {
        render_signal_list(id, data, data_size);
      } else {
        ImGui::TextDisabled("No data");
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("History")) {
      ImGui::TextDisabled("History view not yet implemented");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
