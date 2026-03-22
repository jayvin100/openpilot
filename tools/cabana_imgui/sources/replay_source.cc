#include "sources/replay_source.h"

#include <cstdlib>
#include <algorithm>

#include "cereal/gen/cpp/log.capnp.h"
#include "common/timing.h"
#include "core/app_state.h"

static const int EVENT_BUFFER_SIZE = 6 * 1024 * 1024;

namespace cabana {

ReplaySource::ReplaySource() {
  unsetenv("ZMQ");
  setenv("COMMA_CACHE", "/tmp/comma_download_cache", 1);

#ifndef __APPLE__
  op_prefix_ = std::make_unique<OpenpilotPrefix>();
#endif

  event_buffer_ = std::make_unique<MonotonicBuffer>(EVENT_BUFFER_SIZE);
}

ReplaySource::~ReplaySource() { stop(); }

bool ReplaySource::load(const std::string &route, const std::string &data_dir,
                        uint32_t flags, bool auto_source) {
  route_name_ = route;

  std::vector<std::string> allow = {"can", "carParams"};
  if ((flags & REPLAY_FLAG_NO_VIPC) == 0) {
    allow.push_back("roadEncodeIdx");
    allow.push_back("thumbnail");
    if (flags & REPLAY_FLAG_DCAM) {
      allow.push_back("driverEncodeIdx");
    }
    if (flags & REPLAY_FLAG_ECAM) {
      allow.push_back("wideRoadEncodeIdx");
    }
  }

  replay_.reset(new Replay(route,
                           allow,
                           {}, nullptr, flags, data_dir, auto_source));
  replay_->setSegmentCacheLimit(cabana::app_state().cached_minutes);

  replay_->installEventFilter([this](const Event *e) { return eventFilter(e); });
  replay_->onSegmentsMerged = [this]() { segments_merged_.store(true); };

  bool ok = replay_->load();
  if (ok) {
    fprintf(stderr, "loaded route %s with fingerprint: %s\n",
            route.c_str(), replay_->carFingerprint().c_str());
  } else {
    fprintf(stderr, "failed to load route: %s\n", route.c_str());
  }
  return ok;
}

bool ReplaySource::start(std::string *error) {
  (void)error;
  if (replay_) replay_->start();
  return replay_ != nullptr;
}

void ReplaySource::stop() {
  if (replay_) {
    replay_->waitForFinished();
  }
}

void ReplaySource::pause(bool p) {
  if (replay_) replay_->pause(p);
}

void ReplaySource::seekTo(double sec) {
  if (replay_) replay_->seekTo(sec, false);
}

void ReplaySource::setSpeed(float s) {
  if (replay_) replay_->setSpeed(s);
}

bool ReplaySource::isPaused() const { return replay_ ? replay_->isPaused() : false; }
double ReplaySource::currentSec() const { return replay_ ? replay_->currentSeconds() : 0; }
double ReplaySource::minSec() const { return replay_ ? replay_->minSeconds() : 0; }
double ReplaySource::maxSec() const { return replay_ ? replay_->maxSeconds() : 0; }
float ReplaySource::speed() const { return replay_ ? replay_->getSpeed() : 1.0f; }
const std::string &ReplaySource::routeName() const { return route_name_; }
double ReplaySource::toSeconds(uint64_t mono_time) const { return replay_ ? replay_->toSeconds(mono_time) : 0.0; }
const std::string &ReplaySource::carFingerprint() const {
  static std::string empty;
  return replay_ ? replay_->carFingerprint() : empty;
}
uint64_t ReplaySource::routeStartNanos() const { return replay_ ? replay_->routeStartNanos() : 0; }

bool ReplaySource::eventFilter(const Event *event) {
  if (event->which == cereal::Event::Which::CAN) {
    double sec = replay_->toSeconds(event->mono_time);

    capnp::FlatArrayMessageReader reader(event->data);
    auto e = reader.getRoot<cereal::Event>();
    auto can_msgs = e.getCan();

    {
      std::lock_guard lk(msg_mutex_);
      for (const auto &c : can_msgs) {
        MessageId id = {.source = c.getSrc(), .address = c.getAddress()};
        auto &m = live_msgs_[id];
        const auto dat = c.getDat();
        m.dat.assign(dat.begin(), dat.end());
        m.ts = sec;
        m.count++;
        if (m.count <= 1) {
          m.last_freq_update_ts = sec;
        } else {
          double dt = sec - m.last_freq_update_ts;
          if (dt > 0.1) {
            m.freq = m.count / dt;
          }
        }
      }
      msgs_dirty_.store(true, std::memory_order_relaxed);
    }
  }
  return true;
}

bool ReplaySource::pollEvents() {
  bool changed = false;

  if (segments_merged_.load(std::memory_order_relaxed) && mergeSegments(1)) {
    changed = true;
  }

  // Copy dirty messages to UI at throttled rate
  if (msgs_dirty_.exchange(false, std::memory_order_relaxed)) {
    std::lock_guard lk(msg_mutex_);
    ui_msgs_ = live_msgs_;
    changed = true;
  }

  return changed;
}

bool ReplaySource::mergeAllSegments() {
  bool changed = false;
  while (mergeSegments(SIZE_MAX)) {
    changed = true;
  }

  if (msgs_dirty_.exchange(false, std::memory_order_relaxed)) {
    std::lock_guard lk(msg_mutex_);
    ui_msgs_ = live_msgs_;
    changed = true;
  }
  return changed;
}

bool ReplaySource::mergeSegments(size_t max_segments) {
  size_t merged_segments = 0;
  auto event_data = replay_->getEventData();
  for (const auto &[n, seg] : event_data->segments) {
    if (processed_segments_.count(n)) continue;
    processed_segments_.insert(n);
    merged_segments++;

    std::vector<const CanEvent *> new_events;
    new_events.reserve(seg->log->events.size());
    for (const Event &e : seg->log->events) {
      if (e.which == cereal::Event::Which::CAN) {
        capnp::FlatArrayMessageReader reader(e.data);
        auto event = reader.getRoot<cereal::Event>();
        for (const auto &c : event.getCan()) {
          new_events.push_back(allocEvent(e.mono_time, c));
        }
      }
    }

    // Merge into per-message event lists
    for (const CanEvent *e : new_events) {
      MessageId id = {.source = e->src, .address = e->address};
      events_[id].push_back(e);
    }
    all_events_.insert(all_events_.end(), new_events.begin(), new_events.end());

    if (merged_segments >= max_segments) {
      break;
    }
  }

  // Sort all events by time
  std::sort(all_events_.begin(), all_events_.end(),
            [](const CanEvent *a, const CanEvent *b) { return a->mono_time < b->mono_time; });
  for (auto &[_, evts] : events_) {
    std::sort(evts.begin(), evts.end(),
              [](const CanEvent *a, const CanEvent *b) { return a->mono_time < b->mono_time; });
  }

  const bool has_more = processed_segments_.size() < event_data->segments.size();
  segments_merged_.store(has_more, std::memory_order_relaxed);
  return merged_segments > 0;
}

const std::vector<const CanEvent *> *ReplaySource::events(const MessageId &id) const {
  auto it = events_.find(id);
  return it == events_.end() ? nullptr : &it->second;
}

const CanEvent *ReplaySource::allocEvent(uint64_t mono_time, const cereal::CanData::Reader &c) {
  auto dat = c.getDat();
  CanEvent *e = (CanEvent *)event_buffer_->allocate(sizeof(CanEvent) + dat.size());
  e->src = c.getSrc();
  e->address = c.getAddress();
  e->mono_time = mono_time;
  e->size = dat.size();
  memcpy(e->dat, dat.begin(), dat.size());
  return e;
}

}  // namespace cabana
