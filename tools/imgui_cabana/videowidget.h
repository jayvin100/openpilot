#pragma once

#include <string>

#include "tools/imgui_cabana/messageswidget.h"

namespace imgui_cabana {

struct PlaybackToolbarModel {
  double *current_sec = nullptr;
  double *min_sec = nullptr;
  double *max_sec = nullptr;
  bool *paused = nullptr;
};

struct VideoWidgetModel {
  PlaybackToolbarModel playback_toolbar;
  Rect *playback_slider_rect = nullptr;
};

std::string formatPlaybackTime(double sec, bool with_millis);
void drawPlaybackToolbar(PlaybackToolbarModel &model, const WidgetCallbacks &callbacks);
void drawVideoPane(VideoWidgetModel &model, const WidgetCallbacks &callbacks);

}  // namespace imgui_cabana
