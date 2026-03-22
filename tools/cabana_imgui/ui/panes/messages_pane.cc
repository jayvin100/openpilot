#include "ui/panes/messages_pane.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <tuple>
#include <string>
#include <vector>

#include "imgui.h"
#include "app/application.h"
#include "core/app_state.h"
#include "dbc/dbc_manager.h"
#include "sources/source.h"

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
  std::string search_text;
  uint32_t count;
  double freq;
};

enum class MsgSortColumn {
  Name = 0,
  Bus,
  Id,
  Freq,
  Count,
};

std::string lower_copy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

std::string build_search_text(const MessageId &id, const std::string &name) {
  char bus[16];
  char addr[16];
  snprintf(bus, sizeof(bus), "%u", id.source);
  snprintf(addr, sizeof(addr), "0x%X", id.address);
  return lower_copy(name + " " + bus + " " + addr);
}

bool less_by_column(const MsgRow &lhs, const MsgRow &rhs, MsgSortColumn column) {
  switch (column) {
    case MsgSortColumn::Name:
      return std::tie(lhs.name, lhs.id) < std::tie(rhs.name, rhs.id);
    case MsgSortColumn::Bus:
      return std::tie(lhs.id.source, lhs.id.address) < std::tie(rhs.id.source, rhs.id.address);
    case MsgSortColumn::Id:
      return std::tie(lhs.id.address, lhs.id.source) < std::tie(rhs.id.address, rhs.id.source);
    case MsgSortColumn::Freq:
      return std::tie(lhs.freq, lhs.id) < std::tie(rhs.freq, rhs.id);
    case MsgSortColumn::Count:
      return std::tie(lhs.count, lhs.id) < std::tie(rhs.count, rhs.id);
  }
  return std::tie(lhs.name, lhs.id) < std::tie(rhs.name, rhs.id);
}

void rebuild_rows(std::vector<MsgRow> &rows, const std::unordered_map<MessageId, MsgLiveData> &msgs,
                  cabana::dbc::DbcManager &dbc_mgr) {
  rows.clear();
  rows.reserve(msgs.size());
  for (const auto &[id, m] : msgs) {
    const char *msg_name = dbc_mgr.msgName(id);
    std::string name;
    if (msg_name && *msg_name != '\0') {
      name = msg_name;
    } else {
      char hex[16];
      snprintf(hex, sizeof(hex), "0x%X", id.address);
      name = hex;
    }
    rows.push_back({.id = id,
                    .name = name,
                    .search_text = build_search_text(id, name),
                    .count = m.count,
                    .freq = m.freq});
  }
}

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
  static const cabana::Source *last_src = nullptr;
  static uint64_t last_dbc_revision = 0;
  static bool show_inactive_messages = true;

  if (src) {
    const auto &msgs = src->messages();
    auto &dbc_mgr = cabana::dbc::dbc_manager();
    const uint64_t dbc_revision = dbc_mgr.revision();

    if (src != last_src || dbc_revision != last_dbc_revision || (int)msgs.size() != last_msg_count) {
      last_src = src;
      last_dbc_revision = dbc_revision;
      last_msg_count = (int)msgs.size();
      rebuild_rows(rows, msgs, dbc_mgr);
    } else {
      // Update counts/freq without rebuilding rows
      bool needs_rebuild = false;
      for (auto &r : rows) {
        auto it = msgs.find(r.id);
        if (it != msgs.end()) {
          r.count = it->second.count;
          r.freq = it->second.freq;
        } else {
          needs_rebuild = true;
          break;
        }
      }
      if (needs_rebuild) {
        rebuild_rows(rows, msgs, dbc_mgr);
      }
    }
  } else if (last_src != nullptr) {
    rows.clear();
    last_msg_count = 0;
    last_src = nullptr;
    last_dbc_revision = 0;
  }

  // Summary row
  const int total_msgs = (int)rows.size();
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
  ImGui::SameLine();
  ImGui::Checkbox("Inactive", &show_inactive_messages);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip("Show inactive messages");
  }

  ImGui::Separator();

  // Message table
  if (ImGui::BeginTable("##msg_table", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Sortable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 28.0f);
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
      } else if (src) {
        ImGui::TextDisabled(src->liveStreaming() ? "(waiting for CAN traffic...)" : "(no messages indexed yet)");
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
        filter_lower = lower_copy(search);
      }

      static std::vector<int> visible;
      visible.clear();
      visible.reserve(rows.size());
      for (int i = 0; i < (int)rows.size(); i++) {
        if (!show_inactive_messages && src && !src->isMessageActive(rows[i].id)) {
          continue;
        }
        if (has_filter && rows[i].search_text.find(filter_lower) == std::string::npos) {
          continue;
        }
        visible.push_back(i);
      }

      ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs();
      const MsgSortColumn sort_column = (sort_specs && sort_specs->SpecsCount > 0)
                                           ? static_cast<MsgSortColumn>(sort_specs->Specs[0].ColumnIndex)
                                           : MsgSortColumn::Name;
      const bool ascending = !(sort_specs && sort_specs->SpecsCount > 0 &&
                               sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Descending);
      std::stable_sort(visible.begin(), visible.end(), [&](int lhs, int rhs) {
        const bool less = less_by_column(rows[lhs], rows[rhs], sort_column);
        const bool greater = less_by_column(rows[rhs], rows[lhs], sort_column);
        if (less == greater) {
          return rows[lhs].id < rows[rhs].id;
        }
        return ascending ? less : greater;
      });

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
          const bool is_active = src ? src->isMessageActive(r.id) : false;
          const bool dim_inactive = show_inactive_messages && !is_active;
          ImGui::TableNextRow();
          ImGui::TableNextColumn();

          // Selectable row
          bool is_selected = st.has_selection && st.selected_msg == r.id;
          if (dim_inactive) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          }
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
          if (dim_inactive) {
            ImGui::PopStyleColor();
          }
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
