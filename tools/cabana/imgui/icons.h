#pragma once

#include "imgui.h"

enum class BootstrapIcon {
  Play,
  Pause,
  Rewind,
  FastForward,
  SkipEnd,
  SkipEndFill,
  Repeat,
  X,
  XLg,
  Pencil,
  Plus,
  Dash,
  Gear,
  GraphUp,
  ArrowCounterclockwise,
  ArrowClockwise,
  ZoomOut,
  InfoCircle,
  ChevronLeft,
  ChevronRight,
  FileEarmarkRuled,
  Stopwatch,
  GripHorizontal,
  ThreeDots,
  WindowStack,
  DashSquare,
  FilePlus,
  List,
  _Count,
};

void initBootstrapIcons(const char *svg_path, float icon_size = 0);
void destroyBootstrapIcons();
ImTextureID getBootstrapIcon(BootstrapIcon icon);
bool iconButton(const char *str_id, BootstrapIcon icon, const char *tooltip = nullptr);
bool smallIconButton(const char *str_id, BootstrapIcon icon, const char *tooltip = nullptr);
void drawIcon(BootstrapIcon icon, float size = 0);
