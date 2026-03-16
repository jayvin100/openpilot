#include "tools/imgui_cabana/streams/replaystream.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <capnp/serialize.h>

#include "cereal/gen/cpp/log.capnp.h"
#include "tools/replay/logreader.h"
#include "tools/replay/route.h"

namespace imgui_cabana {

namespace {

struct MessageKey {
  int source = 0;
  uint32_t address = 0;

  bool operator==(const MessageKey &other) const {
    return source == other.source && address == other.address;
  }
};

struct MessageKeyHash {
  std::size_t operator()(const MessageKey &key) const noexcept {
    return (std::hash<int>{}(key.source) << 1) ^ std::hash<int>{}(key.address);
  }
};

std::string formatByteValue(uint8_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%02X (%u)", value, static_cast<unsigned>(value));
  return buffer;
}

std::string defaultMessageName(uint32_t address) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%X", address);
  return buffer;
}

void ensureSignalLayout(MessageData &message, std::size_t byte_count) {
  if (message.signals.size() >= byte_count) return;

  for (std::size_t i = message.signals.size(); i < byte_count; ++i) {
    SignalData signal;
    signal.name = "byte" + std::to_string(i);
    signal.value = "0x00 (0)";
    signal.byte_index = static_cast<int>(i);
    message.signals.push_back(std::move(signal));
  }
}

void applySample(MessageData &message, std::size_t sample_index) {
  if (message.samples.empty()) return;

  message.active_sample_index = std::min(sample_index, message.samples.size() - 1);
  const auto &sample = message.samples[message.active_sample_index];
  message.bytes = sample.bytes;
  ensureSignalLayout(message, message.bytes.size());
  for (std::size_t i = 0; i < message.signals.size(); ++i) {
    if (i < message.bytes.size()) {
      message.signals[i].value = formatByteValue(message.bytes[i]);
    } else {
      message.signals[i].value = "n/a";
    }
  }
}

std::string routeErrorMessage(RouteLoadError err) {
  switch (err) {
    case RouteLoadError::Unauthorized: return "route access denied";
    case RouteLoadError::AccessDenied: return "route access denied";
    case RouteLoadError::NetworkError: return "network error while loading route";
    case RouteLoadError::FileNotFound: return "route not found";
    case RouteLoadError::UnknownError: return "unknown route load error";
    case RouteLoadError::None:
    default: return "failed to load route";
  }
}

}  // namespace

ReplayLoadResult loadReplayRoute(const std::string &route, const std::optional<std::string> &data_dir) {
  ReplayLoadResult result;
  Route replay_route(route, data_dir.value_or(""), false);
  if (!replay_route.load()) {
    result.error = routeErrorMessage(replay_route.lastError());
    return result;
  }

  result.route_name = replay_route.name();

  std::unordered_map<MessageKey, std::size_t, MessageKeyHash> message_index;
  uint64_t first_can_mono = 0;
  double last_sec = 0.0;

  for (const auto &[segment_num, segment] : replay_route.segments()) {
    (void)segment_num;
    const std::string log_path = !segment.rlog.empty() ? segment.rlog : segment.qlog;
    if (log_path.empty()) continue;

    LogReader reader;
    std::atomic<bool> abort = false;
    if (!reader.load(log_path, &abort, true)) continue;

    for (const auto &event : reader.events) {
      capnp::FlatArrayMessageReader msg_reader(event.data);
      auto log_event = msg_reader.getRoot<cereal::Event>();
      if (log_event.which() == cereal::Event::CAR_PARAMS && result.fingerprint.empty()) {
        result.fingerprint = log_event.getCarParams().getCarFingerprint().cStr();
        continue;
      }
      if (log_event.which() != cereal::Event::CAN) continue;

      if (first_can_mono == 0) first_can_mono = event.mono_time;
      const double sec = static_cast<double>(event.mono_time - first_can_mono) / 1e9;
      last_sec = std::max(last_sec, sec);

      for (const auto &can : log_event.getCan()) {
        const MessageKey key = {
            .source = can.getSrc(),
            .address = can.getAddress(),
        };
        auto [it, inserted] = message_index.emplace(key, result.messages.size());
        if (inserted) {
          MessageData message;
          message.address = key.address;
          message.source = key.source;
          message.name = defaultMessageName(key.address);
          message.node = "Bus " + std::to_string(key.source);
          result.messages.push_back(std::move(message));
        }

        auto &message = result.messages[it->second];
        const auto dat = can.getDat();
        std::vector<uint8_t> bytes(dat.begin(), dat.end());
        ensureSignalLayout(message, bytes.size());
        message.samples.push_back({sec, bytes});
        ++message.count;
      }
    }
  }

  if (result.messages.empty()) {
    result.error = "route contains no CAN traffic";
    return result;
  }

  std::sort(result.messages.begin(), result.messages.end(), [](const auto &a, const auto &b) {
    return std::tie(a.source, a.address) < std::tie(b.source, b.address);
  });

  for (auto &message : result.messages) {
    if (message.samples.empty()) continue;
    const double first_sec = message.samples.front().sec;
    const double final_sec = message.samples.back().sec;
    if (message.samples.size() > 1 && final_sec > first_sec) {
      message.freq = static_cast<double>(message.samples.size() - 1) / (final_sec - first_sec);
    }
    applySample(message, message.samples.size() - 1);
  }

  result.success = true;
  result.min_sec = 0.0;
  result.max_sec = std::max(0.0, last_sec);
  return result;
}

void syncReplayMessages(std::vector<MessageData> &messages, double current_sec) {
  for (auto &message : messages) {
    if (message.samples.empty()) continue;
    auto it = std::upper_bound(message.samples.begin(), message.samples.end(), current_sec,
                               [](double sec, const MessageSample &sample) { return sec < sample.sec; });
    if (it == message.samples.begin()) {
      applySample(message, 0);
      continue;
    }
    applySample(message, static_cast<std::size_t>(std::distance(message.samples.begin(), std::prev(it))));
  }
}

}  // namespace imgui_cabana
