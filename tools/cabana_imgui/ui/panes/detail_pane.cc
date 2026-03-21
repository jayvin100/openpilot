#include "ui/panes/detail_pane.h"

#include "imgui.h"

namespace cabana {
namespace panes {

static void render_splash() {
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 center(avail.x * 0.5f, avail.y * 0.4f);

  // Big "CABANA" text
  ImGui::PushFont(nullptr);  // default font
  float text_w = ImGui::CalcTextSize("CABANA").x;
  ImGui::SetCursorPos(ImVec2(center.x - text_w * 1.5f, center.y - 40));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.6f));

  // Scale up the text by using a workaround: draw it manually
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddText(nullptr, 48.0f, pos, IM_COL32(128, 128, 128, 150), "CABANA");
  ImGui::Dummy(ImVec2(0, 60));

  // Subtitle
  float sub_w = ImGui::CalcTextSize("<-Select a message to view details").x;
  ImGui::SetCursorPosX(center.x - sub_w * 0.5f);
  ImGui::TextDisabled("<-Select a message to view details");

  ImGui::Spacing();
  ImGui::Spacing();

  // Keyboard shortcuts
  auto shortcut_row = [&](const char *label, const char *key) {
    float lw = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(center.x - lw - 10);
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::SmallButton(key);
  };

  shortcut_row("Pause", "Space");
  shortcut_row("Help", "F1");
  shortcut_row("WhatsThis", "Shift+F1");

  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void detail() {
  ImGui::Begin("Detail");

  bool has_selection = false;  // TODO: wire to actual selection state

  if (!has_selection) {
    render_splash();
  } else {
    if (ImGui::BeginTabBar("##detail_tabs")) {
      if (ImGui::BeginTabItem("Binary")) {
        ImGui::TextUnformatted("Binary data");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Signals")) {
        ImGui::TextUnformatted("Signal data");
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("History")) {
        ImGui::TextUnformatted("History data");
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
