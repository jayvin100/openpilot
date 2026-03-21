#include "tools/jotpluggler/bootstrap_icons.h"
#include "tools/jotpluggler/bootstrap_icons_font_data.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace jotpluggler::bootstrap_icons {
namespace {

struct IconEntry {
  const char *id;
  const char *utf8;
};

// Sorted by id for binary search
constexpr std::array<IconEntry, 15> kIcons = {{
  {"arrow-down-up",         "\xef\x84\xa7"},  // U+F127
  {"arrow-left-right",      "\xef\x84\xab"},  // U+F12B
  {"bar-chart",             "\xef\x85\xbe"},  // U+F17E
  {"clipboard2",            "\xef\x9c\xb3"},  // U+F733
  {"distribute-horizontal", "\xef\x8c\x83"},  // U+F303
  {"distribute-vertical",   "\xef\x8c\x84"},  // U+F304
  {"file-earmark-image",    "\xef\x8d\xad"},  // U+F36D
  {"files",                 "\xef\x8f\x82"},  // U+F3C2
  {"palette",               "\xef\x92\xb1"},  // U+F4B1
  {"plus-slash-minus",      "\xef\x9a\xaa"},  // U+F6AA
  {"save",                  "\xef\x94\xa5"},  // U+F525
  {"sliders",               "\xef\x95\xab"},  // U+F56B
  {"trash",                 "\xef\x97\x9e"},  // U+F5DE
  {"x-square",              "\xef\x98\xa9"},  // U+F629
  {"zoom-out",              "\xef\x98\xad"},  // U+F62D
}};

}  // namespace

void load_font(float size) {
  ImGuiIO &io = ImGui::GetIO();
  ImFontConfig config;
  config.MergeMode = true;
  config.GlyphMinAdvanceX = size;
  config.FontDataOwnedByAtlas = false;
  static const ImWchar ranges[] = {0xF000, 0xF8FF, 0};
  io.Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char *>(kBootstrapIconsFontData),
      static_cast<int>(kBootstrapIconsFontSize),
      size, &config, ranges);
}

const char *glyph(std::string_view icon_id) {
  auto it = std::lower_bound(kIcons.begin(), kIcons.end(), icon_id,
                             [](const IconEntry &e, std::string_view id) {
                               return std::strcmp(e.id, id.data()) < 0;
                             });
  if (it != kIcons.end() && icon_id == it->id) {
    return it->utf8;
  }
  return "";
}

bool menu_item(std::string_view icon_id,
               const char *label,
               const char *shortcut,
               bool selected,
               bool enabled) {
  const char *icon = glyph(icon_id);
  std::string text;
  if (icon[0] != '\0') {
    text = std::string(icon) + "  " + label;
  } else {
    text = std::string("   ") + label;
  }
  return ImGui::MenuItem(text.c_str(), shortcut, selected, enabled);
}

}  // namespace jotpluggler::bootstrap_icons
