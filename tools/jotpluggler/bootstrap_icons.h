// Bootstrap Icons - font-based rendering for Dear ImGui
// https://github.com/twbs/icons
#pragma once

#include <string_view>

#include "imgui.h"

namespace jotpluggler::bootstrap_icons {

// Call once during ImGui init, after adding the primary font.
// Merges the Bootstrap Icons font into the current font atlas.
void load_font(float size);

// Returns the UTF-8 string for an icon ID (e.g. "trash" → "\xef\x97\x9e").
// Returns empty string if not found.
const char *glyph(std::string_view icon_id);

// ImGui::MenuItem with a leading icon. Same API as before.
bool menu_item(std::string_view icon_id,
               const char *label,
               const char *shortcut = nullptr,
               bool selected = false,
               bool enabled = true);

}  // namespace jotpluggler::bootstrap_icons
