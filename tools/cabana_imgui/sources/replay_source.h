#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/prefix.h"
#include "core/types.h"
#include "sources/source.h"
#include "tools/replay/replay.h"
#include "tools/replay/util.h"

namespace cabana {

class ReplaySource : public Source {
public:
  ReplaySource();
  ~ReplaySource() override;

  bool load(const std::string &route, const std::string &data_dir,
            uint32_t flags, bool auto_source);
  bool start(std::string *error = nullptr) override;
  void stop() override;
  void pause(bool p) override;
  void seekTo(double sec) override;
  void setSpeed(float s) override;

  bool isPaused() const override;
  bool liveStreaming() const override { return false; }
  double currentSec() const override;
  double minSec() const override;
  double maxSec() const override;
  float speed() const override;
  const std::string &routeName() const override;
  const std::string &carFingerprint() const override;
  uint64_t routeStartNanos() const override;
  double toSeconds(uint64_t mono_time) const override;

  // Called each frame on the main thread. Returns true if UI-visible state changed.
  bool pollEvents() override;
  bool mergeAllSegments() override;

  // Latest message state — call only from main thread after pollEvents()
  const std::unordered_map<MessageId, MsgLiveData> &messages() const override { return ui_msgs_; }

  // Merged events map (main thread only, updated by mergeSegments)
  const MessageEventsMap &eventsMap() const override { return events_; }
  const std::vector<const CanEvent *> &allEvents() const override { return all_events_; }
  const std::vector<const CanEvent *> *events(const MessageId &id) const override;

  Replay *replay() { return replay_.get(); }

private:
  bool mergeSegments(size_t max_segments);
  bool eventFilter(const Event *event);
  const CanEvent *allocEvent(uint64_t mono_time, const cereal::CanData::Reader &c);

  std::unique_ptr<Replay> replay_;
  std::unique_ptr<OpenpilotPrefix> op_prefix_;

  // Merged segment data (main thread only)
  std::set<int> processed_segments_;
  MessageEventsMap events_;
  std::vector<const CanEvent *> all_events_;
  std::unique_ptr<MonotonicBuffer> event_buffer_;

  // Stream thread writes to live_msgs_ under mutex.
  // Main thread copies to ui_msgs_ at ~10Hz in pollEvents().
  std::mutex msg_mutex_;
  std::unordered_map<MessageId, MsgLiveData> live_msgs_;
  std::unordered_map<MessageId, MsgLiveData> ui_msgs_;
  std::atomic<bool> msgs_dirty_{false};

  // Atomic flag for segment merge notification
  std::atomic<bool> segments_merged_{false};

  std::string route_name_;
};

}  // namespace cabana
