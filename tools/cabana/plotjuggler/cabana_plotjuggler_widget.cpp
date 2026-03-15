#include "cabana_plotjuggler_widget.h"

#include <QElapsedTimer>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QApplication>

#include <algorithm>

#include "plotjuggler_app/mainwindow.h"
#include "plotjuggler_app/transforms/absolute_transform.h"
#include "plotjuggler_app/transforms/first_derivative.h"
#include "plotjuggler_app/transforms/integral_transform.h"
#include "plotjuggler_app/transforms/moving_average_filter.h"
#include "plotjuggler_app/transforms/moving_rms.h"
#include "plotjuggler_app/transforms/outlier_removal.h"
#include "plotjuggler_app/transforms/scale_transform.h"
#include "PlotJuggler/transform_function.h"
#include "tools/cabana/pj_engine/replay_engine.h"

void initPlotJugglerResources() {
  static bool initialized = false;
  if (initialized) return;

  Q_INIT_RESOURCE(resource);
  Q_INIT_RESOURCE(qcodeeditor_resources);
  Q_INIT_RESOURCE(ads);
  initialized = true;
}

void initPlotJugglerTransforms() {
  static bool initialized = false;
  if (initialized) return;

  PJ::TransformFactory::registerTransform<FirstDerivative>();
  PJ::TransformFactory::registerTransform<ScaleTransform>();
  PJ::TransformFactory::registerTransform<MovingAverageFilter>();
  PJ::TransformFactory::registerTransform<MovingRMS>();
  PJ::TransformFactory::registerTransform<OutlierRemovalFilter>();
  PJ::TransformFactory::registerTransform<IntegralTransform>();
  PJ::TransformFactory::registerTransform<AbsoluteTransform>();
  initialized = true;
}

class CabanaPlotJugglerWidget::Impl {
public:
  struct PerfStats {
    bool enabled = qEnvironmentVariableIsSet("CABANA_PJ_PERF");
    mutable qint64 tracker_calls = 0;
    mutable qint64 tracker_total_ms = 0;
    mutable qint64 tracker_max_ms = 0;
  };

  Impl(CabanaPlotJugglerWidget *parent, const QString &dbc_name, const QString &layout_path)
      : dbc_name(dbc_name), layout_path(layout_path) {
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    parent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    initPlotJugglerResources();
    initPlotJugglerTransforms();
    qApp->setProperty("PlotJugglerEmbedded", true);
    pj_controller = new MainWindow({}, parent);
    pj_controller->hide();
    pj_pane = pj_controller->takeEmbeddedPane();
    if (pj_pane) {
      pj_pane->setMinimumSize(0, 0);
      pj_pane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      layout->addWidget(pj_pane, 1);
      pj_pane->show();
    }

    engine = std::make_unique<cabana::pj_engine::ReplayEngine>();
    engine->setBatchReadyHandler([this](const cabana::pj_engine::ParsedBatchPtr &batch) {
      if (!batch || batch->data_map.getAllNames().empty()) {
        return;
      }

      QElapsedTimer append_timer;
      append_timer.start();
      pj_controller->appendExternalData(std::move(batch->data_map));
      engine->noteBatchConsumed(*batch, append_timer.elapsed());
    });

    if (qEnvironmentVariableIsSet("CABANA_PJ_DEBUG")) {
      QTimer::singleShot(3000, parent, [this, parent]() {
        qInfo() << "CabanaPlotJugglerWidget geom" << parent->geometry() << "visible" << parent->isVisible();
        if (pj_pane) {
          qInfo() << "Detached PJ pane geom" << pj_pane->geometry() << "visible" << pj_pane->isVisible()
                  << "sizeHint" << pj_pane->sizeHint();
          pj_pane->dumpObjectTree();
        }
      });
    }

    if (!dbc_name.isEmpty()) {
      qputenv("DBC_NAME", dbc_name.toUtf8());
    }

    const QString embedded_screenshot_path = qEnvironmentVariable("CABANA_PJ_EMBED_SCREENSHOT");
    if (!embedded_screenshot_path.isEmpty()) {
      QTimer::singleShot(20000, parent, [this, embedded_screenshot_path]() {
        if (pj_pane) {
          pj_pane->grab().save(embedded_screenshot_path);
        }
      });
    }
  }

  ~Impl() {
    if (pj_controller) {
      pj_controller->close();
    }
    engine.reset();
  }

  void refreshLayoutOnce() {
    if (layout_loaded || layout_path.isEmpty()) return;
    layout_loaded = pj_controller->loadLayoutFromFile(layout_path);
    if (qEnvironmentVariableIsSet("CABANA_PJ_DEBUG")) {
      qInfo() << "Cabana PJ layout load" << layout_path << "success" << layout_loaded;
    }
  }

  void queueSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
    refreshLayoutOnce();
    engine->queueSegments(segments, route_start_nanos);
  }

  void resizeTo(const QSize &size) {
    if (!pj_pane) return;
    pj_pane->resize(size);
    pj_pane->updateGeometry();
  }

  QString perfSummary() const {
    if (!perf.enabled) return {};
    const auto avg = [](qint64 total, qint64 calls) -> qint64 { return calls > 0 ? total / calls : 0; };
    const QString engine_summary = engine ? engine->perfSummary() : QString();
    return QString("trk %1/%2ms max %3%4%5")
        .arg(perf.tracker_calls)
        .arg(avg(perf.tracker_total_ms, perf.tracker_calls))
        .arg(perf.tracker_max_ms)
        .arg(engine_summary.isEmpty() ? QString() : " | ")
        .arg(engine_summary);
  }

  MainWindow *pj_controller = nullptr;
  QWidget *pj_pane = nullptr;
  std::unique_ptr<cabana::pj_engine::ReplayEngine> engine;
  QString dbc_name;
  QString layout_path;
  bool layout_loaded = false;
  PerfStats perf;
};

CabanaPlotJugglerWidget::CabanaPlotJugglerWidget(const QString &dbc_name, const QString &layout_path, QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this, dbc_name, layout_path)) {
}

CabanaPlotJugglerWidget::~CabanaPlotJugglerWidget() = default;

void CabanaPlotJugglerWidget::clearData() {
  impl_->layout_loaded = false;
  impl_->engine->clear();
  impl_->pj_controller->clearExternalData();
  impl_->refreshLayoutOnce();
}

void CabanaPlotJugglerWidget::appendSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
  impl_->queueSegments(segments, route_start_nanos);
}

void CabanaPlotJugglerWidget::setCurrentTime(double relative_sec) {
  QElapsedTimer timer;
  if (impl_->perf.enabled) {
    timer.start();
  }
  impl_->pj_controller->setExternalTrackerTime(impl_->engine->routeStartSec() + relative_sec);
  if (impl_->perf.enabled) {
    const qint64 elapsed = timer.elapsed();
    impl_->perf.tracker_calls++;
    impl_->perf.tracker_total_ms += elapsed;
    impl_->perf.tracker_max_ms = std::max(impl_->perf.tracker_max_ms, elapsed);
    if (elapsed >= 16) {
      qInfo().noquote() << QString("CABANA_PJ_PERF tracker_sync=%1ms t=%2")
                               .arg(elapsed)
                               .arg(relative_sec, 0, 'f', 3);
    }
  }
}

void CabanaPlotJugglerWidget::setPlaybackPaused(bool paused) {
  impl_->pj_controller->setExternalPlaybackPaused(paused);
}

void CabanaPlotJugglerWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  impl_->resizeTo(event->size());
}

QString CabanaPlotJugglerWidget::perfSummary() const {
  return impl_->perfSummary();
}
