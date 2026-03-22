#include "ui/file_dialogs.h"

#include <cstdio>
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

void openRequestedDialog() {
  if (requested_dialog.mode == DialogMode::None) return;

  active_dialog = requested_dialog;
  requested_dialog = {};
  dialog_error.clear();
  focus_path_input = true;
  copyPath(active_dialog.mode == DialogMode::OpenDbc ? initialOpenPath(active_dialog) : initialSavePath(active_dialog));
  ImGui::OpenPopup(dialogTitle(active_dialog.mode));
}

bool submitDialog() {
  if (!app()) return false;

  const std::string path = path_buffer;
  if (path.empty()) {
    dialog_error = "Path is required.";
    return false;
  }

  const bool ok = active_dialog.mode == DialogMode::OpenDbc
                    ? app()->openDbcFile(path, active_dialog.sources)
                    : app()->saveDbcAs(path, active_dialog.source);
  if (!ok) {
    dialog_error = active_dialog.mode == DialogMode::OpenDbc ? "Failed to open DBC file." : "Failed to save DBC file.";
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

  ImGui::TextUnformatted(active_dialog.mode == DialogMode::OpenDbc ?
                         "Enter a DBC file path to load." :
                         "Enter the output path for the current DBC.");
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
  if (ImGui::Button(active_dialog.mode == DialogMode::OpenDbc ? "Open" : "Save", ImVec2(96, 0)) || submit_from_input) {
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
