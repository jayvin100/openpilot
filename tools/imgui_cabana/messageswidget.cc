#include "tools/imgui_cabana/messageswidget.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace imgui_cabana {

namespace {

std::string formatFrequency(double freq) {
  if (freq <= 0.0) return "--";
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", freq);
  return buffer;
}

std::string formatBytes(const MessageData &message) {
  if (message.bytes.empty()) return "--";
  std::string out;
  char buffer[4];
  for (std::size_t i = 0; i < message.bytes.size(); ++i) {
    if (i != 0) out += ' ';
    std::snprintf(buffer, sizeof(buffer), "%02X", message.bytes[i]);
    out += buffer;
  }
  return out;
}

void drawMessagesFilter(const char *id, char *buffer, size_t size, const char *hint, float width,
                        bool *focus_message_filter, std::string *message_filter, const std::function<void()> &clamp_selection,
                        bool interactive) {
  ImGui::SetNextItemWidth(width);
  if (interactive && *focus_message_filter) {
    ImGui::SetKeyboardFocusHere();
    *focus_message_filter = false;
  }
  if (interactive) {
    if (ImGui::InputTextWithHint(id, hint, buffer, size)) {
      *message_filter = std::string(buffer);
      clamp_selection();
    }
  } else {
    ImGui::InputTextWithHint(id, hint, buffer, size, ImGuiInputTextFlags_ReadOnly);
  }
}

}  // namespace

std::vector<int> filteredMessageIndices(const MessagesWidgetModel &model) {
  std::vector<int> indices;
  for (size_t i = 0; i < model.messages->size(); ++i) {
    if (model.message_filter->empty() || (*model.messages)[i].name.find(*model.message_filter) == 0) {
      indices.push_back(static_cast<int>(i));
    }
  }
  if (indices.empty()) {
    for (size_t i = 0; i < model.messages->size(); ++i) indices.push_back(static_cast<int>(i));
  }
  return indices;
}

void clampSelection(MessagesWidgetModel &model) {
  const auto visible = filteredMessageIndices(model);
  if (visible.empty()) return;
  if (std::find(visible.begin(), visible.end(), *model.selected_message_index) == visible.end()) {
    *model.selected_message_index = visible.front();
  }
}

void drawMessagesPane(MessagesWidgetModel &model, const WidgetCallbacks &callbacks) {
  ImGui::BeginChild("MessagesToolbar", ImVec2(0.0f, 35.0f), false);
  callbacks.capture_window_rect("MessagesToolbar", "QWidget");
  ImGui::SetCursorPos(ImVec2(0.0f, 9.0f));
  ImGui::Button("Suppress Highlighted", ImVec2(140.0f, 26.0f));
  callbacks.capture_item("SuppressHighlightedButton", "QPushButton", "Suppress Highlighted", std::nullopt);
  ImGui::SameLine(146.0f, 0.0f);
  ImGui::Button("Clear", ImVec2(80.0f, 26.0f));
  callbacks.capture_item("SuppressClearButton", "QPushButton", "Clear", std::nullopt);
  ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - 24.0f, 10.0f));
  ImGui::Button("...", ImVec2(24.0f, 23.0f));
  callbacks.capture_item("MessagesViewButton", "QToolButton", "...", std::nullopt);
  ImGui::EndChild();

  ImGui::BeginChild("MessageHeader", ImVec2(0.0f, 49.0f), false);
  callbacks.capture_window_rect("MessageHeader", "QWidget");
  auto clamp = [&model]() { clampSelection(model); };
  ImGui::SetCursorPos(ImVec2(1.0f, 4.0f));
  drawMessagesFilter("##MessagesFilterName", model.message_filter_buffer->data(), model.message_filter_buffer->size(),
                     "Filter Name", 104.0f, model.focus_message_filter, model.message_filter, clamp, true);
  callbacks.capture_item("MessagesFilterName", "QLineEdit", *model.message_filter, std::string("Filter Name"));
  ImGui::SameLine(105.0f, 0.0f);
  drawMessagesFilter("##MessagesFilterBus", model.message_bus_filter_buffer->data(), model.message_bus_filter_buffer->size(),
                     "Filter Bus", 104.0f, model.focus_message_filter, model.message_filter, clamp, false);
  callbacks.capture_item("MessagesFilterBus", "QLineEdit", std::string(""), std::string("Filter Bus"));
  ImGui::SameLine(209.0f, 0.0f);
  drawMessagesFilter("##MessagesFilterId", model.message_id_filter_buffer->data(), model.message_id_filter_buffer->size(),
                     "Filter ID", 104.0f, model.focus_message_filter, model.message_filter, clamp, false);
  callbacks.capture_item("MessagesFilterId", "QLineEdit", std::string(""), std::string("Filter ID"));
  ImGui::SameLine(313.0f, 0.0f);
  drawMessagesFilter("##MessagesFilterNode", model.message_node_filter_buffer->data(), model.message_node_filter_buffer->size(),
                     "Filter Node", 104.0f, model.focus_message_filter, model.message_filter, clamp, false);
  callbacks.capture_item("MessagesFilterNode", "QLineEdit", std::string(""), std::string("Filter Node"));
  ImGui::SameLine(417.0f, 0.0f);
  drawMessagesFilter("##MessagesFilterFreq", model.message_freq_filter_buffer->data(), model.message_freq_filter_buffer->size(),
                     "Filter Freq", 104.0f, model.focus_message_filter, model.message_filter, clamp, false);
  callbacks.capture_item("MessagesFilterFreq", "QLineEdit", std::string(""), std::string("Filter Freq"));
  ImGui::SameLine(521.0f, 0.0f);
  drawMessagesFilter("##MessagesFilterCount", model.message_count_filter_buffer->data(), model.message_count_filter_buffer->size(),
                     "Filter Count", 104.0f, model.focus_message_filter, model.message_filter, clamp, false);
  callbacks.capture_item("MessagesFilterCount", "QLineEdit", std::string(""), std::string("Filter Count"));
  ImGui::SameLine(625.0f, 0.0f);
  drawMessagesFilter("##MessagesFilterBytes", model.message_bytes_filter_buffer->data(), model.message_bytes_filter_buffer->size(),
                     "Filter Bytes", 198.0f, model.focus_message_filter, model.message_filter, clamp, false);
  callbacks.capture_item("MessagesFilterBytes", "QLineEdit", std::string(""), std::string("Filter Bytes"));
  ImGui::EndChild();

  ImGui::BeginChild("MessageTable", ImVec2(0.0f, 0.0f), false);
  callbacks.capture_window_rect("MessageTable", "QTableView");
  ImGui::BeginChild("MessageTableScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
  const auto visible = filteredMessageIndices(model);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  for (int index : visible) {
    auto &message = (*model.messages)[index];
    const bool selected = *model.detail_visible && index == *model.selected_message_index;
    ImGui::PushID(index);
    ImVec2 row_min = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##message_row", selected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(-1.0f, 25.0f))) {
      *model.selected_message_index = index;
      *model.detail_visible = true;
    }
    (*model.row_snapshots)[message.messageId()].rect = callbacks.current_item_rect();
    const ImU32 text_color = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(32, 32, 32, 255);
    draw_list->AddText(ImVec2(row_min.x + 6.0f, row_min.y + 5.0f), text_color, message.name.c_str());
    draw_list->AddText(ImVec2(row_min.x + 110.0f, row_min.y + 5.0f), text_color, std::to_string(message.source).c_str());
    char id_buffer[16];
    std::snprintf(id_buffer, sizeof(id_buffer), "%X", message.address);
    draw_list->AddText(ImVec2(row_min.x + 210.0f, row_min.y + 5.0f), text_color, id_buffer);
    draw_list->AddText(ImVec2(row_min.x + 262.0f, row_min.y + 5.0f), text_color, message.node.c_str());
    const auto freq_text = formatFrequency(message.freq);
    const auto count_text = std::to_string(message.count);
    const auto bytes_text = formatBytes(message);
    draw_list->AddText(ImVec2(row_min.x + 325.0f, row_min.y + 5.0f), text_color, freq_text.c_str());
    draw_list->AddText(ImVec2(row_min.x + 372.0f, row_min.y + 5.0f), text_color, count_text.c_str());
    draw_list->AddText(ImVec2(row_min.x + 425.0f, row_min.y + 5.0f), text_color, bytes_text.c_str());
    ImGui::PopID();
  }

  ImGui::EndChild();
  ImGui::EndChild();
}

}  // namespace imgui_cabana
