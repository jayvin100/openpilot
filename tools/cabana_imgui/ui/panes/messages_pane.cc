#include "ui/panes/messages_pane.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "app/application.h"
#include "core/app_state.h"
#include "dbc/dbc_manager.h"
#include "sources/replay_source.h"

namespace cabana {
namespace panes {

static bool flat_button(const char *label) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
  bool clicked = ImGui::SmallButton(label);
  ImGui::PopStyleColor(3);
  return clicked;
}

struct MsgRow {
  MessageId id;
  std::string name;
  uint32_t count;
  double freq;
};

static int current_visible_index(const std::vector<MsgRow> &rows, const std::vector<int> &visible,
                                 const MessageId &selected_msg) {
  for (int i = 0; i < (int)visible.size(); ++i) {
    if (rows[visible[i]].id == selected_msg) {
      return i;
    }
  }
  return -1;
}

void messages() {
  ImGui::Begin("Messages");

  auto *src = app() ? app()->source() : nullptr;

  // Read messages from source (already on main thread, no copy needed)
  static std::vector<MsgRow> rows;
  static int last_msg_count = 0;
  static const cabana::ReplaySource *last_src = nullptr;
  static uint64_t last_dbc_revision = 0;

  if (src) {
    const auto &msgs = src->messages();
    auto &dbc_mgr = cabana::dbc::dbc_manager();
    const uint64_t dbc_revision = dbc_mgr.revision();

    if (src != last_src || dbc_revision != last_dbc_revision || (int)msgs.size() != last_msg_count) {
      last_src = src;
      last_dbc_revision = dbc_revision;
      last_msg_count = (int)msgs.size();
      rows.clear();
      rows.reserve(msgs.size());
      for (const auto &[id, m] : msgs) {
        const char *msg_name = dbc_mgr.msgName(id);
        std::string name;
        if (msg_name) {
          name = msg_name;
        } else {
          char hex[16];
          snprintf(hex, sizeof(hex), "0x%X", id.address);
          name = hex;
        }
        rows.push_back({id, std::move(name), m.count, m.freq});
      }
      std::sort(rows.begin(), rows.end(), [](const MsgRow &a, const MsgRow &b) {
        return a.id < b.id;
      });
    } else {
      // Update counts/freq without rebuilding rows
      for (auto &r : rows) {
        auto it = msgs.find(r.id);
        if (it != msgs.end()) {
          r.count = it->second.count;
          r.freq = it->second.freq;
        }
      }
    }
  } else if (last_src != nullptr) {
    rows.clear();
    last_msg_count = 0;
    last_src = nullptr;
    last_dbc_revision = 0;
  }

  // Summary row
  int total_msgs = (int)rows.size();
  auto &dbc = cabana::dbc::dbc_manager();
  ImGui::Text("%d Messages", total_msgs);
  ImGui::SameLine();
  ImGui::TextDisabled("(%d DBC Messages, %d Signals)", dbc.msgCount(), dbc.signalCount());

  // Search row
  static char search[256] = "";
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##search", "Search", search, sizeof(search));

  // Filter actions are still pending parity wiring.
  ImGui::BeginDisabled();
  flat_button("Suppress Highlighted");
  ImGui::SameLine();
  flat_button("Clear CRC");
  ImGui::SameLine();
  flat_button("Hide Signals");
  ImGui::EndDisabled();

  ImGui::Separator();

  // Message table
  if (ImGui::BeginTable("##msg_table", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Sortable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableHeadersRow();

    if (rows.empty()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const auto &st = cabana::app_state();
      if (st.route_loading) {
        ImGui::TextDisabled("(loading route...)");
      } else if (!st.route_load_error.empty()) {
        ImGui::TextDisabled("(route load failed)");
      } else {
        ImGui::TextDisabled("(no stream loaded)");
      }
    } else {
      // Build filtered indices once per frame
      bool has_filter = search[0] != '\0';
      std::string filter_lower;
      if (has_filter) {
        filter_lower = search;
        std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);
      }

      static std::vector<int> visible;
      visible.clear();
      visible.reserve(rows.size());
      for (int i = 0; i < (int)rows.size(); i++) {
        if (has_filter) {
          std::string name_lower = rows[i].name;
          std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
          if (name_lower.find(filter_lower) == std::string::npos) continue;
        }
        visible.push_back(i);
      }

      auto &st = cabana::app_state();
      int scroll_to_visible_row = -1;
      if (!visible.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
          !ImGui::GetIO().WantTextInput) {
        auto select_visible = [&](int visible_index) {
          visible_index = std::clamp(visible_index, 0, (int)visible.size() - 1);
          st.setSelectedMessage(rows[visible[visible_index]].id);
          scroll_to_visible_row = visible_index;
        };

        const int selected_visible = st.has_selection ? current_visible_index(rows, visible, st.selected_msg) : -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
          select_visible(selected_visible == -1 ? 0 : selected_visible + 1);
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
          select_visible(selected_visible == -1 ? (int)visible.size() - 1 : selected_visible - 1);
        }
      }

      auto render_row = [&](int row) {
          const auto &r = rows[visible[row]];
          ImGui::TableNextRow();
          ImGui::TableNextColumn();

          // Selectable row
          bool is_selected = st.has_selection && st.selected_msg == r.id;
          if (ImGui::Selectable(r.name.c_str(), is_selected,
                                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                                ImVec2(-FLT_MIN, 0.0f))) {
            st.setSelectedMessage(r.id);
            scroll_to_visible_row = row;
          }
          if (scroll_to_visible_row == row) {
            ImGui::SetScrollHereY(0.35f);
          }
          ImGui::TableNextColumn();
          ImGui::Text("%d", r.id.source);
          ImGui::TableNextColumn();
          ImGui::Text("0x%X", r.id.address);
          ImGui::TableNextColumn();
          ImGui::Text("%.0f", r.freq);
          ImGui::TableNextColumn();
          ImGui::Text("%u", r.count);
      };

      // Force one full render pass when keyboard navigation changes selection so the scroll target exists.
      if (scroll_to_visible_row != -1) {
        for (int row = 0; row < (int)visible.size(); ++row) {
          render_row(row);
        }
      } else {
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size());
        while (clipper.Step()) {
          for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            render_row(row);
          }
        }
      }
    }

    ImGui::EndTable();
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
