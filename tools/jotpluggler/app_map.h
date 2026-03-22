#pragma once

#include <cstdint>
#include <memory>

struct GpsTrace;
struct RouteBasemap;

class MapDataManager {
public:
  MapDataManager();
  ~MapDataManager();

  MapDataManager(const MapDataManager &) = delete;
  MapDataManager &operator=(const MapDataManager &) = delete;

  void pump();
  void ensureTrace(const GpsTrace &trace);
  bool loading() const;
  const RouteBasemap *current() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
