#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "sources/live_source.h"
#include "tools/cabana/panda.h"

namespace cabana {

struct BusConfig {
  int can_speed_kbps = 500;
  int data_speed_kbps = 2000;
  bool can_fd = false;
};

struct PandaSourceConfig {
  std::string serial;
  std::vector<BusConfig> bus_config;
};

inline constexpr std::array<uint32_t, 8> kCanSpeeds = {10U, 20U, 50U, 100U, 125U, 250U, 500U, 1000U};
inline constexpr std::array<uint32_t, 10> kCanDataSpeeds = {10U, 20U, 50U, 100U, 125U, 250U, 500U, 1000U, 2000U, 5000U};

class PandaSource : public LiveSource {
public:
  explicit PandaSource(PandaSourceConfig config = {});
  ~PandaSource() override;

protected:
  bool prepare(std::string *error) override;
  void runThread() override;
  void cleanup() override;

private:
  bool connectPanda();

  PandaSourceConfig config_;
  std::unique_ptr<Panda> panda_;
};

}  // namespace cabana
