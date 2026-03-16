#include "tools/cabana/cabana_plot_ui/plot_tab_widget.h"

#include <QApplication>
#include <QInputDialog>
#include <QMouseEvent>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace cabana::plot_ui {

PlotTabWidget::PlotTabWidget(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  tab_widget_ = new QTabWidget(this);
  tab_widget_->setTabsClosable(true);
  layout->addWidget(tab_widget_);

  // "+" button to create new tabs.
  auto *add_btn = new QToolButton(tab_widget_);
  add_btn->setText("+");
  add_btn->setAutoRaise(true);
  tab_widget_->setCornerWidget(add_btn, Qt::TopRightCorner);
  connect(add_btn, &QToolButton::clicked, this, [this]() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Tab", "Tab name:", QLineEdit::Normal,
                                         QString("Tab %1").arg(tab_widget_->count() + 1), &ok);
    if (ok && !name.isEmpty()) {
      emit tabCreateRequested(name);
    }
  });

  connect(tab_widget_, &QTabWidget::tabCloseRequested, this, &PlotTabWidget::onTabCloseRequested);

  // When user switches tabs, update the newly visible plots with latest data.
  connect(tab_widget_, &QTabWidget::currentChanged, this, [this](int index) {
    if (index >= 0 && index < static_cast<int>(plots_per_tab_.size()) && last_bundle_) {
      for (auto *pc : plots_per_tab_[index]) {
        pc->updateSnapshots(*last_bundle_);
      }
    }
  });

  // Double-click tab to rename.
  tab_widget_->tabBar()->installEventFilter(this);
}

PlotTabWidget::~PlotTabWidget() = default;

bool PlotTabWidget::eventFilter(QObject *obj, QEvent *event) {
  if (obj == tab_widget_->tabBar() && event->type() == QEvent::MouseButtonDblClick) {
    auto *me = static_cast<QMouseEvent *>(event);
    int idx = tab_widget_->tabBar()->tabAt(me->pos());
    if (idx >= 0) {
      bool ok = false;
      QString name = QInputDialog::getText(this, "Rename Tab", "Tab name:",
                                           QLineEdit::Normal, tab_widget_->tabText(idx), &ok);
      if (ok && !name.isEmpty()) {
        tab_widget_->setTabText(idx, name);
      }
      return true;
    }
  }
  return QWidget::eventFilter(obj, event);
}

void PlotTabWidget::onTabCloseRequested(int index) {
  if (tab_widget_->count() <= 1) return;  // keep at least one tab
  emit tabCloseRequested(index);
}

QWidget *PlotTabWidget::buildLayoutNode(const cabana::pj_layout::LayoutNode &node,
                                         QWidget *parent) {
  using Kind = cabana::pj_layout::LayoutNode::Kind;

  if (node.kind == Kind::DockArea) {
    if (node.plots.empty()) {
      return new QWidget(parent);
    }
    auto wirePlot = [this](PlotContainer *pc) {
      int idx = static_cast<int>(all_plots_.size());
      connect(pc, &PlotContainer::seekRequested, this, &PlotTabWidget::seekRequested);
      connect(pc, &PlotContainer::curveDropped, this, [this, idx](const QString &name) {
        emit curveDropped(name, idx);
      });
      connect(pc, &PlotContainer::splitRequested, this, [this, idx](Qt::Orientation o) {
        emit splitRequested(idx, o);
      });
      connect(pc, &PlotContainer::xRangeChanged, this, [this, pc](double min, double max) {
        if (!linked_zoom_) return;
        for (auto *other : all_plots_) {
          if (other != pc) other->setLinkedXRange(min, max);
        }
      });
      all_plots_.push_back(pc);
    };

    if (node.plots.size() == 1) {
      auto *pc = new PlotContainer(parent);
      pc->configure(node.plots[0]);
      wirePlot(pc);
      return pc;
    }
    auto *splitter = new QSplitter(Qt::Vertical, parent);
    for (const auto &plot_model : node.plots) {
      auto *pc = new PlotContainer(splitter);
      pc->configure(plot_model);
      wirePlot(pc);
      splitter->addWidget(pc);
    }
    return splitter;
  }

  auto *splitter = new QSplitter(node.orientation, parent);
  for (const auto &child : node.children) {
    splitter->addWidget(buildLayoutNode(child, splitter));
  }

  if (!node.sizes.empty()) {
    QList<int> sizes;
    for (double s : node.sizes) {
      sizes.append(std::max(1, static_cast<int>(s * 1000)));
    }
    splitter->setSizes(sizes);
  }

  return splitter;
}

void PlotTabWidget::rebuildFromLayout(const cabana::pj_layout::LayoutModel &layout) {
  // Save previous state for undo (skip on first load).
  if (!current_layout_.tabbed_widgets.empty() && !(layout == current_layout_)) {
    snapshotForUndo();
  }
  current_layout_ = layout;
  all_plots_.clear();
  plots_per_tab_.clear();
  while (tab_widget_->count() > 0) {
    QWidget *page = tab_widget_->widget(0);
    tab_widget_->removeTab(0);
    delete page;
  }

  if (layout.tabbed_widgets.empty()) return;

  const auto &tabbed = layout.tabbed_widgets[0];
  for (const auto &tab : tabbed.tabs) {
    size_t plots_before = all_plots_.size();
    QWidget *tab_page = nullptr;

    if (tab.containers.empty()) {
      tab_page = new QWidget(tab_widget_);
    } else if (tab.containers.size() == 1 && tab.containers[0].has_root) {
      tab_page = buildLayoutNode(tab.containers[0].root, tab_widget_);
    } else {
      auto *splitter = new QSplitter(Qt::Vertical, tab_widget_);
      for (const auto &container : tab.containers) {
        if (container.has_root) {
          splitter->addWidget(buildLayoutNode(container.root, splitter));
        }
      }
      tab_page = splitter;
    }

    tab_widget_->addTab(tab_page, tab.tab_name);
    // Track which plots belong to this tab.
    std::vector<PlotContainer *> tab_plots(all_plots_.begin() + plots_before, all_plots_.end());
    plots_per_tab_.push_back(std::move(tab_plots));
  }

  if (tabbed.current_tab_index >= 0 && tabbed.current_tab_index < tab_widget_->count()) {
    tab_widget_->setCurrentIndex(tabbed.current_tab_index);
  }
}

void PlotTabWidget::snapshotForUndo() {
  if (undo_timer_.isValid() && undo_timer_.elapsed() < 100 && !undo_stack_.empty()) {
    undo_stack_.pop_back();
  }
  undo_timer_.restart();
  while (undo_stack_.size() >= 100) undo_stack_.pop_front();
  undo_stack_.push_back(current_layout_);
  redo_stack_.clear();
}

void PlotTabWidget::undo() {
  if (undo_stack_.size() <= 1) return;
  redo_stack_.push_back(current_layout_);
  current_layout_ = undo_stack_.back();
  undo_stack_.pop_back();
  rebuildFromLayout(current_layout_);
}

void PlotTabWidget::redo() {
  if (redo_stack_.empty()) return;
  undo_stack_.push_back(current_layout_);
  current_layout_ = redo_stack_.back();
  redo_stack_.pop_back();
  rebuildFromLayout(current_layout_);
}

void PlotTabWidget::setLinkedZoom(bool enabled) {
  linked_zoom_ = enabled;
}

void PlotTabWidget::updateSnapshots(cabana::pj_engine::PlotSnapshotBundlePtr bundle) {
  if (!bundle) return;
  last_bundle_ = bundle;
  int current_tab = tab_widget_->currentIndex();
  if (current_tab >= 0 && current_tab < static_cast<int>(plots_per_tab_.size())) {
    for (auto *pc : plots_per_tab_[current_tab]) {
      pc->updateSnapshots(*bundle);
    }
  } else {
    for (auto *pc : all_plots_) {
      pc->updateSnapshots(*bundle);
    }
  }
}

void PlotTabWidget::setTrackerTime(double time) {
  for (auto *pc : all_plots_) {
    pc->setTrackerTime(time);
  }
}

void PlotTabWidget::setGridVisible(bool visible) {
  for (auto *pc : all_plots_) {
    pc->setGridVisible(visible);
  }
}

void PlotTabWidget::cycleLegend() {
  static int legend_state = 0;
  legend_state = (legend_state + 1) % 3;
  for (auto *pc : all_plots_) {
    auto *legend = pc->chart()->legend();
    switch (legend_state) {
      case 0: legend->hide(); break;
      case 1: legend->setAlignment(Qt::AlignRight); legend->show(); break;
      case 2: legend->setAlignment(Qt::AlignLeft); legend->show(); break;
    }
  }
}

void PlotTabWidget::zoomOutAll() {
  for (auto *pc : all_plots_) {
    pc->resetZoom();
  }
}

}  // namespace cabana::plot_ui
