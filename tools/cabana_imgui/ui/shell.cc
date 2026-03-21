#include "ui/shell.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/menus.h"
#include "ui/panes/messages_pane.h"
#include "ui/panes/detail_pane.h"
#include "ui/panes/video_pane.h"
#include "ui/panes/charts_pane.h"

namespace cabana {
namespace shell {

static bool first_frame = true;
static constexpr float STATUS_BAR_HEIGHT = 22.0f;

static void setup_default_layout(ImGuiID dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

  // Left: Messages (25%)
  ImGuiID left, remainder;
  ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, &left, &remainder);

  // Right: Video+Charts (25%), Center: Detail
  ImGuiID right, center;
  ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, 0.30f, &right, &center);

  // Right split: Video top (45%), Charts bottom (55%)
  ImGuiID right_top, right_bottom;
  ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.55f, &right_top, &right_bottom);

  ImGui::DockBuilderDockWindow("Messages", left);
  ImGui::DockBuilderDockWindow("Detail", center);
  ImGui::DockBuilderDockWindow("Video", right_top);
  ImGui::DockBuilderDockWindow("Charts", right_bottom);

  ImGui::DockBuilderFinish(dockspace_id);
}

static void render_status_bar() {
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - STATUS_BAR_HEIGHT));
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, STATUS_BAR_HEIGHT));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoNav;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));

  if (ImGui::Begin("##StatusBar", nullptr, flags)) {
    ImGui::TextUnformatted("For Help, Press F1");
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::TextUnformatted("Cached Minutes:0  FPS:0");
  }
  ImGui::End();

  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
}

void render() {
  ImGuiViewport *viewport = ImGui::GetMainViewport();

  // Full-screen dockspace (leave room for status bar at bottom)
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - STATUS_BAR_HEIGHT));
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##DockSpace", nullptr, flags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace_id = ImGui::GetID("CabanaDockSpace");

  if (first_frame) {
    setup_default_layout(dockspace_id);
    first_frame = false;
  }

  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);

  // Menu bar
  cabana::menus::render();

  ImGui::End();

  // Panes
  cabana::panes::messages();
  cabana::panes::detail();
  cabana::panes::video();
  cabana::panes::charts();

  // Status bar
  render_status_bar();
}

}  // namespace shell
}  // namespace cabana
