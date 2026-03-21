#include "ui/bootstrap_icons.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "imgui.h"

namespace cabana {
namespace icons {
namespace {
namespace fs = std::filesystem;

struct IconEntry {
  const char *id;
  const char *utf8;
};

// Sorted by id for binary search — all icons used in Qt cabana
constexpr std::array<IconEntry, 30> kIcons = {{
  {"arrow-clockwise",          "\xef\x84\x96"},  // U+F116
  {"arrow-counterclockwise",   "\xef\x84\x97"},  // U+F117
  {"arrow-down-left-square",   "\xef\x84\x9d"},  // U+F11D
  {"arrow-up-right-square",    "\xef\x85\x83"},  // U+F143
  {"chevron-left",             "\xef\x8a\x84"},  // U+F284
  {"chevron-right",            "\xef\x8a\x85"},  // U+F285
  {"dash",                     "\xef\x8b\xaa"},  // U+F2EA
  {"dash-square",              "\xef\x8b\xa9"},  // U+F2E9
  {"exclamation-triangle",     "\xef\x8c\xbb"},  // U+F33B
  {"fast-forward",             "\xef\x9f\xb4"},  // U+F7F4
  {"file-earmark-ruled",       "\xef\x8e\x85"},  // U+F385
  {"file-plus",                "\xef\x8e\xab"},  // U+F3AB
  {"graph-up",                 "\xef\x8f\xb2"},  // U+F3F2
  {"grip-horizontal",          "\xef\x8f\xbd"},  // U+F3FD
  {"info-circle",              "\xef\x90\xb1"},  // U+F431
  {"list",                     "\xef\x91\xb9"},  // U+F479
  {"pause",                    "\xef\x93\x84"},  // U+F4C4
  {"pencil",                   "\xef\x93\x8b"},  // U+F4CB
  {"play",                     "\xef\x93\xb5"},  // U+F4F5
  {"plus",                     "\xef\x93\xbe"},  // U+F4FE
  {"repeat",                   "\xef\xa0\x93"},  // U+F813
  {"repeat-1",                 "\xef\xa0\x92"},  // U+F812
  {"rewind",                   "\xef\xa0\x99"},  // U+F819
  {"save",                     "\xef\x94\xa5"},  // U+F525
  {"skip-end",                 "\xef\x95\x98"},  // U+F558
  {"stopwatch",                "\xef\x96\x97"},  // U+F597
  {"window-stack",             "\xef\x9b\x92"},  // U+F6D2
  {"x",                        "\xef\x98\xaa"},  // U+F62A
  {"x-lg",                     "\xef\x99\x99"},  // U+F659
  {"zoom-out",                 "\xef\x98\xad"},  // U+F62D
}};

fs::path repo_root() {
  std::array<char, 4096> buf = {};
  const ssize_t count = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (count <= 0) return {};
  return fs::path(std::string(buf.data(), static_cast<size_t>(count)))
      .parent_path().parent_path().parent_path();
}

}  // namespace

void load_font(float size) {
  const fs::path ttf = repo_root() / "third_party" / "bootstrap" / "bootstrap-icons.ttf";
  if (!fs::exists(ttf)) {
    fprintf(stderr, "bootstrap-icons.ttf not found at %s\n", ttf.c_str());
    return;
  }

  ImGuiIO &io = ImGui::GetIO();
  ImFontConfig config;
  config.MergeMode = true;
  config.GlyphMinAdvanceX = 0;  // use natural glyph width
  static const ImWchar ranges[] = {0xF000, 0xF8FF, 0};
  io.Fonts->AddFontFromFileTTF(ttf.c_str(), size, &config, ranges);
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
  const char *ic = glyph(icon_id);
  std::string text;
  if (ic[0] != '\0') {
    text = std::string(ic) + "  " + label;
  } else {
    text = std::string("   ") + label;
  }
  return ImGui::MenuItem(text.c_str(), shortcut, selected, enabled);
}

bool icon(std::string_view icon_id) {
  const char *ic = glyph(icon_id);
  if (ic[0] != '\0') {
    ImGui::TextUnformatted(ic);
    return true;
  }
  return false;
}

void icon_text(std::string_view icon_id, const char *text) {
  const char *ic = glyph(icon_id);
  if (ic[0] != '\0') {
    ImGui::TextUnformatted(ic);
    ImGui::SameLine();
  }
  ImGui::TextUnformatted(text);
}

}  // namespace icons
}  // namespace cabana
