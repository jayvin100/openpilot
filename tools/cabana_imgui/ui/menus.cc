#include "ui/menus.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
#include "core/command_stack.h"
#include "dbc/dbc_manager.h"
#include "ui/file_dialogs.h"
#include "ui/panes/detail_pane.h"

namespace cabana {
namespace menus {

namespace {

bool show_manage_dbcs_modal = false;

std::vector<int> available_buses() {
  std::set<int> buses;

  if (auto *application = app(); application && application->source()) {
    for (const auto &[id, _] : application->source()->messages()) {
      if (id.source < 64) {
        buses.insert(id.source);
      }
    }
  }

  for (const auto &[source, _] : cabana::app_state().active_dbc_files) {
    if (source >= 0 && source < 64) {
      buses.insert(source);
    }
  }

  return {buses.begin(), buses.end()};
}

std::string dbc_menu_label(const cabana::dbc::DbcFile *dbc_file) {
  if (!dbc_file) {
    return "No DBC loaded";
  }
  if (dbc_file->filename().empty()) {
    return "untitled.dbc";
  }
  return std::filesystem::path(dbc_file->filename()).filename().string();
}

int primary_source_for_file(const cabana::dbc::DbcFile *dbc_file) {
  if (!dbc_file) return cabana::dbc::kDbcSourceAll;
  const auto sources = cabana::dbc::dbc_manager().sources(dbc_file);
  return sources.empty() ? cabana::dbc::kDbcSourceAll : *sources.begin();
}

const cabana::dbc::DbcFile *single_dbc_file() {
  const auto files = cabana::dbc::dbc_manager().allDbcFiles();
  return files.size() == 1 ? *files.begin() : nullptr;
}

void render_manage_dbcs_modal(Application *application) {
  auto &dbc = cabana::dbc::dbc_manager();
  const auto buses = available_buses();

  if (show_manage_dbcs_modal) {
    ImGui::OpenPopup("Manage DBC Files");
    show_manage_dbcs_modal = false;
  }

  ImGui::SetNextWindowSize(ImVec2(760, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Manage DBC Files", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }

  if (buses.empty()) {
    ImGui::TextDisabled("No CAN buses detected yet.");
  } else {
    for (int bus : buses) {
      const auto sources = cabana::dbc::groupedSourcesForBus(bus);
      const auto *dbc_file = dbc.findDbcFile((uint8_t)bus);
      const std::string file_label = dbc_menu_label(dbc_file);
      const std::string source_label = dbc_file ? cabana::dbc::sourceSetLabel(dbc.sources(dbc_file)) : std::string();

      ImGui::PushID(bus);
      ImGui::Text("Bus %d", bus);
      ImGui::SameLine();
      ImGui::TextDisabled("%s", file_label.c_str());
      if (!source_label.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", source_label.c_str());
      }

      if (ImGui::Button("New DBC", ImVec2(88, 0)) && application) {
        application->newDbcFile(sources);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Open DBC...", ImVec2(96, 0))) {
        cabana::file_dialogs::requestOpenDbc(sources);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Load Clipboard", ImVec2(116, 0)) && application) {
        application->loadDbcFromClipboard(sources);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!dbc_file);
      if (ImGui::Button("Save", ImVec2(72, 0)) && application) {
        if (!dbc_file->filename().empty()) {
          application->saveDbc(bus);
        } else if (!dbc_file->messages().empty()) {
          cabana::file_dialogs::requestSaveDbcAs(bus);
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Save As", ImVec2(88, 0))) {
        cabana::file_dialogs::requestSaveDbcAs(bus);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Copy", ImVec2(88, 0)) && application) {
        application->copyDbcToClipboard(bus);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove Bus", ImVec2(104, 0)) && application) {
        application->closeDbcs(sources);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove All", ImVec2(104, 0)) && application) {
        application->closeDbcEverywhere(bus);
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::Separator();
      ImGui::PopID();
    }
  }

  if (ImGui::Button("Close", ImVec2(96, 0))) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace

void render() {
  auto &st = cabana::app_state();
  auto *application = app();
  auto &dbc = cabana::dbc::dbc_manager();
  auto &commands = cabana::command_stack();
  const bool has_dbc = dbc.hasAnyDbc();
  const auto *single_file = single_dbc_file();
  const bool has_single_dbc = single_file != nullptr;
  const int single_source = primary_source_for_file(single_file);

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Stream...")) {}
      if (ImGui::MenuItem("Close Stream", nullptr, false, application && application->source())) {
        application->closeRoute();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Export to CSV...")) {}
      ImGui::Separator();
      if (ImGui::MenuItem("New DBC File", "Ctrl+N") && application) {
        application->newDbcFile(cabana::dbc::sourceAll());
      }
      if (ImGui::MenuItem("Open DBC File...", "Ctrl+O")) {
        cabana::file_dialogs::requestOpenDbc();
      }
      if (ImGui::MenuItem("Manage DBC Files...")) {
        show_manage_dbcs_modal = true;
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
      if (ImGui::MenuItem("Load DBC From Clipboard") && application) {
        application->loadDbcFromClipboard(cabana::dbc::sourceAll());
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Save DBC...", "Ctrl+S", false, has_dbc)) {
        if (has_single_dbc) {
          if (!single_file->filename().empty()) {
            application->saveDbc(single_source);
          } else {
            cabana::file_dialogs::requestSaveDbcAs(single_source);
          }
        } else {
          application->saveDbc();
        }
      }
      if (ImGui::MenuItem("Save DBC As...", "Ctrl+Shift+S", false, has_single_dbc)) {
        cabana::file_dialogs::requestSaveDbcAs(single_source);
      }
      if (ImGui::MenuItem("Copy DBC To Clipboard", nullptr, false, has_single_dbc) && application) {
        application->copyDbcToClipboard(single_source);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Settings...")) {}
      if (ImGui::MenuItem("Exit", "Ctrl+Q")) { st.quit_requested = true; }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, commands.canUndo())) {
        commands.undo();
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, commands.canRedo())) {
        commands.redo();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Edit Message...", "Ctrl+E", false, cabana::panes::canEditSelectedMessage())) {
        cabana::panes::requestEditSelectedMessage();
      }
      if (ImGui::MenuItem("Add Signal...", nullptr, false, cabana::panes::canAddSignalToSelectedMessage())) {
        cabana::panes::requestAddSignalForSelectedMessage();
      }
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

  render_manage_dbcs_modal(application);
}

}  // namespace menus
}  // namespace cabana
