#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct MessageId {
  uint8_t source = 0;
  uint32_t address = 0;

  std::string toString() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%u:%X", source, address);
    return buf;
  }

  bool operator==(const MessageId &o) const { return source == o.source && address == o.address; }
  bool operator!=(const MessageId &o) const { return !(*this == o); }
  bool operator<(const MessageId &o) const { return std::tie(source, address) < std::tie(o.source, o.address); }
};

template <>
struct std::hash<MessageId> {
  std::size_t operator()(const MessageId &k) const noexcept {
    return std::hash<uint8_t>{}(k.source) ^ (std::hash<uint32_t>{}(k.address) << 1);
  }
};

struct CanEvent {
  uint8_t src;
  uint32_t address;
  uint64_t mono_time;
  uint8_t size;
  uint8_t dat[];
};

struct CompareCanEvent {
  constexpr bool operator()(const CanEvent *e, uint64_t ts) const { return e->mono_time < ts; }
  constexpr bool operator()(uint64_t ts, const CanEvent *e) const { return ts < e->mono_time; }
};

typedef std::set<int> SourceSet;
typedef std::unordered_map<MessageId, std::vector<const CanEvent *>> MessageEventsMap;
