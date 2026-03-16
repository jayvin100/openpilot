#include "tools/imgui_cabana/binaryview.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace imgui_cabana {

void drawBinaryView(const MessageData &message) {
  ImGui::BeginChild("BinaryView", ImVec2(0.0f, 170.0f), ImGuiChildFlags_Borders);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const int column_count = 4;
  const int row_count = std::max(1, static_cast<int>((message.bytes.size() + column_count - 1) / column_count));
  for (int row = 0; row < row_count; ++row) {
    for (int col = 0; col < column_count; ++col) {
      const int index = row * column_count + col;
      ImVec2 min = ImVec2(origin.x + col * 58.0f, origin.y + row * 28.0f);
      ImVec2 max = ImVec2(min.x + 54.0f, min.y + 24.0f);
      draw_list->AddRectFilled(min, max, IM_COL32(250, 250, 250, 255));
      draw_list->AddRect(min, max, IM_COL32(215, 215, 215, 255));
      if (index < static_cast<int>(message.bytes.size())) {
        char value[8];
        std::snprintf(value, sizeof(value), "%02X", message.bytes[index]);
        draw_list->AddText(ImVec2(min.x + 8.0f, min.y + 4.0f), IM_COL32(32, 32, 32, 255), value);
      }
    }
  }
  ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(140.0f, row_count * 30.0f)));
  ImGui::EndChild();
}

}  // namespace imgui_cabana
