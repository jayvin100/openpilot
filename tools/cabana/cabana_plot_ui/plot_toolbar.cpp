#include "tools/cabana/cabana_plot_ui/plot_toolbar.h"

#include <QSettings>
#include <QShortcut>

#include "PlotJuggler/svg_util.h"

namespace cabana::plot_ui {

PlotToolbar::PlotToolbar(QWidget *parent) : QToolBar(parent) {
  setIconSize(QSize(24, 24));
  setMovable(false);

  QSettings settings;
  QString theme = settings.value("StyleSheet::theme", "dark").toString();

  // ── Playback controls ──

  play_btn_ = new QPushButton(this);
  play_btn_->setCheckable(true);
  play_btn_->setIcon(LoadSvg(":/resources/svg/play_arrow.svg", theme));
  play_btn_->setToolTip("Play / Pause");
  play_btn_->setFixedSize(28, 28);
  play_btn_->setFlat(true);
  addWidget(play_btn_);
  connect(play_btn_, &QPushButton::toggled, this, [this](bool checked) {
    paused_ = !checked;
    QSettings s;
    QString t = s.value("StyleSheet::theme", "dark").toString();
    play_btn_->setIcon(LoadSvg(checked ? ":/resources/svg/pause.svg"
                                       : ":/resources/svg/play_arrow.svg", t));
    emit playPauseToggled(paused_);
  });

  addSeparator();

  // Speed
  auto *speed_label = new QLabel(" Speed:", this);
  speed_label->setStyleSheet("font-weight: bold;");
  addWidget(speed_label);

  speed_spin_ = new QDoubleSpinBox(this);
  speed_spin_->setRange(0.1, 10.0);
  speed_spin_->setSingleStep(0.1);
  speed_spin_->setValue(1.0);
  speed_spin_->setSuffix("x");
  speed_spin_->setFixedWidth(70);
  addWidget(speed_spin_);
  connect(speed_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &PlotToolbar::speedChanged);

  addSeparator();

  // ── Plot controls ──

  grid_btn_ = new QPushButton(this);
  grid_btn_->setCheckable(true);
  grid_btn_->setChecked(true);
  grid_btn_->setIcon(LoadSvg(":/resources/svg/grid.svg", theme));
  grid_btn_->setToolTip("Toggle Grid");
  grid_btn_->setFixedSize(28, 28);
  grid_btn_->setFlat(true);
  addWidget(grid_btn_);
  connect(grid_btn_, &QPushButton::toggled, this, &PlotToolbar::gridToggled);

  legend_btn_ = new QPushButton(this);
  legend_btn_->setIcon(LoadSvg(":/resources/svg/legend.svg", theme));
  legend_btn_->setToolTip("Cycle Legend Position");
  legend_btn_->setFixedSize(28, 28);
  legend_btn_->setFlat(true);
  addWidget(legend_btn_);
  connect(legend_btn_, &QPushButton::clicked, this, &PlotToolbar::legendToggled);

  link_btn_ = new QPushButton(this);
  link_btn_->setCheckable(true);
  link_btn_->setChecked(true);
  link_btn_->setIcon(LoadSvg(":/resources/svg/link.svg", theme));
  link_btn_->setToolTip("Linked Zoom");
  link_btn_->setFixedSize(28, 28);
  link_btn_->setFlat(true);
  addWidget(link_btn_);
  connect(link_btn_, &QPushButton::toggled, this, &PlotToolbar::linkedZoomToggled);

  auto *zoom_out_btn = new QPushButton(this);
  zoom_out_btn->setIcon(LoadSvg(":/resources/svg/zoom_max.svg", theme));
  zoom_out_btn->setToolTip("Zoom Out All");
  zoom_out_btn->setFixedSize(28, 28);
  zoom_out_btn->setFlat(true);
  addWidget(zoom_out_btn);
  connect(zoom_out_btn, &QPushButton::clicked, this, &PlotToolbar::zoomOutAll);

  addSeparator();

  // ── Layout controls ──

  auto *save_btn = new QPushButton(this);
  save_btn->setIcon(LoadSvg(":/resources/svg/export.svg", theme));
  save_btn->setToolTip("Save Layout");
  save_btn->setFixedSize(28, 28);
  save_btn->setFlat(true);
  addWidget(save_btn);
  connect(save_btn, &QPushButton::clicked, this, &PlotToolbar::saveLayoutRequested);

  auto *load_btn = new QPushButton(this);
  load_btn->setIcon(LoadSvg(":/resources/svg/import.svg", theme));
  load_btn->setToolTip("Load Layout");
  load_btn->setFixedSize(28, 28);
  load_btn->setFlat(true);
  addWidget(load_btn);
  connect(load_btn, &QPushButton::clicked, this, &PlotToolbar::loadLayoutRequested);

  addSeparator();

  // ── Time display controls ──

  auto *offset_btn = new QPushButton(this);
  offset_btn->setCheckable(true);
  offset_btn->setChecked(true);
  offset_btn->setIcon(LoadSvg(":/resources/svg/datetime.svg", theme));
  offset_btn->setToolTip("Remove Time Offset");
  offset_btn->setFixedSize(28, 28);
  offset_btn->setFlat(true);
  addWidget(offset_btn);
  connect(offset_btn, &QPushButton::toggled, this, &PlotToolbar::timeOffsetToggled);

  // ── Keyboard shortcuts (parented to toolbar so they work when toolbar is visible) ──

  auto *undo_sc = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Z), this);
  connect(undo_sc, &QShortcut::activated, this, &PlotToolbar::undoRequested);
  auto *redo_sc = new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Z), this);
  connect(redo_sc, &QShortcut::activated, this, &PlotToolbar::redoRequested);
}

void PlotToolbar::setPaused(bool paused) {
  paused_ = paused;
  play_btn_->blockSignals(true);
  play_btn_->setChecked(!paused);
  QSettings s;
  QString t = s.value("StyleSheet::theme", "dark").toString();
  play_btn_->setIcon(LoadSvg(!paused ? ":/resources/svg/pause.svg"
                                     : ":/resources/svg/play_arrow.svg", t));
  play_btn_->blockSignals(false);
}

}  // namespace cabana::plot_ui
