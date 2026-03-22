#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

#include "sources/replay_source.h"

struct ReplayLoadState;

class Application {
public:
  ~Application();
  bool init(int argc, char *argv[]);
  int run();
  void shutdown();

  cabana::ReplaySource *source() { return source_.get(); }
  bool videoEnabled() const;

private:
  bool parseArgs(int argc, char *argv[]);
  void beginReplayLoad();
  void pollReplayLoad();

  GLFWwindow *window = nullptr;
  std::unique_ptr<cabana::ReplaySource> source_;
  std::shared_ptr<ReplayLoadState> replay_load_state_;

  // CLI args
  std::string route_;
  std::string dbc_file_;
  std::string data_dir_;
  uint32_t replay_flags_ = 0;
  bool auto_source_ = false;
};

// Global access for UI panes
Application *app();
