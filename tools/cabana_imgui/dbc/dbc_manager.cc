#include "dbc/dbc_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "third_party/json11/json11.hpp"

namespace cabana {
namespace dbc {

namespace fs = std::filesystem;

static fs::path repo_root() {
  std::array<char, 4096> buf = {};
  ssize_t count = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (count <= 0) return {};
  return fs::path(std::string(buf.data(), (size_t)count))
      .parent_path().parent_path().parent_path();
}

void DbcManager::bumpRevision() {
  ++revision_;
}

DbcManager &dbc_manager() {
  static DbcManager mgr;
  return mgr;
}

bool DbcManager::loadFromFile(const std::string &path) {
  auto f = std::make_unique<DbcFile>();
  if (!f->load(path)) {
    fprintf(stderr, "Failed to load DBC: %s\n", path.c_str());
    return false;
  }
  dbc_ = std::move(f);
  loaded_name_ = path;
  bumpRevision();
  fprintf(stderr, "Loaded DBC: %s (%d messages, %d signals)\n",
          path.c_str(), msgCount(), signalCount());
  return true;
}

bool DbcManager::save() {
  if (!dbc_ || !dbc_->save()) {
    return false;
  }
  loaded_name_ = dbc_->filename();
  return true;
}

bool DbcManager::saveAs(const std::string &path) {
  if (!dbc_ || !dbc_->saveAs(path)) {
    return false;
  }
  loaded_name_ = dbc_->filename();
  bumpRevision();
  return true;
}

bool DbcManager::loadFromOpendbc(const std::string &name) {
  fs::path root = repo_root();
  fs::path dbc_path = root / "opendbc" / "dbc" / (name + ".dbc");
  if (!fs::exists(dbc_path)) {
    fprintf(stderr, "OpenDBC file not found: %s\n", dbc_path.c_str());
    return false;
  }
  return loadFromFile(dbc_path.string());
}

bool DbcManager::loadFromFingerprint(const std::string &fingerprint) {
  if (fingerprint.empty()) return false;

  fs::path root = repo_root();
  fs::path json_path = root / "tools" / "cabana" / "dbc" / "car_fingerprint_to_dbc.json";
  if (!fs::exists(json_path)) {
    fprintf(stderr, "Fingerprint mapping not found: %s\n", json_path.c_str());
    return false;
  }

  std::ifstream f(json_path);
  std::string json_str((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
  std::string err;
  auto json = json11::Json::parse(json_str, err);
  if (!err.empty()) {
    fprintf(stderr, "Failed to parse fingerprint JSON: %s\n", err.c_str());
    return false;
  }

  auto &obj = json.object_items();
  auto it = obj.find(fingerprint);
  if (it == obj.end()) {
    fprintf(stderr, "Fingerprint not found in mapping: %s\n", fingerprint.c_str());
    return false;
  }

  std::string dbc_name = it->second.string_value();
  fprintf(stderr, "Fingerprint %s -> DBC %s\n", fingerprint.c_str(), dbc_name.c_str());
  return loadFromOpendbc(dbc_name);
}

const char *DbcManager::msgName(uint32_t address) const {
  if (!dbc_) return nullptr;
  auto m = dbc_->msg(address);
  return m ? m->name.c_str() : nullptr;
}

const Message *DbcManager::msg(uint32_t address) const {
  return dbc_ ? dbc_->msg(address) : nullptr;
}

bool DbcManager::updateMessage(uint32_t address, const std::string &name, uint32_t size,
                               const std::string &transmitter, const std::string &comment) {
  if (!dbc_ || name.empty()) {
    return false;
  }
  dbc_->updateMessage(address, name, size, transmitter, comment);
  bumpRevision();
  return true;
}

bool DbcManager::removeMessage(uint32_t address) {
  if (!dbc_ || !dbc_->removeMessage(address)) {
    return false;
  }
  bumpRevision();
  return true;
}

bool DbcManager::addSignal(uint32_t address, const Signal &signal) {
  if (!dbc_ || !dbc_->addSignal(address, signal)) {
    return false;
  }
  bumpRevision();
  return true;
}

bool DbcManager::updateSignal(uint32_t address, const std::string &original_name, const Signal &signal) {
  if (!dbc_ || !dbc_->updateSignal(address, original_name, signal)) {
    return false;
  }
  bumpRevision();
  return true;
}

bool DbcManager::removeSignal(uint32_t address, const std::string &name) {
  if (!dbc_ || !dbc_->removeSignal(address, name)) {
    return false;
  }
  bumpRevision();
  return true;
}

bool DbcManager::messageNameExists(const std::string &name, uint32_t ignore_address) const {
  if (!dbc_ || name.empty()) {
    return false;
  }
  for (const auto &[address, msg] : dbc_->messages()) {
    if (address != ignore_address && msg.name == name) {
      return true;
    }
  }
  return false;
}

std::string DbcManager::nextMessageName(uint32_t address) const {
  char buf[64];
  snprintf(buf, sizeof(buf), "NEW_MSG_%X", address);
  std::string candidate = buf;
  if (!messageNameExists(candidate)) {
    return candidate;
  }
  for (int suffix = 1; suffix < 10000; ++suffix) {
    snprintf(buf, sizeof(buf), "NEW_MSG_%X_%d", address, suffix);
    candidate = buf;
    if (!messageNameExists(candidate)) {
      return candidate;
    }
  }
  return "NEW_MSG";
}

std::string DbcManager::nextSignalName(uint32_t address) const {
  auto *message = msg(address);
  if (!message) {
    return "NEW_SIGNAL";
  }
  auto signal_exists = [&](const std::string &candidate) {
    return std::any_of(message->signals.begin(), message->signals.end(), [&](const auto &signal) {
      return signal.name == candidate;
    });
  };
  if (!signal_exists("NEW_SIGNAL")) {
    return "NEW_SIGNAL";
  }
  char buf[64];
  for (int suffix = 1; suffix < 10000; ++suffix) {
    snprintf(buf, sizeof(buf), "NEW_SIGNAL_%d", suffix);
    if (!signal_exists(buf)) {
      return buf;
    }
  }
  return "NEW_SIGNAL";
}

}  // namespace dbc
}  // namespace cabana
