#include "sources/live_source.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cereal/gen/cpp/log.capnp.h"
#include "common/timing.h"

namespace {

constexpr int kEventBufferSize = 6 * 1024 * 1024;

}  // namespace

namespace cabana {

LiveSource::LiveSource(std::string name) : route_name_(std::move(name)) {
#ifndef __APPLE__
  op_prefix_ = std::make_unique<OpenpilotPrefix>();
#endif
  event_buffer_ = std::make_unique<MonotonicBuffer>(kEventBufferSize);
}

LiveSource::~LiveSource() {
  stop();
}

bool LiveSource::start(std::string *error) {
  if (worker_.joinable()) {
    return true;
  }
  if (!prepare(error)) {
    return false;
  }

  stop_requested_.store(false, std::memory_order_relaxed);
  paused_ = false;
  follow_live_ = true;
  speed_ = 1.0f;
  anchor_event_ts_ = current_event_ts_;
  anchor_wall_nanos_ = nanos_since_boot();
  worker_ = std::thread([this]() { runThread(); });
  return true;
}

void LiveSource::stop() {
  stop_requested_.store(true, std::memory_order_relaxed);
  interrupt();
  if (worker_.joinable()) {
    worker_.join();
  }
  cleanup();
}

void LiveSource::pause(bool paused) {
  if (paused_ == paused) {
    return;
  }
  paused_ = paused;
  resetPlaybackAnchor();
}

void LiveSource::seekTo(double sec) {
  if (begin_event_ts_ == 0) {
    return;
  }

  sec = std::max(0.0, sec);
  uint64_t target = begin_event_ts_ + static_cast<uint64_t>(sec * 1e9);
  if (latest_event_ts_ > 0) {
    target = std::min(target, latest_event_ts_);
  }
  current_event_ts_ = target;
  follow_live_ = latest_event_ts_ > 0 && current_event_ts_ >= latest_event_ts_;
  resetPlaybackAnchor();
  rebuildUiMessages(current_event_ts_);
}

void LiveSource::setSpeed(float speed) {
  speed_ = speed > 0.0f ? speed : 1.0f;
  resetPlaybackAnchor();
}

double LiveSource::currentSec() const {
  return toSeconds(current_event_ts_);
}

double LiveSource::maxSec() const {
  return begin_event_ts_ == 0 || latest_event_ts_ < begin_event_ts_ ? 0.0 : (latest_event_ts_ - begin_event_ts_) / 1e9;
}

double LiveSource::toSeconds(uint64_t mono_time) const {
  if (begin_event_ts_ == 0 || mono_time <= begin_event_ts_) {
    return 0.0;
  }
  return (mono_time - begin_event_ts_) / 1e9;
}

bool LiveSource::pollEvents() {
  bool changed = mergePendingEvents();
  if (updatePlaybackState()) {
    changed = true;
  }
  return changed;
}

bool LiveSource::mergeAllSegments() {
  return mergePendingEvents();
}

const std::vector<const CanEvent *> *LiveSource::events(const MessageId &id) const {
  auto it = events_.find(id);
  return it == events_.end() ? nullptr : &it->second;
}

void LiveSource::handleEvent(kj::ArrayPtr<const capnp::word> data) {
  capnp::FlatArrayMessageReader reader(data);
  auto event = reader.getRoot<cereal::Event>();
  if (event.which() != cereal::Event::Which::CAN) {
    return;
  }

  const uint64_t mono_time = event.getLogMonoTime();
  std::lock_guard lk(pending_lock_);
  for (const auto &c : event.getCan()) {
    pending_events_.push_back(allocEvent(mono_time, c));
  }
}

const CanEvent *LiveSource::allocEvent(uint64_t mono_time, const cereal::CanData::Reader &c) {
  auto dat = c.getDat();
  CanEvent *e = reinterpret_cast<CanEvent *>(event_buffer_->allocate(sizeof(CanEvent) + dat.size()));
  e->src = c.getSrc();
  e->address = c.getAddress();
  e->mono_time = mono_time;
  e->size = dat.size();
  memcpy(e->dat, dat.begin(), dat.size());
  return e;
}

bool LiveSource::mergePendingEvents() {
  std::vector<const CanEvent *> pending;
  {
    std::lock_guard lk(pending_lock_);
    if (pending_events_.empty()) {
      return false;
    }
    pending.swap(pending_events_);
  }

  for (const CanEvent *event : pending) {
    if (!event) {
      continue;
    }

    if (begin_event_ts_ == 0) {
      begin_event_ts_ = event->mono_time;
      current_event_ts_ = event->mono_time;
      anchor_event_ts_ = event->mono_time;
      anchor_wall_nanos_ = nanos_since_boot();
    }
    latest_event_ts_ = std::max(latest_event_ts_, event->mono_time);

    all_events_.insert(std::upper_bound(all_events_.begin(), all_events_.end(), event->mono_time, CompareCanEvent()), event);

    MessageId id = {.source = event->src, .address = event->address};
    auto &message_events = events_[id];
    message_events.insert(std::upper_bound(message_events.begin(), message_events.end(), event->mono_time, CompareCanEvent()),
                          event);
  }

  if (follow_live_ && !paused_) {
    current_event_ts_ = latest_event_ts_;
    resetPlaybackAnchor();
    rebuildUiMessages(current_event_ts_);
  }

  return true;
}

bool LiveSource::updatePlaybackState() {
  if (begin_event_ts_ == 0) {
    return false;
  }

  uint64_t target = current_event_ts_;
  if (!paused_) {
    if (follow_live_ && speed_ == 1.0f) {
      target = latest_event_ts_;
    } else {
      uint64_t elapsed = nanos_since_boot() - anchor_wall_nanos_;
      target = anchor_event_ts_ + static_cast<uint64_t>(elapsed * speed_);
      if (latest_event_ts_ > 0) {
        target = std::min(target, latest_event_ts_);
      }
    }
  }

  if (target == current_event_ts_ && !ui_msgs_.empty()) {
    return false;
  }

  current_event_ts_ = target;
  rebuildUiMessages(current_event_ts_);
  return true;
}

void LiveSource::resetPlaybackAnchor() {
  anchor_event_ts_ = current_event_ts_;
  anchor_wall_nanos_ = nanos_since_boot();
}

void LiveSource::rebuildUiMessages(uint64_t target_mono) {
  ui_msgs_.clear();
  ui_msgs_.reserve(events_.size());

  for (const auto &[id, message_events] : events_) {
    if (message_events.empty()) {
      continue;
    }

    auto last = std::upper_bound(message_events.begin(), message_events.end(), target_mono, CompareCanEvent());
    if (last == message_events.begin()) {
      continue;
    }

    const CanEvent *event = *std::prev(last);
    auto &msg = ui_msgs_[id];
    msg.ts = toSeconds(event->mono_time);
    msg.count = std::distance(message_events.begin(), last);
    msg.freq = computeFrequency(message_events, target_mono);
    msg.dat.assign(event->dat, event->dat + event->size);
    msg.last_freq_update_ts = msg.ts;
  }
}

double LiveSource::computeFrequency(const std::vector<const CanEvent *> &events, uint64_t target_mono) const {
  if (events.size() <= 1) {
    return 0.0;
  }

  const uint64_t window_start = target_mono > 60ULL * 1000000000ULL ? target_mono - 60ULL * 1000000000ULL : 0;
  auto first = std::lower_bound(events.begin(), events.end(), window_start, CompareCanEvent());
  auto last = std::upper_bound(first, events.end(), target_mono, CompareCanEvent());
  const size_t count = std::distance(first, last);
  if (count <= 1) {
    return 0.0;
  }

  const uint64_t start_mono = (*first)->mono_time;
  const uint64_t end_mono = (*std::prev(last))->mono_time;
  const double duration = (end_mono - start_mono) / 1e9;
  return duration > 0.0 ? (count - 1) / duration : 0.0;
}

}  // namespace cabana
