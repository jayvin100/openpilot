#include "tools/imgui_cabana/videowidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

namespace imgui_cabana {

std::string formatPlaybackTime(double sec, bool with_millis) {
  const int total_minutes = static_cast<int>(sec) / 60;
  const int total_seconds = static_cast<int>(sec) % 60;
  if (!with_millis) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", total_minutes, total_seconds);
    return buffer;
  }
  const int millis = static_cast<int>(std::round((sec - std::floor(sec)) * 1000.0));
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d", total_minutes, total_seconds, millis);
  return buffer;
}

void drawPlaybackToolbar(PlaybackToolbarModel &model, const WidgetCallbacks &callbacks) {
  ImGui::BeginChild("PlaybackToolbar", ImVec2(0.0f, 33.0f), false);
  callbacks.capture_window_rect("PlaybackToolbar", "QWidget");

  if (ImGui::Button("<<", ImVec2(26.0f, 23.0f))) *model.current_sec = std::max(*model.min_sec, *model.current_sec - 1.0);
  callbacks.capture_item("PlaybackSeekBackwardButton", "QToolButton", "<<", std::nullopt);
  ImGui::SameLine(0.0f, 4.0f);

  if (ImGui::Button(*model.paused ? ">" : "||", ImVec2(26.0f, 23.0f))) *model.paused = !*model.paused;
  callbacks.capture_item("PlaybackPlayToggleButton", "QToolButton", std::string(*model.paused ? ">" : "||"), std::nullopt);
  ImGui::SameLine(0.0f, 4.0f);

  if (ImGui::Button(">>", ImVec2(26.0f, 23.0f))) *model.current_sec = std::min(*model.max_sec, *model.current_sec + 1.0);
  callbacks.capture_item("PlaybackSeekForwardButton", "QToolButton", ">>", std::nullopt);
  ImGui::SameLine(0.0f, 4.0f);

  std::string time_display = formatPlaybackTime(*model.current_sec, true) + " / " + formatPlaybackTime(*model.max_sec, false);
  ImGui::Button(time_display.c_str(), ImVec2(118.0f, 23.0f));
  callbacks.capture_item("PlaybackTimeDisplayButton", "QToolButton", time_display, std::nullopt);

  const float speed_width = 52.0f;
  float right_x = ImGui::GetWindowContentRegionMax().x - speed_width;
  if (right_x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right_x);
  ImGui::Button("1x  ", ImVec2(speed_width, 23.0f));
  callbacks.capture_item("PlaybackSpeedButton", "QToolButton", "1x  ", std::nullopt);
  ImGui::EndChild();
}

void drawVideoPane(VideoWidgetModel &model, const WidgetCallbacks &callbacks) {
  const float kPlaybackToolbarHeight = 33.0f;
  const float kPlaybackSliderHeight = 15.0f;
  const float kVideoHeight = 150.0f;
  const float content_width = ImGui::GetContentRegionAvail().x;
  const float camera_height = std::max(60.0f, kVideoHeight - kPlaybackToolbarHeight - kPlaybackSliderHeight);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 camera_pos = ImGui::GetCursorScreenPos();
  ImVec2 camera_max = ImVec2(camera_pos.x + content_width, camera_pos.y + camera_height);
  draw_list->AddRectFilled(camera_pos, camera_max, IM_COL32(40, 40, 40, 255));
  draw_list->AddText(ImVec2(camera_pos.x + 10.0f, camera_pos.y + 10.0f), IM_COL32(220, 220, 220, 255), "roadCameraState");
  float indicator_x = camera_pos.x + 12.0f + static_cast<float>((*model.playback_toolbar.current_sec - *model.playback_toolbar.min_sec) /
                      std::max(0.001, *model.playback_toolbar.max_sec - *model.playback_toolbar.min_sec)) * (content_width - 24.0f);
  draw_list->AddLine(ImVec2(indicator_x, camera_pos.y + 8.0f), ImVec2(indicator_x, camera_max.y - 8.0f), IM_COL32(47, 101, 202, 255), 2.0f);
  ImGui::Dummy(ImVec2(content_width, camera_height));

  ImGui::BeginChild("PlaybackSliderRegion", ImVec2(0.0f, kPlaybackSliderHeight), false);
  ImGui::SetNextItemWidth(-1.0f);
  float slider_sec = static_cast<float>(*model.playback_toolbar.current_sec);
  if (ImGui::SliderFloat("##PlaybackSlider", &slider_sec, static_cast<float>(*model.playback_toolbar.min_sec),
                         static_cast<float>(*model.playback_toolbar.max_sec), "")) {
    *model.playback_toolbar.current_sec = slider_sec;
    *model.playback_toolbar.paused = true;
  }
  callbacks.capture_item("PlaybackSlider", "QSlider", std::nullopt, std::nullopt);
  *model.playback_slider_rect = callbacks.current_item_rect();
  ImGui::EndChild();

  drawPlaybackToolbar(model.playback_toolbar, callbacks);
}

}  // namespace imgui_cabana
