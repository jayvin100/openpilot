#include "dbc/dbc_manager.h"

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
  fprintf(stderr, "Loaded DBC: %s (%d messages, %d signals)\n",
          path.c_str(), msgCount(), signalCount());
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

}  // namespace dbc
}  // namespace cabana
