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
#include "tools/replay/replay.h"
#include "tools/replay/util.h"

namespace cabana {

// Per-message live state updated from the stream thread
struct MsgLiveData {
  double ts = 0;
  uint32_t count = 0;
  double freq = 0;
  std::vector<uint8_t> dat;
  double last_freq_update_ts = 0;
};

class ReplaySource {
public:
  ReplaySource();
  ~ReplaySource();

  bool load(const std::string &route, const std::string &data_dir,
            uint32_t flags, bool auto_source);
  void start();
  void pause(bool p);
  void seekTo(double sec);
  void setSpeed(float s);

  bool isPaused() const;
  double currentSec() const;
  double minSec() const;
  double maxSec() const;
  float speed() const;
  const std::string &routeName() const;
  const std::string &carFingerprint() const;
  uint64_t routeStartNanos() const;
  double toSeconds(uint64_t mono_time) const;

  // Called each frame on the main thread. Returns true if UI-visible state changed.
  bool pollEvents();
  bool mergeAllSegments();

  // Latest message state — call only from main thread after pollEvents()
  const std::unordered_map<MessageId, MsgLiveData> &messages() const { return ui_msgs_; }

  // Merged events map (main thread only, updated by mergeSegments)
  const MessageEventsMap &eventsMap() const { return events_; }
  const std::vector<const CanEvent *> &allEvents() const { return all_events_; }
  const std::vector<const CanEvent *> *events(const MessageId &id) const;

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
