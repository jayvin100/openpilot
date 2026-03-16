#pragma once

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/imgui_cabana/messageswidget.h"

namespace imgui_cabana {

struct SignalViewModel {
  std::vector<MessageData> *messages = nullptr;
  int *selected_message_index = nullptr;
  std::string *signal_filter = nullptr;
  std::array<char, 128> *signal_filter_buffer = nullptr;
  bool *focus_signal_filter = nullptr;
  std::unordered_map<std::string, RowSnapshot> *signal_snapshots = nullptr;
};

void drawSignalView(SignalViewModel &model, const WidgetCallbacks &callbacks);

}  // namespace imgui_cabana
