#pragma once

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/app_state.h"
#include "core/types.h"

namespace cabana {

struct MsgLiveData {
  double ts = 0.0;
  uint32_t count = 0;
  double freq = 0.0;
  std::vector<uint8_t> dat;
  double last_freq_update_ts = 0.0;
};

class Source {
public:
  virtual ~Source() = default;

  virtual bool start(std::string *error = nullptr) = 0;
  virtual void stop() = 0;

  virtual void pause(bool paused) = 0;
  virtual void seekTo(double sec) = 0;
  virtual void setSpeed(float speed) = 0;

  virtual bool isPaused() const = 0;
  virtual bool liveStreaming() const = 0;
  virtual double currentSec() const = 0;
  virtual double minSec() const = 0;
  virtual double maxSec() const = 0;
  virtual float speed() const = 0;
  virtual const std::string &routeName() const = 0;
  virtual const std::string &carFingerprint() const = 0;
  virtual uint64_t routeStartNanos() const = 0;
  virtual double toSeconds(uint64_t mono_time) const = 0;

  virtual bool pollEvents() = 0;
  virtual bool mergeAllSegments() = 0;

  virtual const std::unordered_map<MessageId, MsgLiveData> &messages() const = 0;
  virtual const MessageEventsMap &eventsMap() const = 0;
  virtual const std::vector<const CanEvent *> &allEvents() const = 0;
  virtual const std::vector<const CanEvent *> *events(const MessageId &id) const = 0;

  bool isMessageActive(const MessageId &id) const {
    if (id.source == std::numeric_limits<uint8_t>::max()) {
      return false;
    }

    const auto &msgs = messages();
    auto it = msgs.find(id);
    if (it == msgs.end()) {
      return false;
    }

    const auto &msg = it->second;
    const double delta = currentSec() - msg.ts;
    if (msg.freq < std::numeric_limits<double>::epsilon()) {
      return delta < 1.5;
    }

    return delta < (5.0 / msg.freq) + (1.0 / app_state().fps);
  }
};

}  // namespace cabana
