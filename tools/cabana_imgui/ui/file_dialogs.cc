#include "ui/file_dialogs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
#include "dbc/dbc_manager.h"

namespace cabana {
namespace file_dialogs {

namespace {

enum class DialogMode {
  None = 0,
  OpenDbc,
  SaveDbcAs,
  ExportCsv,
};

struct DialogRequest {
  DialogMode mode = DialogMode::None;
  SourceSet sources = cabana::dbc::sourceAll();
  int source = cabana::dbc::kDbcSourceAll;
};

constexpr size_t kPathBufferSize = 2048;

DialogRequest requested_dialog;
DialogRequest active_dialog;
bool focus_path_input = false;
char path_buffer[kPathBufferSize] = {};
std::string dialog_error;

const char *dialogTitle(DialogMode mode) {
  switch (mode) {
    case DialogMode::OpenDbc: return "Open DBC File";
    case DialogMode::SaveDbcAs: return "Save DBC As";
    case DialogMode::ExportCsv: return "Export to CSV";
    case DialogMode::None: break;
  }
  return "";
}

void copyPath(const std::string &path) {
  std::snprintf(path_buffer, sizeof(path_buffer), "%s", path.c_str());
}

std::string initialOpenPath(const DialogRequest &request) {
  const auto &st = cabana::app_state();
  const std::string assigned = st.dbcPathForSource(request.source);
  if (!assigned.empty()) return assigned;
  if (!st.active_dbc_file.empty()) return st.active_dbc_file;
  if (!st.recent_dbc_files.empty()) return st.recent_dbc_files.front();
  return {};
}

std::string initialSavePath(const DialogRequest &request) {
  const auto loaded = cabana::dbc::dbc_manager().loadedName(request.source);
  if (!loaded.empty()) return loaded;
  const auto &st = cabana::app_state();
  const std::string assigned = st.dbcPathForSource(request.source);
  if (!assigned.empty()) return assigned;
  if (!st.active_dbc_file.empty()) return st.active_dbc_file;
  return "untitled.dbc";
}

std::string sanitizeFilename(std::string value) {
  if (value.empty()) return "cabana_export";
  std::replace_if(value.begin(), value.end(), [](unsigned char c) {
    return !(std::isalnum(c) || c == '_' || c == '-' || c == '.');
  }, '_');
  return value;
}

std::string initialExportPath() {
  namespace fs = std::filesystem;

  const auto &st = cabana::app_state();
  fs::path base = fs::current_path();
  std::string name = "cabana_export";
  if (!st.route_name.empty()) {
    const fs::path route_path(st.route_name);
    name = sanitizeFilename(route_path.filename().string());
  }
  return (base / (name + ".csv")).string();
}

void openRequestedDialog() {
  if (requested_dialog.mode == DialogMode::None) return;

  active_dialog = requested_dialog;
  requested_dialog = {};
  dialog_error.clear();
  focus_path_input = true;
  switch (active_dialog.mode) {
    case DialogMode::OpenDbc:
      copyPath(initialOpenPath(active_dialog));
      break;
    case DialogMode::SaveDbcAs:
      copyPath(initialSavePath(active_dialog));
      break;
    case DialogMode::ExportCsv:
      copyPath(initialExportPath());
      break;
    case DialogMode::None:
      break;
  }
  ImGui::OpenPopup(dialogTitle(active_dialog.mode));
}

bool submitDialog() {
  if (!app()) return false;

  const std::string path = path_buffer;
  if (path.empty()) {
    dialog_error = "Path is required.";
    return false;
  }

  bool ok = false;
  switch (active_dialog.mode) {
    case DialogMode::OpenDbc:
      ok = app()->openDbcFile(path, active_dialog.sources);
      break;
    case DialogMode::SaveDbcAs:
      ok = app()->saveDbcAs(path, active_dialog.source);
      break;
    case DialogMode::ExportCsv:
      ok = app()->exportCsv(path);
      break;
    case DialogMode::None:
      break;
  }
  if (!ok) {
    switch (active_dialog.mode) {
      case DialogMode::OpenDbc:
        dialog_error = "Failed to open DBC file.";
        break;
      case DialogMode::SaveDbcAs:
        dialog_error = "Failed to save DBC file.";
        break;
      case DialogMode::ExportCsv:
        dialog_error = "Failed to export CSV.";
        break;
      case DialogMode::None:
        dialog_error.clear();
        break;
    }
    return false;
  }

  active_dialog = {};
  dialog_error.clear();
  ImGui::CloseCurrentPopup();
  return true;
}

}  // namespace

void requestOpenDbc() {
  requestOpenDbc(cabana::dbc::sourceAll());
}

void requestOpenDbc(const SourceSet &sources) {
  requested_dialog = {
    .mode = DialogMode::OpenDbc,
    .sources = sources,
    .source = sources.empty() ? cabana::dbc::kDbcSourceAll : *sources.begin(),
  };
}

void requestSaveDbcAs() {
  requestSaveDbcAs(cabana::dbc::kDbcSourceAll);
}

void requestSaveDbcAs(int source) {
  requested_dialog = {
    .mode = DialogMode::SaveDbcAs,
    .sources = {},
    .source = source,
  };
}

void requestExportCsv() {
  requested_dialog = {
    .mode = DialogMode::ExportCsv,
    .sources = {},
    .source = cabana::dbc::kDbcSourceAll,
  };
}

void render() {
  openRequestedDialog();
  if (active_dialog.mode == DialogMode::None) return;

  ImGui::SetNextWindowSize(ImVec2(680, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(dialogTitle(active_dialog.mode), nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }

  if (focus_path_input) {
    ImGui::SetKeyboardFocusHere();
    focus_path_input = false;
  }

  switch (active_dialog.mode) {
    case DialogMode::OpenDbc:
      ImGui::TextUnformatted("Enter a DBC file path to load.");
      break;
    case DialogMode::SaveDbcAs:
      ImGui::TextUnformatted("Enter the output path for the current DBC.");
      break;
    case DialogMode::ExportCsv:
      ImGui::TextUnformatted("Enter the output path for the CAN CSV export.");
      break;
    case DialogMode::None:
      break;
  }
  ImGui::Spacing();
  const bool submit_from_input = ImGui::InputText("Path", path_buffer, sizeof(path_buffer),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
  if (!dialog_error.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", dialog_error.c_str());
  }

  ImGui::Spacing();
  const bool can_submit = path_buffer[0] != '\0';
  if (!can_submit) ImGui::BeginDisabled();
  const char *action_label = active_dialog.mode == DialogMode::OpenDbc ? "Open" :
                             active_dialog.mode == DialogMode::SaveDbcAs ? "Save" : "Export";
  if (ImGui::Button(action_label, ImVec2(96, 0)) || submit_from_input) {
    submitDialog();
  }
  if (!can_submit) ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) {
    active_dialog = {};
    dialog_error.clear();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

}  // namespace file_dialogs
}  // namespace cabana
