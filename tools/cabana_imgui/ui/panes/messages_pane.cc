#include "ui/panes/messages_pane.h"

#include "imgui.h"

namespace cabana {
namespace panes {

// Flat button helper — no frame background, just text with hover
static bool flat_button(const char *label) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
  bool clicked = ImGui::SmallButton(label);
  ImGui::PopStyleColor(3);
  return clicked;
}

void messages() {
  ImGui::Begin("Messages");

  // Info row: message counts + search field
  ImGui::Text("0 Messages (0 DBC Messages, 0 Signals)");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96);
  static char search[256] = "";
  ImGui::SetNextItemWidth(100);
  ImGui::InputTextWithHint("##search", "Search", search, sizeof(search));

  // Filter buttons row (flat style matching Qt)
  flat_button("Suppress Highlighted");
  ImGui::SameLine();
  flat_button("Clear CRCs");
  ImGui::SameLine();
  flat_button("Suppress Signals");

  ImGui::Separator();

  // Message table with tight rows
  if (ImGui::BeginTable("##msg_table", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Sortable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 38.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 45.0f);
    ImGui::TableHeadersRow();

    // Placeholder
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("(no stream loaded)");

    ImGui::EndTable();
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
