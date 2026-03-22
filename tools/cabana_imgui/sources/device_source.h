#pragma once

#include <memory>
#include <string>

#include "msgq/ipc.h"
#include "sources/live_source.h"

namespace cabana {

struct DeviceSourceConfig {
  bool use_zmq = false;
  std::string address = "127.0.0.1";
};

class DeviceSource : public LiveSource {
public:
  explicit DeviceSource(DeviceSourceConfig config = {});
  ~DeviceSource() override;

protected:
  bool prepare(std::string *error) override;
  void runThread() override;
  void cleanup() override;

private:
  bool startBridge(std::string *error);
  void stopBridge();
  std::string bridgePath() const;

  DeviceSourceConfig config_;
  std::unique_ptr<Context> context_;
  std::unique_ptr<SubSocket> sock_;
  int bridge_pid_ = -1;
};

}  // namespace cabana
