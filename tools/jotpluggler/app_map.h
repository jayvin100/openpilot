#pragma once

#include <cstdint>
#include <memory>

class MapTileManager {
public:
  MapTileManager();
  ~MapTileManager();

  MapTileManager(const MapTileManager &) = delete;
  MapTileManager &operator=(const MapTileManager &) = delete;

  uint32_t textureFor(int z, int x, int y);
  void pump();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
