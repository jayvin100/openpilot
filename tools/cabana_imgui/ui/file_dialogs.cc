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

constexpr size_t kPathBufferSize = 2048;

DialogMode requested_dialog = DialogMode::None;
DialogMode active_dialog = DialogMode::None;
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

std::string initialOpenPath() {
  const auto &st = cabana::app_state();
  if (!st.active_dbc_file.empty()) return st.active_dbc_file;
  if (!st.recent_dbc_files.empty()) return st.recent_dbc_files.front();
  return {};
}

std::string initialSavePath() {
  const auto &loaded = cabana::dbc::dbc_manager().loadedName();
  if (!loaded.empty()) return loaded;
  const auto &st = cabana::app_state();
  if (!st.active_dbc_file.empty()) return st.active_dbc_file;
  return "untitled.dbc";
}

void openRequestedDialog() {
  if (requested_dialog == DialogMode::None) return;

  active_dialog = requested_dialog;
  requested_dialog = DialogMode::None;
  dialog_error.clear();
  focus_path_input = true;
  copyPath(active_dialog == DialogMode::OpenDbc ? initialOpenPath() : initialSavePath());
  ImGui::OpenPopup(dialogTitle(active_dialog));
}

bool submitDialog() {
  if (!app()) return false;

  const std::string path = path_buffer;
  if (path.empty()) {
    dialog_error = "Path is required.";
    return false;
  }

  const bool ok = active_dialog == DialogMode::OpenDbc ? app()->openDbcFile(path) : app()->saveDbcAs(path);
  if (!ok) {
    dialog_error = active_dialog == DialogMode::OpenDbc ? "Failed to open DBC file." : "Failed to save DBC file.";
    return false;
  }

  active_dialog = DialogMode::None;
  dialog_error.clear();
  ImGui::CloseCurrentPopup();
  return true;
}

}  // namespace

void requestOpenDbc() {
  requested_dialog = DialogMode::OpenDbc;
}

void requestSaveDbcAs() {
  requested_dialog = DialogMode::SaveDbcAs;
}

void render() {
  openRequestedDialog();
  if (active_dialog == DialogMode::None) return;

  ImGui::SetNextWindowSize(ImVec2(680, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(dialogTitle(active_dialog), nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }

  if (focus_path_input) {
    ImGui::SetKeyboardFocusHere();
    focus_path_input = false;
  }

  ImGui::TextUnformatted(active_dialog == DialogMode::OpenDbc ?
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
  if (ImGui::Button(active_dialog == DialogMode::OpenDbc ? "Open" : "Save", ImVec2(96, 0)) || submit_from_input) {
    submitDialog();
  }
  if (!can_submit) ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) {
    active_dialog = DialogMode::None;
    dialog_error.clear();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

}  // namespace file_dialogs
}  // namespace cabana
