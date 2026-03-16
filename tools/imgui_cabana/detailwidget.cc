#include "tools/imgui_cabana/detailwidget.h"

#include "imgui.h"

#include "tools/imgui_cabana/binaryview.h"

namespace imgui_cabana {

void drawDetailPane(DetailWidgetModel &model, const DetailWidgetCallbacks &callbacks) {
  auto &message = (*model.messages)[*model.selected_message_index];

  ImGui::BeginChild("DetailToolbar", ImVec2(0.0f, 33.0f), false);
  callbacks.widget.capture_window_rect("DetailToolbar", "QWidget");
  ImGui::SetCursorPos(ImVec2(6.0f, 6.0f));
  std::string label = message.name + " (" + message.node + ")";
  ImGui::TextUnformatted(label.c_str());
  callbacks.widget.capture_item("DetailMessageLabel", "QLabel", label, std::nullopt);
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 190.0f);
  ImGui::TextUnformatted("Heatmap:");
  ImGui::SameLine();
  ImGui::RadioButton("Live", true);
  ImGui::SameLine();
  ImGui::RadioButton("All", false);
  ImGui::SameLine();
  if (ImGui::Button("Edit", ImVec2(42.0f, 22.0f))) callbacks.open_edit_dialog();
  callbacks.widget.capture_item("EditMessageButton", "QToolButton", "Edit", std::nullopt);
  ImGui::EndChild();

  drawBinaryView(message);
  drawSignalView(model.signal_view, callbacks.widget);
}

}  // namespace imgui_cabana
