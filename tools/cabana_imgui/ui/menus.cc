#include "ui/menus.h"

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
#include "dbc/dbc_manager.h"
#include "ui/file_dialogs.h"

namespace cabana {
namespace menus {

void render() {
  auto &st = cabana::app_state();
  auto *application = app();
  auto &dbc = cabana::dbc::dbc_manager();
  const bool has_dbc = dbc.dbc() != nullptr;

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Stream...")) {}
      if (ImGui::MenuItem("Close Stream", nullptr, false, application && application->source())) {
        application->closeRoute();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Export to CSV...")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("New DBC File", "Ctrl+N")) {}
      if (ImGui::MenuItem("Open DBC File...", "Ctrl+O")) {
        cabana::file_dialogs::requestOpenDbc();
      }
      if (ImGui::BeginMenu("Manage DBC Files")) {
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Open Recent")) {
        bool added = false;
        if (!st.recent_dbc_files.empty()) {
          ImGui::TextDisabled("DBC Files");
          for (const auto &path : st.recent_dbc_files) {
            if (ImGui::MenuItem(path.c_str()) && application) {
              application->openDbcFile(path);
            }
          }
          added = true;
        }
        if (!st.recent_routes.empty()) {
          if (added) ImGui::Separator();
          ImGui::TextDisabled("Routes");
          for (const auto &route : st.recent_routes) {
            if (ImGui::MenuItem(route.c_str()) && application) {
              application->openRoute(route);
            }
          }
          added = true;
        }
        if (!added) {
          ImGui::MenuItem("No Recent Items", nullptr, false, false);
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::BeginMenu("Load DBC from comma/opendbc")) {
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Load DBC From Clipboard")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("Save DBC...", "Ctrl+S", false, has_dbc)) {
        if (!dbc.loadedName().empty()) {
          application->saveDbc();
        } else {
          cabana::file_dialogs::requestSaveDbcAs();
        }
      }
      if (ImGui::MenuItem("Save DBC As...", "Ctrl+Shift+S", false, has_dbc)) {
        cabana::file_dialogs::requestSaveDbcAs();
      }
      if (ImGui::MenuItem("Copy DBC To Clipboard")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("Settings...")) {}
      if (ImGui::MenuItem("Exit", "Ctrl+Q")) { st.quit_requested = true; }
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
      if (ImGui::MenuItem("Reset Window Layout")) {
        cabana::app_state().reset_layout_requested = true;
      }
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
      if (ImGui::MenuItem("Help", "F1")) {
        st.show_help_overlay = true;
      }
      if (ImGui::MenuItem("Online Help")) {}
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

}  // namespace menus
}  // namespace cabana
