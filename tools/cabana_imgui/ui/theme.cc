#include "ui/theme.h"

#include "imgui.h"

namespace cabana {
namespace theme {

void apply() {
  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 2.0f;
  style.FrameRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.TabRounding = 2.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
  colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
  colors[ImGuiCol_TabSelected] = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
}

}  // namespace theme
}  // namespace cabana
