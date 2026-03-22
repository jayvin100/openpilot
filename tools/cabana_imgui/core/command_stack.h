#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/app_state.h"
#include "dbc/dbc_manager.h"

namespace cabana {

class CommandStack {
public:
  struct Command {
    std::string label;
    std::function<bool()> undo;
    std::function<bool()> redo;
  };

  bool canUndo() const;
  bool canRedo() const;
  const std::string &undoLabel() const;
  const std::string &redoLabel() const;
  void push(Command command);
  bool undo();
  bool redo();
  void clear();

private:
  std::vector<Command> commands_;
  size_t next_index_ = 0;
};

CommandStack &command_stack();

void pushSnapshotCommand(const std::string &label,
                         const dbc::DbcManager::Snapshot &before_dbc,
                         const AppState::EditSnapshot &before_app,
                         const dbc::DbcManager::Snapshot &after_dbc,
                         const AppState::EditSnapshot &after_app);

}  // namespace cabana
