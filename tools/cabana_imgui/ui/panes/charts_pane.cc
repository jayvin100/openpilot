#include "ui/panes/charts_pane.h"

#include "imgui.h"
#include "implot.h"

namespace cabana {
namespace panes {

void charts() {
  ImGui::Begin("Charts");

  // Charts control row (matches Qt cabana)
  ImGui::Text("Charts: 0");
  ImGui::SameLine();
  ImGui::TextDisabled("Type: Line");
  ImGui::SameLine();

  // Time range slider
  static float chart_range = 7.0f;
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("##range", &chart_range, 1.0f, 60.0f, "%.0f s");

  ImGui::Separator();

  // Chart area
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.y > 20 && ImPlot::BeginPlot("##charts", avail, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus)) {
    ImPlot::EndPlot();
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
