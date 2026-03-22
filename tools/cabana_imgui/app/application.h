#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

#include "core/types.h"
#include "sources/replay_source.h"

struct ReplayLoadState;

class Application {
public:
  ~Application();
  bool init(int argc, char *argv[]);
  int run();
  void shutdown();
  void openRoute(const std::string &route);
  void closeRoute();
  bool openDbcFile(const std::string &path);
  bool openDbcFile(const std::string &path, const SourceSet &sources);
  void closeDbcs(const SourceSet &sources);
  void closeDbcEverywhere(int source);
  bool saveDbc(int source = -1);
  bool saveDbcAs(const std::string &path, int source = -1);

  cabana::ReplaySource *source() { return source_.get(); }
  bool videoEnabled() const;

private:
  bool parseArgs(int argc, char *argv[]);
  void beginReplayLoad();
  void pollReplayLoad();
  void openConfiguredDbcs();

  GLFWwindow *window = nullptr;
  std::unique_ptr<cabana::ReplaySource> source_;
  std::shared_ptr<ReplayLoadState> replay_load_state_;

  // CLI args
  std::string route_;
  std::string dbc_file_;
  std::string data_dir_;
  std::string imgui_ini_path_;
  uint32_t replay_flags_ = 0;
  bool auto_source_ = false;
  bool shutdown_done_ = false;
};

// Global access for UI panes
Application *app();
