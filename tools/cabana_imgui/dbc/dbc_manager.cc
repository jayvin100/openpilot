#include "dbc/dbc_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unistd.h>

#include "third_party/json11/json11.hpp"

namespace cabana {
namespace dbc {

namespace fs = std::filesystem;

namespace {

static fs::path repo_root() {
  std::array<char, 4096> buf = {};
  ssize_t count = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (count <= 0) return {};
  return fs::path(std::string(buf.data(), (size_t)count))
      .parent_path().parent_path().parent_path();
}

int primary_source(const SourceSet &sources) {
  if (sources.empty() || sources.count(kDbcSourceAll)) {
    return kDbcSourceAll;
  }
  return *sources.begin();
}

std::string file_label(const DbcFile *dbc_file) {
  if (!dbc_file) return {};
  if (!dbc_file->filename().empty()) return dbc_file->filename();
  return "untitled.dbc";
}

}  // namespace

SourceSet sourceAll() {
  return {kDbcSourceAll};
}

SourceSet groupedSourcesForBus(int source) {
  SourceSet result = {source};
  if (source >= 0 && source < 64) {
    result.insert((source + 128) & 0xFF);
    result.insert((source + 192) & 0xFF);
  }
  return result;
}

std::string sourceSetLabel(const SourceSet &sources) {
  std::string result;
  for (int source : sources) {
    if (!result.empty()) result += ", ";
    result += (source == kDbcSourceAll) ? "all" : std::to_string(source);
  }
  return result;
}

void DbcManager::bumpRevision() {
  ++revision_;
}

void DbcManager::normalizeActiveSource() {
  if (dbc_files_.empty()) {
    active_source_ = kDbcSourceAll;
    return;
  }

  if (active_source_ != kDbcSourceAll && dbc_files_.count(active_source_)) {
    return;
  }
  if (dbc_files_.count(kDbcSourceAll)) {
    active_source_ = kDbcSourceAll;
    return;
  }
  active_source_ = dbc_files_.begin()->first;
}

int DbcManager::effectiveSource(int source) const {
  if (source != kDbcSourceAll) {
    return source;
  }
  if (active_source_ != kDbcSourceAll && dbc_files_.count(active_source_)) {
    return active_source_;
  }
  return kDbcSourceAll;
}

std::shared_ptr<DbcFile> DbcManager::findSharedFileByPath(const std::string &path) const {
  for (const auto &[_, file] : dbc_files_) {
    if (file && file->filename() == path) {
      return file;
    }
  }
  return nullptr;
}

std::shared_ptr<DbcFile> DbcManager::loadSharedFile(const std::string &path) const {
  if (auto existing = findSharedFileByPath(path)) {
    return existing;
  }

  auto file = std::make_shared<DbcFile>();
  if (!file->load(path)) {
    return nullptr;
  }
  return file;
}

std::shared_ptr<DbcFile> DbcManager::findSharedDbcForSource(int source) const {
  const int resolved = effectiveSource(source);
  auto it = dbc_files_.find(resolved);
  if (it != dbc_files_.end()) {
    return it->second;
  }
  if (resolved != kDbcSourceAll) {
    it = dbc_files_.find(kDbcSourceAll);
    if (it != dbc_files_.end()) {
      return it->second;
    }
  }
  return nullptr;
}

DbcFile *DbcManager::findDbcFileForSource(int source) {
  auto file = findSharedDbcForSource(source);
  return file.get();
}

const DbcFile *DbcManager::findDbcFileForSource(int source) const {
  auto file = findSharedDbcForSource(source);
  return file.get();
}

DbcManager &dbc_manager() {
  static DbcManager mgr;
  return mgr;
}

bool DbcManager::loadFromFile(const SourceSet &sources, const std::string &path) {
  auto file = loadSharedFile(path);
  if (!file) {
    fprintf(stderr, "Failed to load DBC: %s\n", path.c_str());
    return false;
  }

  for (int source : sources) {
    dbc_files_[source] = file;
  }
  active_source_ = primary_source(sources);
  bumpRevision();
  fprintf(stderr, "Loaded DBC: %s (%d messages, %d signals) for %s\n",
          path.c_str(), (int)file->messages().size(), file->signalCount(),
          sourceSetLabel(sources).c_str());
  return true;
}

bool DbcManager::loadFromOpendbc(const SourceSet &sources, const std::string &name) {
  fs::path root = repo_root();
  fs::path dbc_path = root / "opendbc" / "dbc" / (name + ".dbc");
  if (!fs::exists(dbc_path)) {
    fprintf(stderr, "OpenDBC file not found: %s\n", dbc_path.c_str());
    return false;
  }
  return loadFromFile(sources, dbc_path.string());
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
  return loadFromOpendbc(sourceAll(), dbc_name);
}

void DbcManager::close(const SourceSet &sources) {
  bool changed = false;
  for (int source : sources) {
    changed = dbc_files_.erase(source) > 0 || changed;
  }
  if (changed) {
    normalizeActiveSource();
    bumpRevision();
  }
}

void DbcManager::close(const DbcFile *dbc_file) {
  if (!dbc_file) return;

  bool changed = false;
  for (auto it = dbc_files_.begin(); it != dbc_files_.end(); ) {
    if (it->second.get() == dbc_file) {
      it = dbc_files_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) {
    normalizeActiveSource();
    bumpRevision();
  }
}

void DbcManager::closeAll() {
  if (dbc_files_.empty()) return;
  dbc_files_.clear();
  active_source_ = kDbcSourceAll;
  bumpRevision();
}

DbcFile *DbcManager::dbc(int source) {
  return findDbcFileForSource(source);
}

const DbcFile *DbcManager::dbc(int source) const {
  return findDbcFileForSource(source);
}

std::string DbcManager::loadedName(int source) const {
  return file_label(findDbcFileForSource(source));
}

bool DbcManager::hasAnyDbc() const {
  return !allDbcFiles().empty();
}

int DbcManager::msgCount() const {
  int total = 0;
  for (const auto *file : allDbcFiles()) {
    total += file ? (int)file->messages().size() : 0;
  }
  return total;
}

int DbcManager::signalCount() const {
  int total = 0;
  for (const auto *file : allDbcFiles()) {
    total += file ? file->signalCount() : 0;
  }
  return total;
}

int DbcManager::nonEmptyDbcCount() const {
  int total = 0;
  for (const auto *file : allDbcFiles()) {
    total += file && !file->messages().empty();
  }
  return total;
}

void DbcManager::setActiveSource(int source) {
  if (source != kDbcSourceAll && source < 0) {
    source = kDbcSourceAll;
  }
  active_source_ = source;
  normalizeActiveSource();
}

bool DbcManager::save(int source) {
  auto file = findSharedDbcForSource(source);
  if (!file || !file->save()) {
    return false;
  }
  active_source_ = effectiveSource(source);
  return true;
}

bool DbcManager::saveAs(const std::string &path, int source) {
  auto file = findSharedDbcForSource(source);
  if (!file || !file->saveAs(path)) {
    return false;
  }
  active_source_ = effectiveSource(source);
  bumpRevision();
  return true;
}

const char *DbcManager::msgName(const MessageId &id) const {
  auto m = msg(id);
  return m ? m->name.c_str() : nullptr;
}

const Message *DbcManager::msg(const MessageId &id) const {
  auto *file = findDbcFile(id.source);
  return file ? file->msg(id.address) : nullptr;
}

bool DbcManager::updateMessage(const MessageId &id, const std::string &name, uint32_t size,
                               const std::string &transmitter, const std::string &comment) {
  auto *file = findDbcFile(id.source);
  if (!file || name.empty()) {
    return false;
  }
  file->updateMessage(id.address, name, size, transmitter, comment);
  active_source_ = id.source;
  bumpRevision();
  return true;
}

bool DbcManager::removeMessage(const MessageId &id) {
  auto *file = findDbcFile(id.source);
  if (!file || !file->removeMessage(id.address)) {
    return false;
  }
  active_source_ = id.source;
  bumpRevision();
  return true;
}

bool DbcManager::addSignal(const MessageId &id, const Signal &signal) {
  auto *file = findDbcFile(id.source);
  if (!file || !file->addSignal(id.address, signal)) {
    return false;
  }
  active_source_ = id.source;
  bumpRevision();
  return true;
}

bool DbcManager::updateSignal(const MessageId &id, const std::string &original_name, const Signal &signal) {
  auto *file = findDbcFile(id.source);
  if (!file || !file->updateSignal(id.address, original_name, signal)) {
    return false;
  }
  active_source_ = id.source;
  bumpRevision();
  return true;
}

bool DbcManager::removeSignal(const MessageId &id, const std::string &name) {
  auto *file = findDbcFile(id.source);
  if (!file || !file->removeSignal(id.address, name)) {
    return false;
  }
  active_source_ = id.source;
  bumpRevision();
  return true;
}

bool DbcManager::messageNameExists(const std::string &name, const MessageId &id, uint32_t ignore_address) const {
  auto *file = findDbcFile(id.source);
  if (!file || name.empty()) {
    return false;
  }
  for (const auto &[address, msg] : file->messages()) {
    if (address != ignore_address && msg.name == name) {
      return true;
    }
  }
  return false;
}

std::string DbcManager::nextMessageName(const MessageId &id) const {
  char buf[64];
  snprintf(buf, sizeof(buf), "NEW_MSG_%X", id.address);
  std::string candidate = buf;
  if (!messageNameExists(candidate, id)) {
    return candidate;
  }
  for (int suffix = 1; suffix < 10000; ++suffix) {
    snprintf(buf, sizeof(buf), "NEW_MSG_%X_%d", id.address, suffix);
    candidate = buf;
    if (!messageNameExists(candidate, id)) {
      return candidate;
    }
  }
  return "NEW_MSG";
}

std::string DbcManager::nextSignalName(const MessageId &id) const {
  auto *message = msg(id);
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

std::set<DbcFile *> DbcManager::allDbcFiles() {
  std::set<DbcFile *> files;
  for (auto &[_, file] : dbc_files_) {
    if (file) files.insert(file.get());
  }
  return files;
}

std::set<const DbcFile *> DbcManager::allDbcFiles() const {
  std::set<const DbcFile *> files;
  for (const auto &[_, file] : dbc_files_) {
    if (file) files.insert(file.get());
  }
  return files;
}

SourceSet DbcManager::sources(const DbcFile *dbc_file) const {
  SourceSet result;
  if (!dbc_file) return result;
  for (const auto &[source, file] : dbc_files_) {
    if (file.get() == dbc_file) {
      result.insert(source);
    }
  }
  return result;
}

DbcFile *DbcManager::findDbcFile(uint8_t source) {
  return findDbcFileForSource(source);
}

const DbcFile *DbcManager::findDbcFile(uint8_t source) const {
  return findDbcFileForSource(source);
}

DbcManager::Snapshot DbcManager::captureSnapshot() const {
  Snapshot snapshot;
  snapshot.active_source = active_source_;

  std::unordered_map<const DbcFile *, int> file_indexes;
  for (const auto &[source, file] : dbc_files_) {
    if (!file) continue;
    auto it = file_indexes.find(file.get());
    if (it == file_indexes.end()) {
      const int index = snapshot.files.size();
      snapshot.files.push_back({
        .filename = file->filename(),
        .contents = file->contents(),
      });
      it = file_indexes.emplace(file.get(), index).first;
    }
    snapshot.source_to_file[source] = it->second;
  }
  return snapshot;
}

bool DbcManager::restoreSnapshot(const Snapshot &snapshot) {
  std::vector<std::string> restored_paths(snapshot.files.size());
  for (const auto &[source, file_index] : snapshot.source_to_file) {
    if (file_index < 0 || file_index >= (int)restored_paths.size() || !restored_paths[file_index].empty()) {
      continue;
    }
    auto current = dbc_files_.find(source);
    if (current != dbc_files_.end() && current->second && !current->second->filename().empty()) {
      restored_paths[file_index] = current->second->filename();
    }
  }

  std::vector<std::shared_ptr<DbcFile>> files;
  files.reserve(snapshot.files.size());
  for (size_t index = 0; index < snapshot.files.size(); ++index) {
    const auto &file_snapshot = snapshot.files[index];
    auto file = std::make_shared<DbcFile>();
    const std::string &filename = restored_paths[index].empty() ? file_snapshot.filename : restored_paths[index];
    if (!file->loadFromString(file_snapshot.contents, filename)) {
      return false;
    }
    files.push_back(std::move(file));
  }

  dbc_files_.clear();
  for (const auto &[source, file_index] : snapshot.source_to_file) {
    if (file_index < 0 || file_index >= (int)files.size()) {
      return false;
    }
    dbc_files_[source] = files[file_index];
  }
  active_source_ = snapshot.active_source;
  normalizeActiveSource();
  bumpRevision();
  return true;
}

}  // namespace dbc
}  // namespace cabana
