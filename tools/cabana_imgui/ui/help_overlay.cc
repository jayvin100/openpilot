#include "ui/help_overlay.h"

#include <algorithm>

#include "imgui.h"

#include "core/app_state.h"
#include "ui/theme.h"

namespace cabana {
namespace help_overlay {

namespace {

struct ShortcutRow {
  const char *action;
  const char *key;
  const char *notes;
};

void render_shortcuts(const char *title, const ShortcutRow *rows, size_t count) {
  ImGui::SeparatorText(title);
  if (ImGui::BeginTable(title, 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < count; ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(rows[i].action);
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", rows[i].key);
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", rows[i].notes);
    }
    ImGui::EndTable();
  }
}

}  // namespace

void render() {
  auto &st = cabana::app_state();
  if (!st.show_help_overlay) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    st.show_help_overlay = false;
    return;
  }

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float margin = 48.0f;
  ImVec2 panel_size(
      std::min(920.0f * cabana::theme::dpi_scale(), viewport->Size.x - margin * 2.0f),
      std::min(620.0f * cabana::theme::dpi_scale(), viewport->Size.y - margin * 2.0f));

  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::SetNextWindowBgAlpha(0.72f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  const ImGuiWindowFlags backdrop_flags = ImGuiWindowFlags_NoDecoration |
                                          ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoNavFocus;

  if (ImGui::Begin("##HelpOverlayBackdrop", nullptr, backdrop_flags)) {
    ImVec2 panel_pos(viewport->Pos.x + (viewport->Size.x - panel_size.x) * 0.5f,
                     viewport->Pos.y + (viewport->Size.y - panel_size.y) * 0.5f);
    ImGui::SetCursorScreenPos(panel_pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    if (ImGui::BeginChild("##HelpOverlayPanel", panel_size, true)) {
      if (ImFont *bold = cabana::theme::font_bold()) {
        ImGui::PushFont(bold, 0);
      }
      ImGui::TextUnformatted("Cabana Help");
      if (cabana::theme::font_bold()) {
        ImGui::PopFont();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("Current shortcuts and interaction hints");
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 54.0f);
      if (ImGui::Button("Close")) {
        st.show_help_overlay = false;
      }

      ImGui::Spacing();
      ImGui::TextWrapped("This rewrite is still in progress, but these interactions are already wired and stable.");

      static constexpr ShortcutRow global_rows[] = {
          {"Pause or resume replay", "Space", "Toggles playback while a route is open."},
          {"Open or close help", "F1", "Shows this overlay from anywhere in the app."},
          {"Close help", "Esc", "Dismisses the overlay without affecting playback."},
          {"Quit the app", "Ctrl+Q", "Closes the current Cabana window."},
      };
      render_shortcuts("Global", global_rows, std::size(global_rows));

      static constexpr ShortcutRow message_rows[] = {
          {"Select next message", "Down", "Moves selection through the filtered message list."},
          {"Select previous message", "Up", "Moves selection back through the filtered list."},
          {"Filter messages", "Search", "Matches message names case-insensitively."},
      };
      render_shortcuts("Messages", message_rows, std::size(message_rows));

      static constexpr ShortcutRow chart_rows[] = {
          {"Open a signal chart", "Plot", "Use the Plot checkbox in Signals or Binary."},
          {"Merge into selected chart", "Shift+click Plot", "Adds the signal into the currently selected chart."},
          {"Split a chart", "Split", "Separates multi-signal charts into single-signal cards."},
      };
      render_shortcuts("Charts", chart_rows, std::size(chart_rows));

      ImGui::SeparatorText("Current Build Notes");
      ImGui::BulletText("Route replay, detail views, history, and charts are wired.");
      ImGui::BulletText("Replay video playback is wired, including transport controls and the scrub timeline.");
      ImGui::BulletText("Live CAN sources are wired; live video and hardware-backed validation are still pending.");
      ImGui::BulletText("DBC editing and session restore are wired in the ImGui app.");
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

}  // namespace help_overlay
}  // namespace cabana
