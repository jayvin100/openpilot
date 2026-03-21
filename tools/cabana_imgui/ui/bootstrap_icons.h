// Bootstrap Icons - font-based rendering for Dear ImGui
// https://github.com/twbs/icons
#pragma once

#include <string_view>

namespace cabana {
namespace icons {

// Call once during ImGui init, after adding the primary font.
// Merges the Bootstrap Icons font into the current font atlas.
void load_font(float size);

// Returns the UTF-8 string for an icon ID (e.g. "play" -> "\xef\x93\xb5").
// Returns empty string if not found.
const char *glyph(std::string_view icon_id);

// ImGui::MenuItem with a leading icon.
bool menu_item(std::string_view icon_id,
               const char *label,
               const char *shortcut = nullptr,
               bool selected = false,
               bool enabled = true);

// Render just the icon as text. Returns true if icon was found.
bool icon(std::string_view icon_id);

// Icon + SameLine + text helper for buttons/labels
void icon_text(std::string_view icon_id, const char *text);

}  // namespace icons
}  // namespace cabana
