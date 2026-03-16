#include "tools/cabana/cabana_plot_ui/lua_editor.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace cabana::plot_ui {

LuaEditorDialog::LuaEditorDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Lua Custom Math");
  resize(600, 500);

  auto *layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel("Name:"));
  name_edit_ = new QLineEdit(this);
  layout->addWidget(name_edit_);

  layout->addWidget(new QLabel("Linked source:"));
  linked_source_edit_ = new QLineEdit(this);
  layout->addWidget(linked_source_edit_);

  layout->addWidget(new QLabel("Global variables:"));
  global_editor_ = new QPlainTextEdit(this);
  global_editor_->setMinimumHeight(80);
  global_editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  layout->addWidget(global_editor_);

  layout->addWidget(new QLabel("Function:"));
  function_editor_ = new QPlainTextEdit(this);
  function_editor_->setMinimumHeight(200);
  function_editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  layout->addWidget(function_editor_);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    emit snippetSubmitted(snippet());
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

LuaEditorDialog::~LuaEditorDialog() = default;

void LuaEditorDialog::setSnippet(const cabana::pj_layout::SnippetModel &s) {
  name_edit_->setText(s.name);
  global_editor_->setPlainText(s.global_vars);
  function_editor_->setPlainText(s.function);
  linked_source_edit_->setText(s.linked_source);
}

cabana::pj_layout::SnippetModel LuaEditorDialog::snippet() const {
  cabana::pj_layout::SnippetModel s;
  s.name = name_edit_->text();
  s.global_vars = global_editor_->toPlainText();
  s.function = function_editor_->toPlainText();
  s.linked_source = linked_source_edit_->text();
  return s;
}

}  // namespace cabana::plot_ui
