#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/imgui_cabana/ui_types.h"

namespace imgui_cabana {

struct WidgetCallbacks {
  std::function<void(const std::string &, const std::string &)> capture_window_rect;
  std::function<void(const std::string &, const std::string &, std::optional<std::string>, std::optional<std::string>)> capture_item;
  std::function<Rect()> current_item_rect;
};

struct MessagesWidgetModel {
  std::vector<MessageData> *messages = nullptr;
  int *selected_message_index = nullptr;
  bool *detail_visible = nullptr;
  std::string *message_filter = nullptr;
  std::array<char, 128> *message_filter_buffer = nullptr;
  std::array<char, 32> *message_bus_filter_buffer = nullptr;
  std::array<char, 32> *message_id_filter_buffer = nullptr;
  std::array<char, 32> *message_node_filter_buffer = nullptr;
  std::array<char, 32> *message_freq_filter_buffer = nullptr;
  std::array<char, 32> *message_count_filter_buffer = nullptr;
  std::array<char, 96> *message_bytes_filter_buffer = nullptr;
  bool *focus_message_filter = nullptr;
  std::unordered_map<std::string, RowSnapshot> *row_snapshots = nullptr;
};

std::vector<int> filteredMessageIndices(const MessagesWidgetModel &model);
void clampSelection(MessagesWidgetModel &model);
void drawMessagesPane(MessagesWidgetModel &model, const WidgetCallbacks &callbacks);

}  // namespace imgui_cabana
