#include "ui/panes/video_pane.h"

#include <cstdio>
#include <string>

#include "imgui.h"
#include "app/application.h"
#include "core/app_state.h"
#include "sources/replay_source.h"
#include "ui/bootstrap_icons.h"

namespace cabana {
namespace panes {

static bool icon_button(const char *id, std::string_view icon_id) {
  const char *g = cabana::icons::glyph(icon_id);
  if (g[0] == '\0') return ImGui::Button(id);
  std::string label = std::string(g) + "##" + id;
  return ImGui::Button(label.c_str());
}

void video() {
  ImGui::Begin("Video");

  auto &st = cabana::app_state();
  auto *src = app() ? app()->source() : nullptr;

  // Route info header
  if (!st.route_name.empty()) {
    ImGui::Text("ROUTE: %s", st.route_name.c_str());
    if (!st.car_fingerprint.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("FINGERPRINT: %s", st.car_fingerprint.c_str());
    }
  } else {
    ImGui::TextUnformatted("ROUTE:");
    ImGui::SameLine();
    ImGui::TextDisabled("(no route loaded)");
  }

  ImGui::Separator();

  // Video area (black)
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float video_h = avail.y * 0.55f;
  if (video_h < 60) video_h = 60;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + video_h),
                                            IM_COL32(0, 0, 0, 255));
  ImGui::Dummy(ImVec2(avail.x, video_h));

  // Timeline bar
  {
    float progress = 0;
    if (st.max_sec > st.min_sec) {
      progress = (float)((st.current_sec - st.min_sec) / (st.max_sec - st.min_sec));
    }
    ImVec2 tl_pos = ImGui::GetCursorScreenPos();
    float tl_w = avail.x;
    float tl_h = 10.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(tl_pos, ImVec2(tl_pos.x + tl_w, tl_pos.y + tl_h),
                      IM_COL32(40, 40, 40, 255));
    dl->AddRectFilled(tl_pos, ImVec2(tl_pos.x + tl_w * progress, tl_pos.y + tl_h),
                      IM_COL32(50, 180, 50, 255));
    ImGui::Dummy(ImVec2(tl_w, tl_h));
  }

  ImGui::Spacing();

  // Playback controls
  if (icon_button("rewind", "rewind") && src) {
    src->seekTo(src->currentSec() - 1);
  }
  ImGui::SameLine();
  bool is_paused = st.paused;
  if (icon_button("playpause", is_paused ? "play" : "pause") && src) {
    src->pause(!is_paused);
  }
  ImGui::SameLine();
  if (icon_button("ff", "fast-forward") && src) {
    src->seekTo(src->currentSec() + 1);
  }
  ImGui::SameLine();
  if (icon_button("skip", "skip-end") && src) {
    src->seekTo(st.max_sec);
  }
  ImGui::SameLine();

  // Time display
  int cur_min = (int)st.current_sec / 60;
  double cur_frac = st.current_sec - cur_min * 60;
  int max_min = (int)st.max_sec / 60;
  int max_sec_i = (int)st.max_sec % 60;
  ImGui::Text("%02d:%06.3f / %02d:%02d", cur_min, cur_frac, max_min, max_sec_i);

  ImGui::SameLine();
  icon_button("loop", "repeat");
  ImGui::SameLine();
  ImGui::Text("%.0fx", st.speed);
  ImGui::SameLine();
  icon_button("info", "info-circle");

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
