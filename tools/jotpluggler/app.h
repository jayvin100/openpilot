#pragma once

#include <string>

namespace jotpluggler {

struct Options {
  std::string layout;
  std::string route_name;
  std::string data_dir;
  std::string output_path;
  int width = 1600;
  int height = 900;
  bool show = false;
};

int run(const Options &options);

}  // namespace jotpluggler
