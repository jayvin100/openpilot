#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/types.h"
#include "dbc/dbc_file.h"

namespace cabana {
namespace dbc {

constexpr int kDbcSourceAll = -1;

SourceSet sourceAll();
SourceSet groupedSourcesForBus(int source);
std::string sourceSetLabel(const SourceSet &sources);

class DbcManager {
public:
  struct SnapshotFile {
    std::string filename;
    std::string contents;
  };

  struct Snapshot {
    int active_source = kDbcSourceAll;
    std::map<int, int> source_to_file;
    std::vector<SnapshotFile> files;
  };

  // Load DBC from a file path
  bool loadFromFile(const SourceSet &sources, const std::string &path);
  bool loadFromString(const SourceSet &sources, const std::string &content,
                      const std::string &filename = {});

  // Load DBC by name from opendbc (e.g. "ford_lincoln_base_pt")
  bool loadFromOpendbc(const SourceSet &sources, const std::string &name);

  // Auto-load DBC based on car fingerprint using the fingerprint→dbc mapping
  bool loadFromFingerprint(const std::string &fingerprint);
  void close(const SourceSet &sources);
  void close(const DbcFile *dbc_file);
  void closeAll();

  // Accessors
  DbcFile *dbc(int source = kDbcSourceAll);
  const DbcFile *dbc(int source = kDbcSourceAll) const;
  std::string loadedName(int source = kDbcSourceAll) const;
  bool hasAnyDbc() const;
  int msgCount() const;
  int signalCount() const;
  int nonEmptyDbcCount() const;
  uint64_t revision() const { return revision_; }
  int activeSource() const { return active_source_; }
  void setActiveSource(int source);
  bool save(int source = kDbcSourceAll);
  bool saveAs(const std::string &path, int source = kDbcSourceAll);

  // Look up message name by address
  const char *msgName(const MessageId &id) const;

  // Look up message by address
  const Message *msg(const MessageId &id) const;
  bool updateMessage(const MessageId &id, const std::string &name, uint32_t size,
                     const std::string &transmitter, const std::string &comment);
  bool removeMessage(const MessageId &id);
  bool addSignal(const MessageId &id, const Signal &signal);
  bool updateSignal(const MessageId &id, const std::string &original_name, const Signal &signal);
  bool removeSignal(const MessageId &id, const std::string &name);
  bool messageNameExists(const std::string &name, const MessageId &id,
                         uint32_t ignore_address = UINT32_MAX) const;
  std::string nextMessageName(const MessageId &id) const;
  std::string nextSignalName(const MessageId &id) const;
  std::set<DbcFile *> allDbcFiles();
  std::set<const DbcFile *> allDbcFiles() const;
  SourceSet sources(const DbcFile *dbc_file) const;
  DbcFile *findDbcFile(uint8_t source);
  const DbcFile *findDbcFile(uint8_t source) const;
  Snapshot captureSnapshot() const;
  bool restoreSnapshot(const Snapshot &snapshot);

private:
  void bumpRevision();
  std::shared_ptr<DbcFile> loadSharedFile(const std::string &path) const;
  std::shared_ptr<DbcFile> findSharedFileByPath(const std::string &path) const;
  DbcFile *findDbcFileForSource(int source);
  const DbcFile *findDbcFileForSource(int source) const;
  std::shared_ptr<DbcFile> findSharedDbcForSource(int source) const;
  int effectiveSource(int source) const;
  void normalizeActiveSource();

  std::map<int, std::shared_ptr<DbcFile>> dbc_files_;
  int active_source_ = kDbcSourceAll;
  uint64_t revision_ = 0;
};

// Global singleton
DbcManager &dbc_manager();

}  // namespace dbc
}  // namespace cabana
