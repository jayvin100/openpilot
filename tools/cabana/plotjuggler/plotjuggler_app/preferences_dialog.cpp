/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "preferences_dialog.h"
#include "ui_preferences_dialog.h"
#include <QSettings>

PreferencesDialog::PreferencesDialog(QWidget* parent)
  : QDialog(parent), ui(new Ui::PreferencesDialog)
{
  ui->setupUi(this);
  QSettings settings;
  QString theme = settings.value("Preferences::theme", "light").toString();
  if (theme == "dark")
  {
    ui->comboBoxTheme->setCurrentIndex(1);
  }
  else
  {
    ui->comboBoxTheme->setCurrentIndex(0);
  }

  bool use_plot_color_index =
      settings.value("Preferences::use_plot_color_index", false).toBool();
  bool remember_color = settings.value("Preferences::remember_color", true).toBool();

  ui->checkBoxRememberColor->setChecked(remember_color);
  ui->radioLocalColorIndex->setChecked(use_plot_color_index);
  ui->radioGlobalColorIndex->setChecked(!use_plot_color_index);

  bool use_separator = settings.value("Preferences::use_separator", true).toBool();
  ui->checkBoxSeparator->setChecked(use_separator);

  bool use_opengl = settings.value("Preferences::use_opengl", true).toBool();
  ui->checkBoxOpenGL->setChecked(use_opengl);

  bool autozoom_visibility = settings.value("Preferences::autozoom_visibility",true).toBool();
  ui->checkBoxAutoZoomVisibility->setChecked(autozoom_visibility);

  bool autozoom_curve_added = settings.value("Preferences::autozoom_curve_added",true).toBool();
  ui->checkBoxAutoZoomAdded->setChecked(autozoom_curve_added);

  bool autozoom_filter_applied = settings.value("Preferences::autozoom_filter_applied",true).toBool();
  ui->checkBoxAutoZoomFilter->setChecked(autozoom_filter_applied);
}

PreferencesDialog::~PreferencesDialog()
{
  delete ui;
}

void PreferencesDialog::on_buttonBox_accepted()
{
  QSettings settings;
  settings.setValue("Preferences::theme",
                    ui->comboBoxTheme->currentIndex() == 1 ? "dark" : "light");
  settings.setValue("Preferences::remember_color",
                    ui->checkBoxRememberColor->isChecked());
  settings.setValue("Preferences::use_plot_color_index",
                    ui->radioLocalColorIndex->isChecked());
  settings.setValue("Preferences::use_separator", ui->checkBoxSeparator->isChecked());
  settings.setValue("Preferences::use_opengl", ui->checkBoxOpenGL->isChecked());
  settings.setValue("Preferences::autozoom_visibility", ui->checkBoxAutoZoomVisibility->isChecked());
  settings.setValue("Preferences::autozoom_curve_added", ui->checkBoxAutoZoomAdded->isChecked());
  settings.setValue("Preferences::autozoom_filter_applied", ui->checkBoxAutoZoomFilter->isChecked());

  settings.sync();
}
