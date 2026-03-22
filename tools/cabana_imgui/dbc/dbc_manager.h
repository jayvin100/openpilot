#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "dbc/dbc_file.h"

namespace cabana {
namespace dbc {

class DbcManager {
public:
  struct Snapshot {
    bool has_dbc = false;
    std::string contents;
  };

  // Load DBC from a file path
  bool loadFromFile(const std::string &path);

  // Load DBC by name from opendbc (e.g. "ford_lincoln_base_pt")
  bool loadFromOpendbc(const std::string &name);

  // Auto-load DBC based on car fingerprint using the fingerprint→dbc mapping
  bool loadFromFingerprint(const std::string &fingerprint);

  // Accessors
  DbcFile *dbc() { return dbc_.get(); }
  const DbcFile *dbc() const { return dbc_.get(); }
  const std::string &loadedName() const { return loaded_name_; }
  int msgCount() const { return dbc_ ? (int)dbc_->messages().size() : 0; }
  int signalCount() const { return dbc_ ? dbc_->signalCount() : 0; }
  uint64_t revision() const { return revision_; }
  bool save();
  bool saveAs(const std::string &path);

  // Look up message name by address
  const char *msgName(uint32_t address) const;

  // Look up message by address
  const Message *msg(uint32_t address) const;
  bool updateMessage(uint32_t address, const std::string &name, uint32_t size,
                     const std::string &transmitter, const std::string &comment);
  bool removeMessage(uint32_t address);
  bool addSignal(uint32_t address, const Signal &signal);
  bool updateSignal(uint32_t address, const std::string &original_name, const Signal &signal);
  bool removeSignal(uint32_t address, const std::string &name);
  bool messageNameExists(const std::string &name, uint32_t ignore_address = UINT32_MAX) const;
  std::string nextMessageName(uint32_t address) const;
  std::string nextSignalName(uint32_t address) const;
  Snapshot captureSnapshot() const;
  bool restoreSnapshot(const Snapshot &snapshot);

private:
  void bumpRevision();

  std::unique_ptr<DbcFile> dbc_;
  std::string loaded_name_;
  uint64_t revision_ = 0;
};

// Global singleton
DbcManager &dbc_manager();

}  // namespace dbc
}  // namespace cabana
