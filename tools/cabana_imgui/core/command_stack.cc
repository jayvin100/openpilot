#include "core/command_stack.h"

#include <utility>

namespace cabana {

namespace {

const std::string kEmptyLabel;

bool restore_snapshots(const dbc::DbcManager::Snapshot &dbc_snapshot,
                       const AppState::EditSnapshot &app_snapshot) {
  if (!cabana::dbc::dbc_manager().restoreSnapshot(dbc_snapshot)) {
    return false;
  }
  cabana::app_state().restoreEditSnapshot(app_snapshot);
  return true;
}

}  // namespace

bool CommandStack::canUndo() const {
  return next_index_ > 0;
}

bool CommandStack::canRedo() const {
  return next_index_ < commands_.size();
}

const std::string &CommandStack::undoLabel() const {
  if (!canUndo()) return kEmptyLabel;
  return commands_[next_index_ - 1].label;
}

const std::string &CommandStack::redoLabel() const {
  if (!canRedo()) return kEmptyLabel;
  return commands_[next_index_].label;
}

void CommandStack::push(Command command) {
  if (next_index_ < commands_.size()) {
    commands_.erase(commands_.begin() + next_index_, commands_.end());
  }
  commands_.push_back(std::move(command));
  next_index_ = commands_.size();
}

bool CommandStack::undo() {
  if (!canUndo()) return false;
  Command &command = commands_[next_index_ - 1];
  if (!command.undo || !command.undo()) {
    return false;
  }
  --next_index_;
  return true;
}

bool CommandStack::redo() {
  if (!canRedo()) return false;
  Command &command = commands_[next_index_];
  if (!command.redo || !command.redo()) {
    return false;
  }
  ++next_index_;
  return true;
}

void CommandStack::clear() {
  commands_.clear();
  next_index_ = 0;
}

CommandStack &command_stack() {
  static CommandStack stack;
  return stack;
}

void pushSnapshotCommand(const std::string &label,
                         const dbc::DbcManager::Snapshot &before_dbc,
                         const AppState::EditSnapshot &before_app,
                         const dbc::DbcManager::Snapshot &after_dbc,
                         const AppState::EditSnapshot &after_app) {
  command_stack().push(CommandStack::Command{
    .label = label,
    .undo = [before_dbc, before_app]() {
      return restore_snapshots(before_dbc, before_app);
    },
    .redo = [after_dbc, after_app]() {
      return restore_snapshots(after_dbc, after_app);
    },
  });
}

}  // namespace cabana
