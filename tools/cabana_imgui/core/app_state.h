#pragma once

#include <atomic>
#include <string>
#include "core/types.h"

namespace cabana {

struct AppState {
  std::atomic<bool> quit_requested{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> segments_merged{false};

  // Route info (set after load)
  std::string route_name;
  std::string car_fingerprint;
  bool route_loading = false;
  std::string route_load_error;
  double current_sec = 0;
  double min_sec = 0;
  double max_sec = 0;
  float speed = 1.0f;
  int cached_minutes = 30;
  int fps = 10;

  // Selection state
  bool has_selection = false;
  MessageId selected_msg;
};

AppState &app_state();

}  // namespace cabana
