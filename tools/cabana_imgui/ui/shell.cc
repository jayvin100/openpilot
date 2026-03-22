#include "ui/shell.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "core/app_state.h"
#include "ui/menus.h"
#include "ui/panes/messages_pane.h"
#include "ui/panes/detail_pane.h"
#include "ui/panes/video_pane.h"
#include "ui/panes/charts_pane.h"

namespace cabana {
namespace shell {

static bool first_frame = true;
static constexpr float STATUS_BAR_HEIGHT = 20.0f;
static constexpr float LEFT_DOCK_RATIO = 0.21f;
static constexpr float RIGHT_DOCK_RATIO = 0.36f;
static constexpr float RIGHT_TOP_RATIO = 0.58f;

static void setup_default_layout(ImGuiID dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

  // Left: Messages
  ImGuiID left, remainder;
  ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, LEFT_DOCK_RATIO, &left, &remainder);

  // Right: Video+Charts, Center: Detail
  ImGuiID right, center;
  ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, RIGHT_DOCK_RATIO, &right, &center);

  // Right split: Video top, Charts bottom
  ImGuiID right_top, right_bottom;
  ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, RIGHT_TOP_RATIO, &right_top, &right_bottom);

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
    ImGui::SameLine(ImGui::GetWindowWidth() - 220);
    auto &st = cabana::app_state();
    ImGui::Text("Cached Minutes:%d  FPS:%.0f", st.cached_minutes,
                ImGui::GetIO().Framerate);
  }
  ImGui::End();

  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
}

static void render_route_load_overlay() {
  auto &st = cabana::app_state();
  if (!st.route_loading && st.route_load_error.empty()) {
    return;
  }

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.92f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;

  if (ImGui::Begin("##RouteLoadOverlay", nullptr, flags)) {
    if (st.route_loading) {
      ImGui::TextUnformatted("Loading route...");
      if (!st.route_name.empty()) {
        ImGui::TextDisabled("%s", st.route_name.c_str());
      }
    } else {
      ImGui::TextUnformatted("Route load failed");
      ImGui::Separator();
      ImGui::TextWrapped("%s", st.route_load_error.c_str());
    }
  }
  ImGui::End();
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
  auto &st = cabana::app_state();

  if (first_frame || st.reset_layout_requested) {
    setup_default_layout(dockspace_id);
    first_frame = false;
    st.reset_layout_requested = false;
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
  render_route_load_overlay();
}

}  // namespace shell
}  // namespace cabana
