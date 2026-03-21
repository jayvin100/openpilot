#include "ui/theme.h"

#include "imgui.h"

namespace cabana {
namespace theme {

static ImFont *s_font_default = nullptr;
static ImFont *s_font_bold = nullptr;
static ImFont *s_font_splash = nullptr;

// System font paths (NotoSans matches what Qt uses on Linux)
static const char *FONT_REGULAR = "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf";
static const char *FONT_BOLD = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf";

void load_fonts() {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();

  // Base UI font — 13px matches Qt's rendered size at default DPI
  s_font_default = io.Fonts->AddFontFromFileTTF(FONT_REGULAR, 13.0f);
  if (!s_font_default) {
    // Fallback to imgui built-in
    s_font_default = io.Fonts->AddFontDefault();
  }

  // Bold font for headers and emphasis
  s_font_bold = io.Fonts->AddFontFromFileTTF(FONT_BOLD, 13.0f);
  if (!s_font_bold) s_font_bold = s_font_default;

  // Large font for the CABANA splash text
  s_font_splash = io.Fonts->AddFontFromFileTTF(FONT_BOLD, 56.0f);
  if (!s_font_splash) s_font_splash = s_font_default;
}

void apply() {
  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();

  // Match Qt's tight spacing
  style.WindowPadding = ImVec2(4, 4);
  style.FramePadding = ImVec2(4, 1);
  style.CellPadding = ImVec2(4, 1);
  style.ItemSpacing = ImVec2(4, 2);
  style.ItemInnerSpacing = ImVec2(4, 2);
  style.IndentSpacing = 12.0f;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 8.0f;

  // Subtle rounding (Qt uses 0 or very small)
  style.WindowRounding = 0.0f;
  style.FrameRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.TabRounding = 2.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;

  // Smaller tabs and separators
  style.TabBarBorderSize = 1.0f;
  style.SeparatorTextBorderSize = 1.0f;

  // Colors matching Qt cabana dark theme
  ImVec4 *c = style.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
  c[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
  c[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.18f, 0.18f, 0.98f);
  c[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.28f, 0.50f);
  c[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
  c[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.5f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
  c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
  c[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.50f);
  c[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
  c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  c[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
  c[ImGuiCol_TabDimmed] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
  c[ImGuiCol_TabDimmedSelected] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
  c[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
  c[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
  c[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
  c[ImGuiCol_Text] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
  c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
}

ImFont *font_default() { return s_font_default; }
ImFont *font_bold() { return s_font_bold; }
ImFont *font_splash() { return s_font_splash; }

}  // namespace theme
}  // namespace cabana
