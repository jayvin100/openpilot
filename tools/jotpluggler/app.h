#pragma once

#include <string>

namespace jotpluggler {

enum class Mode {
  MockReference,
  Sketch,
};

struct Options {
  Mode mode = Mode::MockReference;
  std::string layout;
  std::string output_path;
  std::string baseline_root;
  std::string python = "python3";
  int width = 1600;
  int height = 900;
  bool show = false;
};

int run(const Options &options);

}  // namespace jotpluggler
