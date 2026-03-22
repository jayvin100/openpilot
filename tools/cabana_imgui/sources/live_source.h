#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <capnp/serialize.h>

#include "cereal/gen/cpp/log.capnp.h"
#include "common/prefix.h"
#include "sources/source.h"
#include "tools/replay/util.h"

namespace cabana {

class LiveSource : public Source {
public:
  explicit LiveSource(std::string name);
  ~LiveSource() override;

  bool start(std::string *error = nullptr) override;
  void stop() override;

  void pause(bool paused) override;
  void seekTo(double sec) override;
  void setSpeed(float speed) override;

  bool isPaused() const override { return paused_; }
  bool liveStreaming() const override { return true; }
  double currentSec() const override;
  double minSec() const override { return 0.0; }
  double maxSec() const override;
  float speed() const override { return speed_; }
  const std::string &routeName() const override { return route_name_; }
  const std::string &carFingerprint() const override { return car_fingerprint_; }
  uint64_t routeStartNanos() const override { return begin_event_ts_; }
  double toSeconds(uint64_t mono_time) const override;

  bool pollEvents() override;
  bool mergeAllSegments() override;

  const std::unordered_map<MessageId, MsgLiveData> &messages() const override { return ui_msgs_; }
  const MessageEventsMap &eventsMap() const override { return events_; }
  const std::vector<const CanEvent *> &allEvents() const override { return all_events_; }
  const std::vector<const CanEvent *> *events(const MessageId &id) const override;

protected:
  virtual bool prepare(std::string *error) = 0;
  virtual void runThread() = 0;
  virtual void interrupt() {}
  virtual void cleanup() {}

  bool shouldStop() const { return stop_requested_.load(std::memory_order_relaxed); }
  void handleEvent(kj::ArrayPtr<const capnp::word> data);
  void setRouteName(const std::string &name) { route_name_ = name; }
  void setCarFingerprint(const std::string &fingerprint) { car_fingerprint_ = fingerprint; }
  const CanEvent *allocEvent(uint64_t mono_time, const cereal::CanData::Reader &c);

private:
  bool mergePendingEvents();
  bool updatePlaybackState();
  void resetPlaybackAnchor();
  void rebuildUiMessages(uint64_t target_mono);
  double computeFrequency(const std::vector<const CanEvent *> &events, uint64_t target_mono) const;

  std::string route_name_;
  std::string car_fingerprint_;

  std::unique_ptr<OpenpilotPrefix> op_prefix_;
  std::unique_ptr<MonotonicBuffer> event_buffer_;

  std::atomic<bool> stop_requested_{false};
  std::thread worker_;

  std::mutex pending_lock_;
  std::vector<const CanEvent *> pending_events_;

  MessageEventsMap events_;
  std::vector<const CanEvent *> all_events_;
  std::unordered_map<MessageId, MsgLiveData> ui_msgs_;

  uint64_t begin_event_ts_ = 0;
  uint64_t latest_event_ts_ = 0;
  uint64_t current_event_ts_ = 0;
  uint64_t anchor_event_ts_ = 0;
  uint64_t anchor_wall_nanos_ = 0;
  float speed_ = 1.0f;
  bool paused_ = false;
  bool follow_live_ = true;
};

}  // namespace cabana
