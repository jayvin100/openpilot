#include "tools/imgui_cabana/signalview.h"

#include "imgui.h"

namespace imgui_cabana {

void drawSignalView(SignalViewModel &model, const WidgetCallbacks &callbacks) {
  auto &message = (*model.messages)[*model.selected_message_index];

  ImGui::BeginChild("SignalView", ImVec2(0.0f, 0.0f), false);
  callbacks.capture_window_rect("SignalView", "QWidget");
  ImGui::SetNextItemWidth(-1.0f);
  if (*model.focus_signal_filter) {
    ImGui::SetKeyboardFocusHere();
    *model.focus_signal_filter = false;
  }
  if (ImGui::InputTextWithHint("##SignalFilterEdit", "Filter signals", model.signal_filter_buffer->data(), model.signal_filter_buffer->size())) {
    *model.signal_filter = std::string(model.signal_filter_buffer->data());
  }
  callbacks.capture_item("SignalFilterEdit", "QLineEdit", *model.signal_filter, std::string("Filter signals"));

  ImGui::BeginChild("SignalTree", ImVec2(0.0f, 0.0f), false);
  callbacks.capture_window_rect("SignalTree", "QTreeView");
  for (auto &signal : message.signals) {
    if (!model.signal_filter->empty() && signal.name.find(*model.signal_filter) == std::string::npos) continue;
    ImGui::PushID(signal.name.c_str());
    ImVec2 row_min = ImGui::GetCursorScreenPos();
    ImGui::Selectable("##signal_row", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(-1.0f, 24.0f));
    (*model.signal_snapshots)[signal.name].rect = callbacks.current_item_rect();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->AddText(ImVec2(row_min.x + 8.0f, row_min.y + 5.0f), IM_COL32(32, 32, 32, 255), signal.name.c_str());
    draw_list->AddText(ImVec2(row_min.x + 280.0f, row_min.y + 5.0f), IM_COL32(88, 88, 88, 255), signal.value.c_str());
    ImGui::SetCursorScreenPos(ImVec2(row_min.x + ImGui::GetContentRegionAvail().x - 60.0f, row_min.y + 2.0f));
    if (ImGui::SmallButton(signal.plotted ? "Plotted" : "Plot")) signal.plotted = !signal.plotted;
    (*model.signal_snapshots)[signal.name].plot_rect = callbacks.current_item_rect();
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndChild();
}

}  // namespace imgui_cabana
