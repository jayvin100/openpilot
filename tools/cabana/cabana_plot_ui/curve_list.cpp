#include "tools/cabana/cabana_plot_ui/curve_list.h"

#include <QHeaderView>
#include <QMimeData>

namespace cabana::plot_ui {

const char *CurveList::kCurveMimeType = "application/x-cabana-curve";

/// QTreeWidget subclass that provides curve-name drag data.
class DragTreeWidget : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

  QStringList mimeTypes() const override { return {CurveList::kCurveMimeType}; }

  QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override {
    auto *data = new QMimeData();
    QByteArray encoded;
    for (auto *item : items) {
      QString curve = item->data(0, Qt::UserRole).toString();
      if (!curve.isEmpty()) {
        encoded.append(curve.toUtf8());
        encoded.append('\n');
      }
    }
    data->setData(CurveList::kCurveMimeType, encoded);
    return data;
  }
};

CurveList::CurveList(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  filter_edit_ = new QLineEdit(this);
  filter_edit_->setPlaceholderText("Filter curves...");
  filter_edit_->setClearButtonEnabled(true);
  layout->addWidget(filter_edit_);

  tree_widget_ = new DragTreeWidget(this);
  tree_widget_->setHeaderLabels({"Curve", "Value"});
  tree_widget_->header()->setStretchLastSection(false);
  tree_widget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_widget_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree_widget_->setDragEnabled(true);
  tree_widget_->setDragDropMode(QAbstractItemView::DragOnly);
  tree_widget_->setAnimated(true);
  tree_widget_->setRootIsDecorated(true);
  layout->addWidget(tree_widget_);

  connect(filter_edit_, &QLineEdit::textChanged, this, &CurveList::applyFilter);
  // curveVisibilityChanged can be emitted programmatically if needed.
}

CurveList::~CurveList() = default;

void CurveList::addCurveToTree(const cabana::pj_engine::CurveEntry &entry) {
  if (known_curves_.count(entry.name)) return;
  known_curves_.insert(entry.name);

  QString full_name = QString::fromStdString(entry.name);
  QString group_name = QString::fromStdString(entry.group);

  // Build the tree name: split by '/' to form a hierarchy.
  QStringList parts = full_name.split('/', Qt::SkipEmptyParts);
  if (parts.isEmpty()) return;

  // If the curve name doesn't already contain the group prefix, prepend it.
  // But only if the group is different from the first path component.
  if (!group_name.isEmpty()) {
    QStringList group_parts = group_name.split('/', Qt::SkipEmptyParts);
    bool already_prefixed = !parts.isEmpty() && !group_parts.isEmpty() &&
                            parts.first() == group_parts.first();
    if (!already_prefixed) {
      parts = group_parts + parts;
    }
  }

  QTreeWidgetItem *parent = tree_widget_->invisibleRootItem();
  for (int i = 0; i < parts.size(); ++i) {
    bool is_leaf = (i == parts.size() - 1);
    const QString &part = parts[i];

    // Find existing child with this name.
    QTreeWidgetItem *child = nullptr;
    for (int c = 0; c < parent->childCount(); ++c) {
      if (parent->child(c)->text(0) == part) {
        child = parent->child(c);
        break;
      }
    }

    if (!child) {
      child = new QTreeWidgetItem(parent);
      child->setText(0, part);
      if (is_leaf) {
        child->setText(1, "-");
        child->setData(0, Qt::UserRole, full_name);
        child->setFlags(child->flags() | Qt::ItemIsDragEnabled);
      } else {
        child->setFlags(child->flags() & ~Qt::ItemIsDragEnabled);
      }
    }

    parent = child;
  }
}

void CurveList::updateTree(const cabana::pj_engine::CurveTreeSnapshot &tree) {
  tree_widget_->blockSignals(true);

  for (const auto &entry : tree.numeric) addCurveToTree(entry);
  for (const auto &entry : tree.strings) addCurveToTree(entry);
  for (const auto &entry : tree.scatter) addCurveToTree(entry);

  // Auto-expand first level for usability.
  for (int i = 0; i < tree_widget_->topLevelItemCount(); ++i) {
    tree_widget_->topLevelItem(i)->setExpanded(true);
  }

  tree_widget_->blockSignals(false);
}

void CurveList::updateValues(const cabana::pj_engine::PlotSnapshotBundle &bundle,
                             double tracker_time) {
  // Walk all leaf items and show the Y value at tracker_time.
  std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
    if (item->childCount() == 0) {
      QString curve = item->data(0, Qt::UserRole).toString();
      if (curve.isEmpty()) return;
      auto it = bundle.snapshots.find(curve.toStdString());
      if (it == bundle.snapshots.end() || !it->second || it->second->points.empty()) {
        item->setText(1, "-");
        return;
      }
      // Binary search for the point closest to tracker_time.
      const auto &pts = it->second->points;
      auto lower = std::lower_bound(pts.begin(), pts.end(), tracker_time,
                                    [](const QPointF &p, double t) { return p.x() < t; });
      if (lower == pts.end()) {
        lower = std::prev(pts.end());
      } else if (lower != pts.begin()) {
        auto prev = std::prev(lower);
        if (std::abs(prev->x() - tracker_time) < std::abs(lower->x() - tracker_time)) {
          lower = prev;
        }
      }
      item->setText(1, QString::number(lower->y(), 'g', 5));
    } else {
      for (int i = 0; i < item->childCount(); ++i) {
        visit(item->child(i));
      }
    }
  };

  tree_widget_->blockSignals(true);
  for (int i = 0; i < tree_widget_->topLevelItemCount(); ++i) {
    visit(tree_widget_->topLevelItem(i));
  }
  tree_widget_->blockSignals(false);
}

void CurveList::applyFilter(const QString &text) {
  std::function<bool(QTreeWidgetItem *)> filterItem = [&](QTreeWidgetItem *item) -> bool {
    if (item->childCount() == 0) {
      // Leaf: match against the full curve name.
      QString curve = item->data(0, Qt::UserRole).toString();
      bool match = text.isEmpty() || curve.contains(text, Qt::CaseInsensitive);
      item->setHidden(!match);
      return match;
    }
    // Branch: show if any child matches.
    bool any_visible = false;
    for (int i = 0; i < item->childCount(); ++i) {
      if (filterItem(item->child(i))) any_visible = true;
    }
    item->setHidden(!any_visible);
    if (any_visible && !text.isEmpty()) item->setExpanded(true);
    return any_visible;
  };

  for (int i = 0; i < tree_widget_->topLevelItemCount(); ++i) {
    filterItem(tree_widget_->topLevelItem(i));
  }
}

}  // namespace cabana::plot_ui
