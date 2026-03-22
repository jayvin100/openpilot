#include "sources/panda_source.h"

#include <chrono>
#include <thread>

#include "cereal/messaging/messaging.h"

namespace cabana {

PandaSource::PandaSource(PandaSourceConfig config)
    : LiveSource(config.serial.empty() ? "Panda" : ("Panda: " + config.serial)), config_(std::move(config)) {
}

PandaSource::~PandaSource() {
  stop();
}

bool PandaSource::prepare(std::string *error) {
  if (!connectPanda()) {
    if (error) *error = "Failed to connect to panda.";
    return false;
  }
  return true;
}

void PandaSource::runThread() {
  std::vector<can_frame> raw_can_data;

  while (!shouldStop()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    if (!panda_ || !panda_->connected()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      connectPanda();
      continue;
    }

    raw_can_data.clear();
    if (!panda_->can_receive(raw_can_data)) {
      continue;
    }

    MessageBuilder msg;
    auto evt = msg.initEvent();
    auto can = evt.initCan(raw_can_data.size());
    for (size_t i = 0; i < raw_can_data.size(); ++i) {
      can[i].setAddress(raw_can_data[i].address);
      can[i].setDat(kj::arrayPtr(reinterpret_cast<const uint8_t *>(raw_can_data[i].dat.data()), raw_can_data[i].dat.size()));
      can[i].setSrc(raw_can_data[i].src);
    }

    auto flat = capnp::messageToFlatArray(msg);
    handleEvent(flat.asPtr());
    panda_->send_heartbeat(false);
  }
}

void PandaSource::cleanup() {
  panda_.reset();
}

bool PandaSource::connectPanda() {
  try {
    panda_ = std::make_unique<Panda>(config_.serial);
    if (config_.bus_config.empty()) {
      config_.bus_config.resize(3);
    }

    panda_->set_safety_model(cereal::CarParams::SafetyModel::NO_OUTPUT);
    for (int bus = 0; bus < (int)config_.bus_config.size(); ++bus) {
      panda_->set_can_speed_kbps(bus, config_.bus_config[bus].can_speed_kbps);
      if (panda_->hw_type == cereal::PandaState::PandaType::RED_PANDA ||
          panda_->hw_type == cereal::PandaState::PandaType::RED_PANDA_V2) {
        panda_->set_data_speed_kbps(bus, config_.bus_config[bus].can_fd ? config_.bus_config[bus].data_speed_kbps : 10);
      }
    }

    const std::string serial = panda_->hw_serial();
    if (!serial.empty()) {
      config_.serial = serial;
      setRouteName("Panda: " + serial);
    }
    return true;
  } catch (const std::exception &) {
    panda_.reset();
    return false;
  }
}

}  // namespace cabana
