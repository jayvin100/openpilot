#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tools/imgui_cabana/ui_types.h"

namespace imgui_cabana {

struct ReplayLoadResult {
  bool success = false;
  std::string route_name;
  std::string fingerprint;
  std::string error;
  std::vector<MessageData> messages;
  double min_sec = 0.0;
  double max_sec = 0.0;
};

ReplayLoadResult loadReplayRoute(const std::string &route, const std::optional<std::string> &data_dir);
void syncReplayMessages(std::vector<MessageData> &messages, double current_sec);

}  // namespace imgui_cabana
