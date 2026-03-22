#pragma once

#include <string>

#include "sources/live_source.h"

namespace cabana {

struct SocketCanSourceConfig {
  std::string device;
};

class SocketCanSource : public LiveSource {
public:
  explicit SocketCanSource(SocketCanSourceConfig config = {});
  ~SocketCanSource() override;

  static bool available();

protected:
  bool prepare(std::string *error) override;
  void runThread() override;
  void interrupt() override;
  void cleanup() override;

private:
  SocketCanSourceConfig config_;
  int sock_fd_ = -1;
};

}  // namespace cabana
