#include "ui/panes/video_pane.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "app/application.h"
#include "core/app_state.h"
#include "ui/bootstrap_icons.h"

namespace cabana {
namespace panes {

static bool icon_button(const char *id, std::string_view icon_id) {
  const char *g = cabana::icons::glyph(icon_id);
  if (g[0] == '\0') return ImGui::Button(id);
  std::string label = std::string(g) + "##" + id;
  return ImGui::Button(label.c_str());
}

static std::string ellipsize_middle(const std::string &text, float max_width) {
  if (text.empty() || ImGui::CalcTextSize(text.c_str()).x <= max_width) {
    return text;
  }

  constexpr const char *ellipsis = "...";
  if (ImGui::CalcTextSize(ellipsis).x > max_width) {
    return {};
  }

  size_t head = text.size() / 2;
  size_t tail = text.size() - head;
  while (head > 2 && tail > 2) {
    std::string result = text.substr(0, head) + ellipsis + text.substr(text.size() - tail);
    if (ImGui::CalcTextSize(result.c_str()).x <= max_width) {
      return result;
    }
    if (head >= tail) {
      --head;
    } else {
      --tail;
    }
  }

  return ellipsis;
}

static void render_meta_row(const char *label, const std::string &value) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(74.0f);
  const float max_width = std::max(40.0f, ImGui::GetContentRegionAvail().x);
  const std::string display = ellipsize_middle(value, max_width);
  ImGui::TextUnformatted(display.c_str());
  if (display != value && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", value.c_str());
  }
}

static void draw_empty_video_state(const char *headline, const char *detail) {
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float video_h = std::max(120.0f, avail.y * 0.55f);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + video_h), IM_COL32(7, 7, 7, 255), 2.0f);
  dl->AddRect(p, ImVec2(p.x + avail.x, p.y + video_h), IM_COL32(70, 70, 70, 255), 2.0f);

  const ImVec2 headline_size = ImGui::CalcTextSize(headline);
  const ImVec2 detail_size = ImGui::CalcTextSize(detail);
  const float center_x = p.x + avail.x * 0.5f;
  const float center_y = p.y + video_h * 0.5f;

  dl->AddText(ImVec2(center_x - headline_size.x * 0.5f, center_y - 16.0f),
              IM_COL32(208, 208, 208, 255), headline);
  dl->AddText(ImVec2(center_x - detail_size.x * 0.5f, center_y + 6.0f),
              IM_COL32(145, 145, 145, 255), detail);
  ImGui::Dummy(ImVec2(avail.x, video_h));
}

void video() {
  ImGui::Begin("Video");

  auto &st = cabana::app_state();
  auto *src = app() ? app()->source() : nullptr;
  const bool video_enabled = app() ? app()->videoEnabled() : true;

  // Route info header
  if (!st.route_name.empty()) {
    render_meta_row("Route", st.route_name);
    if (!st.car_fingerprint.empty()) {
      render_meta_row("Fingerprint", st.car_fingerprint);
    }
  } else {
    render_meta_row("Route", st.route_loading ? "Loading route..." : "No route loaded");
  }

  if (!st.route_load_error.empty()) {
    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "load failed");
  }

  ImGui::Separator();

  if (!src) {
    draw_empty_video_state(st.route_loading ? "Loading route..." : "Video unavailable",
                           st.route_loading ? "Waiting for replay data" : "Open a route to view video");
  } else if (src->liveStreaming()) {
    draw_empty_video_state("Video unavailable", "Live video is not wired yet");
  } else if (!video_enabled) {
    draw_empty_video_state("Video disabled", "Replay started with --no-vipc");
  } else {
    draw_empty_video_state("Video preview unavailable", "Video rendering is not wired yet");
  }

  ImVec2 avail = ImGui::GetContentRegionAvail();

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
