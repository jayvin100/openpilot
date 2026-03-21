#include "ui/panes/charts_pane.h"

#include <string>
#include "imgui.h"
#include "implot.h"
#include "ui/bootstrap_icons.h"

namespace cabana {
namespace panes {

static bool icon_button(const char *id, std::string_view icon_id) {
  const char *g = cabana::icons::glyph(icon_id);
  if (g[0] == '\0') return ImGui::Button(id);
  std::string label = std::string(g) + "##" + id;
  return ImGui::Button(label.c_str());
}

void charts() {
  ImGui::Begin("Charts");

  // Charts control row (matches Qt cabana toolbar)
  icon_button("new_chart", "file-plus");
  ImGui::SameLine();
  icon_button("new_tab", "window-stack");
  ImGui::SameLine();
  ImGui::Text("Charts: 0");
  ImGui::SameLine();
  ImGui::TextDisabled("Type: Line");
  ImGui::SameLine();

  // Time range slider
  static float chart_range = 7.0f;
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##range", &chart_range, 1.0f, 60.0f, "%.0f s");

  ImGui::SameLine();
  icon_button("undo_zoom", "arrow-counterclockwise");
  ImGui::SameLine();
  icon_button("redo_zoom", "arrow-clockwise");
  ImGui::SameLine();
  icon_button("reset_zoom", "zoom-out");
  ImGui::SameLine();
  icon_button("remove_all", "x-lg");

  ImGui::Separator();

  // Chart area — only render ImPlot when there's actual data to show
  ImGui::TextDisabled("No charts. Double-click a message to add one.");

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
