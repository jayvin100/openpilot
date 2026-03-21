#pragma once

#include <memory>
#include <string>

#include "dbc/dbc_file.h"

namespace cabana {
namespace dbc {

class DbcManager {
public:
  // Load DBC from a file path
  bool loadFromFile(const std::string &path);

  // Load DBC by name from opendbc (e.g. "ford_lincoln_base_pt")
  bool loadFromOpendbc(const std::string &name);

  // Auto-load DBC based on car fingerprint using the fingerprint→dbc mapping
  bool loadFromFingerprint(const std::string &fingerprint);

  // Accessors
  const DbcFile *dbc() const { return dbc_.get(); }
  const std::string &loadedName() const { return loaded_name_; }
  int msgCount() const { return dbc_ ? (int)dbc_->messages().size() : 0; }
  int signalCount() const { return dbc_ ? dbc_->signalCount() : 0; }

  // Look up message name by address
  const char *msgName(uint32_t address) const;

  // Look up message by address
  const Message *msg(uint32_t address) const;

private:
  std::unique_ptr<DbcFile> dbc_;
  std::string loaded_name_;
};

// Global singleton
DbcManager &dbc_manager();

}  // namespace dbc
}  // namespace cabana
