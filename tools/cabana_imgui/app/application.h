#pragma once

#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

#include "core/types.h"
#include "sources/device_source.h"
#include "sources/panda_source.h"
#include "sources/replay_source.h"
#include "sources/socketcan_source.h"
#include "sources/source.h"

struct ReplayLoadState;

class Application {
public:
  ~Application();
  bool init(int argc, char *argv[]);
  int run();
  void shutdown();
  void openRoute(const std::string &route, const std::string &data_dir = {},
                 uint32_t replay_flags = 0, bool auto_source = false);
  bool openDeviceStream(const cabana::DeviceSourceConfig &config, const std::string &dbc_path = {});
  bool openPandaStream(const cabana::PandaSourceConfig &config, const std::string &dbc_path = {});
  bool openSocketCanStream(const cabana::SocketCanSourceConfig &config, const std::string &dbc_path = {});
  void closeRoute();
  bool newDbcFile(const SourceSet &sources);
  bool openDbcFile(const std::string &path);
  bool openDbcFile(const std::string &path, const SourceSet &sources);
  bool loadDbcFromClipboard(const SourceSet &sources);
  bool copyDbcToClipboard(int source = -1);
  bool exportCsv(const std::string &path, std::optional<MessageId> msg_id = std::nullopt);
  void closeDbcs(const SourceSet &sources);
  void closeDbcEverywhere(int source);
  bool saveDbc(int source = -1);
  bool saveDbcAs(const std::string &path, int source = -1);

  cabana::Source *source() { return source_.get(); }
  bool videoEnabled() const;

private:
  bool parseArgs(int argc, char *argv[]);
  void beginReplayLoad();
  void pollReplayLoad();
  void openConfiguredDbcs();
  bool activateSource(std::unique_ptr<cabana::Source> source, const std::string &dbc_path = {});
  void resetSourceState();

  GLFWwindow *window = nullptr;
  std::unique_ptr<cabana::Source> source_;
  std::shared_ptr<ReplayLoadState> replay_load_state_;

  // CLI args
  std::string route_;
  std::string dbc_file_;
  std::string data_dir_;
  std::string imgui_ini_path_;
  uint32_t replay_flags_ = 0;
  bool auto_source_ = false;
  bool use_device_stream_ = false;
  bool use_panda_stream_ = false;
  bool use_socketcan_stream_ = false;
  cabana::DeviceSourceConfig device_config_;
  cabana::PandaSourceConfig panda_config_;
  cabana::SocketCanSourceConfig socketcan_config_;
  bool shutdown_done_ = false;
};

// Global access for UI panes
Application *app();
