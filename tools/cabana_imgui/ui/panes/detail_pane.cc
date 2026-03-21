#include "ui/panes/detail_pane.h"

#include "imgui.h"
#include "ui/theme.h"

namespace cabana {
namespace panes {

static void render_splash() {
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float cx = avail.x * 0.5f;
  float cy = avail.y * 0.38f;

  // "CABANA" in large bold font
  ImFont *splash = cabana::theme::font_splash();
  if (splash) {
    const char *text = "CABANA";
    ImGui::PushFont(splash, 56.0f);
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImGui::SetCursorPos(ImVec2(cx - sz.x * 0.5f, cy));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 0.50f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  ImGui::Spacing();

  // Subtitle
  {
    const char *sub = "<-Select a message to view details";
    float sw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX(cx - sw * 0.5f);
    ImGui::TextDisabled("%s", sub);
  }

  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  // Keyboard shortcut hints — right-aligned labels with button keys
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
