#include "sources/device_source.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <limits.h>
#include <memory>
#include <string>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cereal/messaging/messaging.h"
#include "cereal/services.h"

namespace cabana {

DeviceSource::DeviceSource(DeviceSourceConfig config)
    : LiveSource("Live Streaming From " + (config.use_zmq ? config.address : std::string("127.0.0.1"))),
      config_(std::move(config)) {
}

DeviceSource::~DeviceSource() {
  stop();
}

bool DeviceSource::prepare(std::string *error) {
  if (config_.use_zmq) {
    setenv("ZMQ", "1", 1);
    if (!startBridge(error)) {
      return false;
    }
  } else {
    unsetenv("ZMQ");
  }

  context_.reset(Context::create());
  sock_.reset(SubSocket::create(context_.get(), "can", "127.0.0.1", false, true, services.at("can").queue_size));
  if (!sock_) {
    if (error) *error = "Failed to subscribe to CAN stream.";
    stopBridge();
    return false;
  }
  sock_->setTimeout(100);
  return true;
}

void DeviceSource::runThread() {
  while (!shouldStop()) {
    std::unique_ptr<Message> msg(sock_ ? sock_->receive(false) : nullptr);
    if (!msg) {
      continue;
    }

    AlignedBuffer aligned;
    handleEvent(aligned.align(msg.get()));
  }
}

void DeviceSource::cleanup() {
  sock_.reset();
  context_.reset();
  stopBridge();
}

bool DeviceSource::startBridge(std::string *error) {
  const std::string bridge = bridgePath();
  if (bridge.empty() || access(bridge.c_str(), X_OK) != 0) {
    if (error) *error = "Unable to locate cereal/messaging/bridge.";
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    if (error) *error = "Failed to fork bridge process.";
    return false;
  }

  if (pid == 0) {
    execl(bridge.c_str(), bridge.c_str(), config_.address.c_str(), "/\"can/\"", nullptr);
    _exit(127);
  }

  bridge_pid_ = pid;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return true;
}

void DeviceSource::stopBridge() {
  if (bridge_pid_ <= 0) {
    return;
  }

  kill(bridge_pid_, SIGTERM);
  int status = 0;
  waitpid(bridge_pid_, &status, 0);
  bridge_pid_ = -1;
}

std::string DeviceSource::bridgePath() const {
  char exe_path[PATH_MAX] = {};
  const ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) {
    return {};
  }

  std::filesystem::path path(std::string(exe_path, len));
  path = path.parent_path() / "../../cereal/messaging/bridge";
  return path.lexically_normal().string();
}

}  // namespace cabana
