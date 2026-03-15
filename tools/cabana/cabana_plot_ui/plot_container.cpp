#include "tools/cabana/cabana_plot_ui/plot_container.h"
#include "tools/cabana/cabana_plot_ui/curve_list.h"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QElapsedTimer>
#include <QDomDocument>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPen>
#include <QSettings>

#include "qwt_plot.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_grid.h"
#include "qwt_plot_marker.h"
#include "qwt_text.h"
#include "qwt_scale_map.h"
#include "timeseries_qwt.h"
#include "PlotJuggler/svg_util.h"
#include "tools/cabana/pj_layout/layout_model.h"

namespace cabana::plot_ui {

static const QColor kDefaultColors[] = {
    QColor(31, 119, 180),  QColor(255, 127, 14), QColor(44, 160, 44),
    QColor(214, 39, 40),   QColor(148, 103, 189), QColor(140, 86, 75),
    QColor(227, 119, 194), QColor(127, 127, 127), QColor(188, 189, 34),
    QColor(23, 190, 207),
};

PlotContainer::PlotContainer(QWidget *parent)
    : PJ::PlotWidgetBase(parent, [this](const PJ::PlotDataXY *s) { return lookupSnapshot(s); }) {
  PlotWidgetBase::setAcceptDrops(true);

  // Wire drop signals from PlotWidgetBase's event forwarding.
  connect(this, &PJ::PlotWidgetBase::dropSignal, this, [this](QDropEvent *event) {
    if (!event->mimeData()->hasFormat(CurveList::kCurveMimeType)) return;
    QByteArray data = event->mimeData()->data(CurveList::kCurveMimeType);
    for (const QString &name : QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts)) {
      emit curveDropped(name.trimmed());
    }
    event->acceptProposedAction();
  });
  connect(this, &PJ::PlotWidgetBase::dragEnterSignal, this, [](QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(CurveList::kCurveMimeType)) {
      event->acceptProposedAction();
    }
  });

  // Catch right-click on the canvas for context menu.
  qwtPlot()->canvas()->installEventFilter(this);

  // Emit xRangeChanged when the user zooms/pans.
  connect(this, &PJ::PlotWidgetBase::viewResized, this, [this](const QRectF &r) {
    emit xRangeChanged(r.left(), r.right());
  });

  // Grid — enabled by default like PJ.
  grid_ = new QwtPlotGrid();
  grid_->setPen(QPen(Qt::gray, 0.0, Qt::DotLine));
  grid_->enableX(true);
  grid_->enableXMin(true);
  grid_->enableY(true);
  grid_->enableYMin(true);
  grid_->attach(qwtPlot());

  // Tracker line + value text.
  tracker_line_ = new QwtPlotMarker();
  tracker_line_->setLineStyle(QwtPlotMarker::VLine);
  tracker_line_->setLinePen(QPen(Qt::red, 1.0, Qt::DashLine));
  tracker_line_->setVisible(false);
  tracker_line_->attach(qwtPlot());

  tracker_text_ = new QwtPlotMarker();
  tracker_text_->setVisible(false);
  tracker_text_->attach(qwtPlot());
}

PlotContainer::~PlotContainer() = default;

cabana::pj_engine::SeriesSnapshotPtr PlotContainer::lookupSnapshot(
    const PJ::PlotDataXY *series) const {
  // The lookup key is PlotData* but the snapshot_lookup receives const PlotDataXY*.
  // Since PlotData extends PlotDataXY, the pointer values are the same.
  auto *key = const_cast<PJ::PlotData *>(static_cast<const PJ::PlotData *>(series));
  auto name_it = series_to_name_.find(key);
  if (name_it == series_to_name_.end()) return {};
  auto snap_it = latest_bundle_.snapshots.find(name_it->second);
  if (snap_it == latest_bundle_.snapshots.end()) return {};
  return snap_it->second;
}

void PlotContainer::configure(const cabana::pj_layout::PlotModel &model) {
  removeAllCurves();
  series_to_name_.clear();
  local_data_ = PJ::PlotDataMapRef{};

  xy_mode_ = (model.mode == "XYPlot");
  setModeXY(xy_mode_);
  transform_curves_.clear();
  first_data_received_ = false;

  // Phase 1: Create ALL placeholders first so the unordered_map won't rehash
  // and invalidate pointers during Phase 2.
  std::vector<std::string> curve_names;
  for (const auto &binding : model.curves) {
    std::string name = binding.name.toStdString();
    local_data_.addNumeric(name);
    curve_names.push_back(name);
  }

  // Phase 2: Now pointers are stable — collect them and add curves.
  for (size_t i = 0; i < model.curves.size(); ++i) {
    const auto &binding = model.curves[i];
    const std::string &name = curve_names[i];
    auto it = local_data_.numeric.find(name);
    if (it == local_data_.numeric.end()) continue;

    series_to_name_[&(it->second)] = name;

    QColor color;
    if (!binding.color.isEmpty()) {
      color = QColor(binding.color);
    } else {
      color = kDefaultColors[i % 10];
    }

    auto *ci = addCurve(name, it->second, color);

    // Apply per-curve transform from the layout binding.
    if (ci && binding.has_transform && !binding.transform.name.isEmpty()) {
      transform_curves_.insert(name);
      auto *ts = dynamic_cast<TransformedTimeseries *>(ci->curve->data());
      if (ts) {
        ts->setTransform(binding.transform.name);
        if (ts->transform()) {
          QDomDocument doc;
          QDomElement el = doc.createElement("transform");
          el.setAttribute("name", binding.transform.name);
          el.setAttribute("alias", binding.transform.alias);
          for (const auto &child : binding.transform.children) {
            el.appendChild(cabana::pj_layout::ToDomElement(child, &doc));
          }
          ts->transform()->xmlLoadState(el);
        }
        ts->updateCache(true);
        if (!binding.transform.alias.isEmpty()) {
          ts->setAlias(binding.transform.alias);
          ci->curve->setTitle(binding.transform.alias);
        }
      }
    }
  }

  resetZoom();
}

void PlotContainer::updateSnapshots(const cabana::pj_engine::PlotSnapshotBundle &bundle) {
  // Only copy snapshots for curves we actually use.
  latest_bundle_.tracker_time = bundle.tracker_time;
  latest_bundle_.generation = bundle.generation;
  bool any_found = false;
  for (const auto &[_, name] : series_to_name_) {
    auto it = bundle.snapshots.find(name);
    if (it != bundle.snapshots.end()) {
      latest_bundle_.snapshots[name] = it->second;
      any_found = true;
    }
  }
  if (!any_found) return;

  // Only populate local PlotData for curves that HAVE TRANSFORMS.
  // Non-transform curves render directly from snapshots via the lookup.
  bool data_changed = false;
  for (const auto &transform_name : transform_curves_) {
    auto snap_it = latest_bundle_.snapshots.find(transform_name);
    if (snap_it == latest_bundle_.snapshots.end() || !snap_it->second) continue;
    PJ::PlotData *numeric = nullptr;
    for (auto &[ptr, n] : series_to_name_) {
      if (n == transform_name) { numeric = ptr; break; }
    }
    if (!numeric) continue;
    size_t snap_size = snap_it->second->points.size();
    if (numeric->size() == snap_size) continue;
    data_changed = true;
    numeric->clear();
    for (const auto &p : snap_it->second->points) {
      numeric->pushBack({p.x(), p.y()});
    }
  }

  // Refresh curve rendering.
  // For non-transform curves: updateCache calls snapshot_lookup — cheap.
  // For transform curves: updateCache runs calculate() — only if data changed.
  for (auto &ci : curveList()) {
    auto *series = dynamic_cast<QwtSeriesWrapper *>(ci.curve->data());
    if (series) series->updateCache(data_changed);
  }

  QElapsedTimer replot_timer;
  if (qEnvironmentVariableIsSet("CABANA_PJ_PERF")) replot_timer.start();

  if (!first_data_received_) {
    first_data_received_ = true;
    resetZoom();
  } else {
    updateMaximumZoomArea();
    replot();
  }

  if (qEnvironmentVariableIsSet("CABANA_PJ_PERF") && replot_timer.elapsed() >= 16) {
    fprintf(stderr, "CABANA_PJ_PERF UI replot=%lldms curves=%zu\n",
            replot_timer.elapsed(), curveList().size());
  }
}

void PlotContainer::setTrackerTime(double time) {
  tracker_line_->setXValue(time);
  tracker_line_->setVisible(true);

  // Build value text showing each curve's Y at tracker position.
  // Skip if too many curves (perf) or if this is a non-visible update.
  QString text;
  if (series_to_name_.size() > 20) {
    tracker_text_->setVisible(false);
    replot();
    return;
  }
  for (const auto &[ptr, name] : series_to_name_) {
    auto snap_it = latest_bundle_.snapshots.find(name);
    if (snap_it == latest_bundle_.snapshots.end() || !snap_it->second) continue;
    const auto &pts = snap_it->second->points;
    if (pts.empty()) continue;
    auto lower = std::lower_bound(pts.begin(), pts.end(), time,
                                  [](const QPointF &p, double t) { return p.x() < t; });
    if (lower == pts.end()) {
      lower = std::prev(pts.end());
    } else if (lower != pts.begin()) {
      auto prev = std::prev(lower);
      if (std::abs(prev->x() - time) < std::abs(lower->x() - time)) lower = prev;
    }
    // Find this curve's color.
    QColor color = Qt::white;
    for (const auto &ci : curveList()) {
      if (ci.src_name == name) { color = ci.curve->pen().color(); break; }
    }
    if (!text.isEmpty()) text += "\n";
    text += QString("<font color='%1'>%2: %3</font>")
        .arg(color.name()).arg(QString::fromStdString(name).section('/', -1))
        .arg(lower->y(), 0, 'g', 5);
  }

  if (!text.isEmpty()) {
    QwtText label(text);
    label.setRenderFlags(Qt::AlignLeft | Qt::AlignTop);
    label.setBackgroundBrush(QBrush(QColor(40, 40, 40, 200)));
    label.setColor(Qt::white);
    auto range_y = getVisualizationRangeY({std::numeric_limits<double>::lowest(),
                                           std::numeric_limits<double>::max()});
    tracker_text_->setXValue(time);
    tracker_text_->setYValue(range_y.max);
    tracker_text_->setLabel(label);
    tracker_text_->setVisible(true);
  }

  replot();
}

void PlotContainer::setGridVisible(bool visible) {
  grid_->enableX(visible);
  grid_->enableXMin(visible);
  grid_->enableY(visible);
  grid_->enableYMin(visible);
  replot();
}

void PlotContainer::setLinkedXRange(double min, double max) {
  qwtPlot()->setAxisScale(QwtPlot::xBottom, min, max);
  replot();
}

PJ::Range PlotContainer::getXRange() const {
  auto range = getVisualizationRangeX();
  return range;
}

bool PlotContainer::eventFilter(QObject *obj, QEvent *event) {
  if (!qwtPlot() || !qwtPlot()->canvas()) return PlotWidgetBase::eventFilter(obj, event);
  if (obj == qwtPlot()->canvas() && event->type() == QEvent::MouseButtonPress) {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::RightButton && me->modifiers() == Qt::NoModifier) {
      QSettings settings;
      QString theme = settings.value("StyleSheet::theme", "dark").toString();

      QMenu menu(qwtPlot());
      menu.addAction(LoadSvg(":/resources/svg/add_column.svg", theme),
                     "Split Horizontal", this, [this]() { emit splitRequested(Qt::Horizontal); });
      menu.addAction(LoadSvg(":/resources/svg/add_row.svg", theme),
                     "Split Vertical", this, [this]() { emit splitRequested(Qt::Vertical); });
      menu.addSeparator();
      menu.addAction(LoadSvg(":/resources/svg/zoom_max.svg", theme),
                     "Zoom Out", this, [this]() { resetZoom(); });
      menu.addAction(LoadSvg(":/resources/svg/zoom_horizontal.svg", theme),
                     "Zoom Out Horizontally", this, [this]() {
        auto range = getVisualizationRangeX();
        qwtPlot()->setAxisScale(QwtPlot::xBottom, range.min, range.max);
        replot();
      });
      menu.addAction(LoadSvg(":/resources/svg/zoom_vertical.svg", theme),
                     "Zoom Out Vertically", this, [this]() {
        PJ::Range full_x = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max()};
        auto range = getVisualizationRangeY(full_x);
        qwtPlot()->setAxisScale(QwtPlot::yLeft, range.min, range.max);
        replot();
      });
      menu.addSeparator();

      // Per-curve actions: find curve closest to click.
      auto findClickedCurve = [&]() -> PJ::PlotWidgetBase::CurveInfo * {
        const QwtScaleMap xMap = qwtPlot()->canvasMap(QwtPlot::xBottom);
        const QwtScaleMap yMap = qwtPlot()->canvasMap(QwtPlot::yLeft);
        double click_x = xMap.invTransform(me->pos().x());
        double click_y = yMap.invTransform(me->pos().y());
        double best_dist = std::numeric_limits<double>::max();
        PJ::PlotWidgetBase::CurveInfo *best = nullptr;
        for (auto &ci : curveList()) {
          auto *sw = dynamic_cast<QwtSeriesWrapper *>(ci.curve->data());
          if (!sw || sw->size() == 0) continue;
          // Check distance to nearest point.
          for (size_t i = 0; i < std::min<size_t>(sw->size(), 500); ++i) {
            QPointF p = sw->sample(i);
            double dx = xMap.transform(p.x()) - xMap.transform(click_x);
            double dy = yMap.transform(p.y()) - yMap.transform(click_y);
            double dist = dx * dx + dy * dy;
            if (dist < best_dist) {
              best_dist = dist;
              best = &ci;
            }
          }
        }
        return (best_dist < 400) ? best : nullptr;  // 20px threshold
      };

      auto *clicked = findClickedCurve();
      if (clicked) {
        QString curve_title = clicked->curve->title().text();
        menu.addAction("Change Color: " + curve_title, this, [this, clicked]() {
          QColor color = QColorDialog::getColor(clicked->curve->pen().color(), this);
          if (color.isValid()) {
            clicked->curve->setPen(color, 1.3);
            replot();
          }
        });
        menu.addAction("Remove: " + curve_title, this, [this, clicked]() {
          removeCurve(clicked->curve->title().text());
          replot();
        });
        menu.addSeparator();
      }

      menu.addAction(LoadSvg(":/resources/svg/trash.svg", theme),
                     "Remove All Curves", this, [this]() { removeAllCurves(); replot(); });
      menu.addAction(LoadSvg(":/resources/svg/plot_image.svg", theme),
                     "Copy Image to Clipboard", this, [this]() {
        QApplication::clipboard()->setPixmap(grab());
      });
      menu.exec(me->globalPos());
      return true;
    }
  }
  return PlotWidgetBase::eventFilter(obj, event);
}

}  // namespace cabana::plot_ui
