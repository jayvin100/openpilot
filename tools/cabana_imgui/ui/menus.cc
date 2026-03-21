#include "ui/menus.h"

#include "imgui.h"
#include "core/app_state.h"

namespace cabana {
namespace menus {

void render() {
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Stream...")) {}
      if (ImGui::MenuItem("Close Stream")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("Export to CSV...")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("New DBC File", "Ctrl+N")) {}
      if (ImGui::MenuItem("Open DBC File...", "Ctrl+O")) {}
      if (ImGui::BeginMenu("Manage DBC Files")) {
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Open Recent")) {
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::BeginMenu("Load DBC from comma/opendbc")) {
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Load DBC From Clipboard")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("Save DBC...", "Ctrl+S")) {}
      if (ImGui::MenuItem("Save DBC As...")) {}
      if (ImGui::MenuItem("Copy DBC To Clipboard")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("Settings...")) {}
      if (ImGui::MenuItem("Exit", "Ctrl+Q")) { cabana::app_state().quit_requested = true; }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
      if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Full Screen")) {}
      ImGui::Separator();
      ImGui::MenuItem("Messages", nullptr, nullptr);
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Window Layout")) {}
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::MenuItem("Find Signal")) {}
      if (ImGui::MenuItem("Find Similar Bits")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("Route Info")) {}
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Help", "F1")) {}
      if (ImGui::MenuItem("Online Help")) {}
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

}  // namespace menus
}  // namespace cabana
