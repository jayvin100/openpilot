#include "ui/panes/messages_pane.h"

#include "imgui.h"

namespace cabana {
namespace panes {

void messages() {
  ImGui::Begin("Messages");

  // Info row with message counts and search
  ImGui::TextUnformatted("0 Messages (0 DBC Messages, 0 Signals)");
  ImGui::SameLine(ImGui::GetWindowWidth() - 120);
  static char search[256] = "";
  ImGui::SetNextItemWidth(110);
  ImGui::InputTextWithHint("##search", "Search", search, sizeof(search));

  // Filter buttons row
  if (ImGui::Button("Suppress Highlighted")) {}
  ImGui::SameLine();
  if (ImGui::Button("Clear CRCs")) {}
  ImGui::SameLine();
  if (ImGui::Button("Suppress Signals")) {}

  ImGui::Separator();

  // Message table
  if (ImGui::BeginTable("##msg_table", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                        ImGuiTableFlags_Sortable)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 30.0f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 55.0f);
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
