#include "ui/panes/video_pane.h"

#include "imgui.h"

namespace cabana {
namespace panes {

void video() {
  ImGui::Begin("Video");

  // Route info header
  ImGui::TextUnformatted("ROUTE:");
  ImGui::SameLine();
  ImGui::TextDisabled("(no route loaded)");
  ImGui::SameLine(ImGui::GetWindowWidth() - 80);
  ImGui::TextDisabled("FINGERPRINT:");

  ImGui::Separator();

  // Video area (black)
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float video_h = avail.y * 0.55f;
  if (video_h < 60) video_h = 60;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + video_h),
                                            IM_COL32(0, 0, 0, 255));
  ImGui::Dummy(ImVec2(avail.x, video_h));

  // Timeline bar (colored segments placeholder)
  {
    ImVec2 tl_pos = ImGui::GetCursorScreenPos();
    float tl_w = avail.x;
    float tl_h = 12.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(tl_pos, ImVec2(tl_pos.x + tl_w, tl_pos.y + tl_h),
                      IM_COL32(40, 40, 40, 255));
    // Green progress stub
    dl->AddRectFilled(tl_pos, ImVec2(tl_pos.x + tl_w * 0.3f, tl_pos.y + tl_h),
                      IM_COL32(50, 180, 50, 255));
    ImGui::Dummy(ImVec2(tl_w, tl_h));
  }

  ImGui::Spacing();

  // Playback controls row
  if (ImGui::Button("<<")) {}
  ImGui::SameLine();
  if (ImGui::Button("||")) {}
  ImGui::SameLine();
  if (ImGui::Button(">>")) {}
  ImGui::SameLine();
  ImGui::Text("00:00.000 / 00:00");
  ImGui::SameLine();
  if (ImGui::Button("1x")) {}

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
