#pragma once

#include <QDialog>

#include "tools/cabana/pj_layout/layout_model.h"

class QCodeEditor;
class QLineEdit;
class QComboBox;

namespace cabana::plot_ui {

/// Dialog for editing Lua custom math snippets.
class LuaEditorDialog : public QDialog {
  Q_OBJECT

public:
  explicit LuaEditorDialog(QWidget *parent = nullptr);
  ~LuaEditorDialog() override;

  void setSnippet(const cabana::pj_layout::SnippetModel &snippet);
  cabana::pj_layout::SnippetModel snippet() const;

signals:
  void snippetSubmitted(cabana::pj_layout::SnippetModel snippet);

private:
  QLineEdit *name_edit_ = nullptr;
  QCodeEditor *global_editor_ = nullptr;
  QCodeEditor *function_editor_ = nullptr;
  QLineEdit *linked_source_edit_ = nullptr;
};

}  // namespace cabana::plot_ui
