#include "sources/socketcan_source.h"

#ifdef __linux__

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cereal/messaging/messaging.h"

namespace cabana {

SocketCanSource::SocketCanSource(SocketCanSourceConfig config)
    : LiveSource("Live Streaming From Socket CAN " + config.device), config_(std::move(config)) {
}

SocketCanSource::~SocketCanSource() {
  stop();
}

bool SocketCanSource::available() {
  const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    return false;
  }
  close(fd);
  return true;
}

bool SocketCanSource::prepare(std::string *error) {
  sock_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock_fd_ < 0) {
    if (error) *error = "Failed to create SocketCAN socket.";
    return false;
  }

  int fd_enable = 1;
  setsockopt(sock_fd_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &fd_enable, sizeof(fd_enable));

  struct ifreq ifr = {};
  strncpy(ifr.ifr_name, config_.device.c_str(), IFNAMSIZ - 1);
  if (ioctl(sock_fd_, SIOCGIFINDEX, &ifr) < 0) {
    if (error) *error = "Failed to resolve SocketCAN interface.";
    cleanup();
    return false;
  }

  struct sockaddr_can addr = {};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(sock_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    if (error) *error = "Failed to bind SocketCAN interface.";
    cleanup();
    return false;
  }

  struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
  setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  return true;
}

void SocketCanSource::runThread() {
  struct canfd_frame frame = {};
  while (!shouldStop()) {
    const ssize_t nbytes = read(sock_fd_, &frame, sizeof(frame));
    if (nbytes <= 0) {
      continue;
    }

    MessageBuilder msg;
    auto evt = msg.initEvent();
    auto can = evt.initCan(1);
    can[0].setAddress(frame.can_id & CAN_EFF_MASK);
    can[0].setSrc(0);
    can[0].setDat(kj::arrayPtr(frame.data, frame.len));

    auto flat = capnp::messageToFlatArray(msg);
    handleEvent(flat.asPtr());
  }
}

void SocketCanSource::interrupt() {
  if (sock_fd_ >= 0) {
    shutdown(sock_fd_, SHUT_RDWR);
  }
}

void SocketCanSource::cleanup() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
    sock_fd_ = -1;
  }
}

}  // namespace cabana

#else

namespace cabana {

SocketCanSource::SocketCanSource(SocketCanSourceConfig config)
    : LiveSource("Live Streaming From Socket CAN " + config.device), config_(std::move(config)) {
}

SocketCanSource::~SocketCanSource() {
  stop();
}

bool SocketCanSource::available() {
  return false;
}

bool SocketCanSource::prepare(std::string *error) {
  if (error) *error = "SocketCAN is only supported on Linux.";
  return false;
}

void SocketCanSource::runThread() {}
void SocketCanSource::interrupt() {}
void SocketCanSource::cleanup() {}

}  // namespace cabana

#endif
