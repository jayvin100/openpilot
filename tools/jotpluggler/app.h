#pragma once

#include <string>

namespace jotpluggler {

struct Options {
  std::string layout;
  std::string route_name;
  std::string data_dir;
  std::string output_path;
  std::string stream_address = "127.0.0.1";
  int width = 1600;
  int height = 900;
  bool show = false;
  bool sync_load = false;
  bool stream = false;
  double stream_buffer_seconds = 30.0;
};

int run(const Options &options);

}  // namespace jotpluggler
