#pragma once

#include <string_view>

#include "imgui.h"

namespace jotpluggler::bootstrap_icons {

void draw(std::string_view icon_id, ImVec2 pos, float size, ImU32 color, ImDrawList *draw_list = nullptr);
bool menu_item(std::string_view icon_id,
               const char *label,
               const char *shortcut = nullptr,
               bool selected = false,
               bool enabled = true);

}  // namespace jotpluggler::bootstrap_icons
