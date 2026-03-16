#pragma once

#include <functional>

#include "tools/imgui_cabana/messageswidget.h"
#include "tools/imgui_cabana/signalview.h"

namespace imgui_cabana {

struct DetailWidgetModel {
  std::vector<MessageData> *messages = nullptr;
  int *selected_message_index = nullptr;
  SignalViewModel signal_view;
};

struct DetailWidgetCallbacks {
  WidgetCallbacks widget;
  std::function<void()> open_edit_dialog;
};

void drawDetailPane(DetailWidgetModel &model, const DetailWidgetCallbacks &callbacks);

}  // namespace imgui_cabana
