#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace imgui_cabana {

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct RowSnapshot {
  Rect rect;
  Rect plot_rect;
  Rect remove_rect;
};

struct MessageSample {
  double sec = 0.0;
  std::vector<uint8_t> bytes;
};

struct SignalData {
  std::string name;
  std::string value;
  bool plotted = false;
  int byte_index = -1;
};

struct MessageData {
  uint32_t address = 0;
  int source = 0;
  std::string name;
  std::string node;
  std::vector<uint8_t> bytes;
  std::vector<MessageSample> samples;
  uint64_t count = 0;
  double freq = 0.0;
  std::size_t active_sample_index = 0;
  std::vector<SignalData> signals;

  std::string messageId() const {
    return std::to_string(source) + ":" + std::to_string(address);
  }
};

struct DialogState {
  std::string title;
  std::string text;
  std::string detailed_text;
  Rect rect;
  bool modal = true;
  bool visible = false;
};

}  // namespace imgui_cabana
