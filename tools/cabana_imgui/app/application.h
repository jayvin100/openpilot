#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

#include "sources/replay_source.h"

class Application {
public:
  ~Application();
  bool init(int argc, char *argv[]);
  int run();
  void shutdown();

  cabana::ReplaySource *source() { return source_.get(); }

private:
  bool parseArgs(int argc, char *argv[]);

  GLFWwindow *window = nullptr;
  std::unique_ptr<cabana::ReplaySource> source_;

  // CLI args
  std::string route_;
  std::string dbc_file_;
  std::string data_dir_;
  uint32_t replay_flags_ = 0;
  bool auto_source_ = false;
};

// Global access for UI panes
Application *app();
