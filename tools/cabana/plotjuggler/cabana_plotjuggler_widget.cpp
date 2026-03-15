#include "cabana_plotjuggler_widget.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QApplication>

#include <algorithm>

#include "plotjuggler_app/transforms/absolute_transform.h"
#include "plotjuggler_app/transforms/first_derivative.h"
#include "plotjuggler_app/transforms/integral_transform.h"
#include "plotjuggler_app/transforms/moving_average_filter.h"
#include "plotjuggler_app/transforms/moving_rms.h"
#include "plotjuggler_app/transforms/outlier_removal.h"
#include "plotjuggler_app/transforms/scale_transform.h"
#include "PlotJuggler/transform_function.h"
#include "tools/cabana/pj_engine/engine.h"
#include "tools/cabana/cabana_plot_ui/plot_tab_widget.h"
#include "tools/cabana/cabana_plot_ui/curve_list.h"
#include "tools/cabana/cabana_plot_ui/lua_editor.h"
#include "tools/cabana/cabana_plot_ui/plot_toolbar.h"
#include "tools/cabana/pj_layout/layout_model.h"

static void initPlotJugglerResources() {
  static bool initialized = false;
  if (initialized) return;
  Q_INIT_RESOURCE(resource);
  Q_INIT_RESOURCE(qcodeeditor_resources);
  Q_INIT_RESOURCE(ads);
  initialized = true;
}

static void applyPlotJugglerTheme() {
  static bool applied = false;
  if (applied) return;
  applied = true;
  QFile styleFile(":/resources/stylesheet_dark.qss");
  if (!styleFile.open(QFile::ReadOnly)) return;
  // Inline the palette substitution from stylesheet.h.
  QString style = QString::fromUtf8(styleFile.readAll());
  QStringList lines = style.split("\n");
  std::map<QString, QString> palette;
  int i = 0;
  while (i < lines.size()) {
    if (lines[i++].contains("PALETTE START")) break;
  }
  while (i < lines.size()) {
    auto parts = lines[i].split(":");
    if (parts.size() == 2) {
      QString value = parts[1].remove(" ").remove("\r");
      palette.insert({parts[0].remove(" "), value});
    }
    if (lines[i++].contains("PALETTE END")) break;
  }
  QString out;
  while (i < lines.size()) {
    QString line = lines[i];
    int pos = line.indexOf("${", 0);
    while (pos != -1) {
      int end = line.indexOf("}", pos);
      if (end == -1) break;
      QString id = line.mid(pos + 2, end - pos - 2);
      auto it = palette.find(id);
      if (it != palette.end()) {
        line = line.left(pos) + it->second + line.right(line.size() - end - 1);
      }
      pos = line.indexOf("${", pos + 1);
    }
    out += line + "\n";
    i++;
  }
  qApp->setStyleSheet(out);
}

static void initPlotJugglerTransforms() {
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
      : owner(parent), dbc_name(dbc_name), layout_path(layout_path) {
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    parent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    initPlotJugglerResources();
    applyPlotJugglerTheme();
    initPlotJugglerTransforms();
    qApp->setProperty("PlotJugglerEmbedded", true);

    // Disable OpenGL for plot canvases — avoids crashes in headless/xvfb.
    QSettings().setValue("Preferences::use_opengl", false);

    // Toolbar at the top.
    toolbar_ = new cabana::plot_ui::PlotToolbar(parent);
    layout->addWidget(toolbar_);

    // Build UI: left panel (curve list + Lua button) + plot surface.
    auto *splitter = new QSplitter(Qt::Horizontal, parent);
    layout->addWidget(splitter, 1);

    auto *left_panel = new QWidget(splitter);
    auto *left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(2);

    curve_list_ = new cabana::plot_ui::CurveList(left_panel);
    left_layout->addWidget(curve_list_, 1);

    auto *lua_btn = new QPushButton("Lua Editor", left_panel);
    left_layout->addWidget(lua_btn);
    QObject::connect(lua_btn, &QPushButton::clicked, parent, [this, parent]() {
      auto *dlg = new cabana::plot_ui::LuaEditorDialog(parent);
      QObject::connect(dlg, &cabana::plot_ui::LuaEditorDialog::snippetSubmitted,
                       engine_, &cabana::pj_engine::Engine::updateLua);
      dlg->setAttribute(Qt::WA_DeleteOnClose);
      dlg->show();
    });

    left_panel->setMinimumWidth(180);
    left_panel->setMaximumWidth(300);
    splitter->addWidget(left_panel);

    plot_surface_ = new cabana::plot_ui::PlotTabWidget(splitter);
    splitter->addWidget(plot_surface_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // Create Engine on a dedicated worker thread.
    engine_ = new cabana::pj_engine::Engine();
    engine_thread_ = new QThread();
    engine_->moveToThread(engine_thread_);
    QObject::connect(engine_thread_, &QThread::started, engine_,
                     &cabana::pj_engine::Engine::initialize);

    // Wire Engine output signals → UI (queued across threads automatically).
    QObject::connect(engine_, &cabana::pj_engine::Engine::snapshotsReady,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::updateSnapshots);
    QObject::connect(engine_, &cabana::pj_engine::Engine::snapshotsReady,
                     parent, [this](cabana::pj_engine::PlotSnapshotBundlePtr bundle) {
      if (!bundle) return;
      batches_applied++;
      scheduleCaptureReady();
      last_bundle_ = bundle;  // stash for throttled value updates
      // Update seek slider range (cheap — just find max).
      double max_x = 0;
      for (const auto &[_, snap] : bundle->snapshots) {
        if (snap && snap->range_x) {
          double dur = snap->range_x->max - route_start_sec_;
          if (dur > max_x) max_x = dur;
        }
      }
      if (max_x > 0) toolbar_->setTimeRange(max_x);
    });
    QObject::connect(engine_, &cabana::pj_engine::Engine::curveTreeChanged,
                     curve_list_, &cabana::plot_ui::CurveList::updateTree);

    // Wire UI commands → Engine (queued across threads automatically).
    QObject::connect(curve_list_, &cabana::plot_ui::CurveList::curveVisibilityChanged,
                     engine_, &cabana::pj_engine::Engine::setCurveVisibility);

    // Rebuild UI when Engine mutates the layout (tab create/remove, etc.).
    QObject::connect(engine_, &cabana::pj_engine::Engine::layoutChanged,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::rebuildFromLayout);

    // Tab create/close → Engine.
    QObject::connect(plot_surface_, &cabana::plot_ui::PlotTabWidget::tabCreateRequested,
                     engine_, &cabana::pj_engine::Engine::createTab);
    QObject::connect(plot_surface_, &cabana::plot_ui::PlotTabWidget::tabCloseRequested,
                     engine_, &cabana::pj_engine::Engine::removeTab);

    // Split plot → Engine.
    QObject::connect(plot_surface_, &cabana::plot_ui::PlotTabWidget::splitRequested,
                     engine_, &cabana::pj_engine::Engine::splitPlot);

    // Toolbar signals.
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::gridToggled,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::setGridVisible);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::legendToggled,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::cycleLegend);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::linkedZoomToggled,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::setLinkedZoom);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::zoomOutAll,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::zoomOutAll);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::saveLayoutRequested,
                     parent, [this, parent]() {
      QString path = QFileDialog::getSaveFileName(parent, "Save Layout", {}, "XML (*.xml)");
      if (!path.isEmpty()) owner->saveLayoutToFile(path);
    });
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::loadLayoutRequested,
                     parent, [this, parent]() {
      QString path = QFileDialog::getOpenFileName(parent, "Load Layout", {}, "XML (*.xml)");
      if (!path.isEmpty()) owner->loadLayoutFromFile(path);
    });

    // Play/pause, seek, speed → bubble up to pjwindow which controls the stream.
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::playPauseToggled,
                     parent, &CabanaPlotJugglerWidget::playPauseRequested);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::seekSliderMoved,
                     parent, &CabanaPlotJugglerWidget::seekSliderMoved);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::speedChanged,
                     parent, &CabanaPlotJugglerWidget::speedChanged);

    // Undo/Redo.
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::undoRequested,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::undo);
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::redoRequested,
                     plot_surface_, &cabana::plot_ui::PlotTabWidget::redo);

    // Time offset toggle.
    QObject::connect(toolbar_, &cabana::plot_ui::PlotToolbar::timeOffsetToggled,
                     parent, [this](bool on) { use_time_offset_ = on; });

    engine_thread_->start();

    // Throttle curve list value updates to ~4Hz (not every snapshot).
    auto *value_timer = new QTimer(parent);
    value_timer->setInterval(250);
    QObject::connect(value_timer, &QTimer::timeout, parent, [this]() {
      if (last_bundle_) {
        curve_list_->updateValues(*last_bundle_, last_bundle_->tracker_time);
      }
    });
    value_timer->start();

    if (!dbc_name.isEmpty()) {
      qputenv("DBC_NAME", dbc_name.toUtf8());
    }

    const QString embedded_screenshot_path = qEnvironmentVariable("CABANA_PJ_EMBED_SCREENSHOT");
    if (!embedded_screenshot_path.isEmpty()) {
      QTimer::singleShot(20000, parent, [this, parent, embedded_screenshot_path]() {
        parent->grab().save(embedded_screenshot_path);
      });
    }
  }

  ~Impl() {
    // Disconnect all signals first to prevent callbacks during destruction.
    QObject::disconnect(engine_, nullptr, nullptr, nullptr);
    // Stop the engine thread — quit() posts an exit event, wait() blocks until done.
    engine_thread_->quit();
    engine_thread_->wait();
    delete engine_;
    delete engine_thread_;
  }

  void refreshLayoutOnce() {
    if (layout_loaded || layout_path.isEmpty()) return;

    cabana::pj_layout::LayoutModel model;
    QString error;
    if (!cabana::pj_layout::LoadLayoutFile(layout_path, &model, &error)) {
      qWarning() << "Failed to load layout:" << layout_path << error;
      return;
    }

    // Build UI from layout (on UI thread).
    plot_surface_->rebuildFromLayout(model);
    cached_layout_ = model;

    // Send layout to Engine (queued to worker thread).
    QMetaObject::invokeMethod(engine_, "loadLayout", Qt::QueuedConnection,
                              Q_ARG(cabana::pj_layout::LayoutModel, model));
    layout_loaded = true;

    if (qEnvironmentVariableIsSet("CABANA_PJ_DEBUG")) {
      qInfo() << "Cabana PJ layout load" << layout_path << "success" << layout_loaded;
    }
  }

  void queueSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
    refreshLayoutOnce();
    route_start_sec_ = route_start_nanos / 1e9;
    QMetaObject::invokeMethod(engine_, "appendSegments", Qt::QueuedConnection,
                              Q_ARG(SegmentMap, segments),
                              Q_ARG(uint64_t, route_start_nanos));
  }

  void scheduleCaptureReady() {
    if (capture_ready_emitted || !layout_loaded || batches_applied <= 0) return;
    capture_ready_emitted = true;
    // Emit directly — the screenshot handler uses a singleShot timer internally.
    owner->emitCaptureReady();
  }

  QString perfSummary() const {
    if (!perf.enabled) return {};
    const auto avg = [](qint64 total, qint64 calls) -> qint64 { return calls > 0 ? total / calls : 0; };
    return QString("trk %1/%2ms max %3")
        .arg(perf.tracker_calls)
        .arg(avg(perf.tracker_total_ms, perf.tracker_calls))
        .arg(perf.tracker_max_ms);
  }

  CabanaPlotJugglerWidget *owner = nullptr;
  cabana::plot_ui::PlotToolbar *toolbar_ = nullptr;
  cabana::plot_ui::PlotTabWidget *plot_surface_ = nullptr;
  cabana::plot_ui::CurveList *curve_list_ = nullptr;
  cabana::pj_engine::Engine *engine_ = nullptr;
  QThread *engine_thread_ = nullptr;
  cabana::pj_engine::PlotSnapshotBundlePtr last_bundle_;
  cabana::pj_layout::LayoutModel cached_layout_;  // UI-thread copy for safe reads
  double route_start_sec_ = 0.0;
  bool use_time_offset_ = true;
  QString dbc_name;
  QString layout_path;
  bool layout_loaded = false;
  bool capture_ready_emitted = false;
  int batches_applied = 0;
  PerfStats perf;
};

CabanaPlotJugglerWidget::CabanaPlotJugglerWidget(const QString &dbc_name, const QString &layout_path, QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this, dbc_name, layout_path)) {
}

CabanaPlotJugglerWidget::~CabanaPlotJugglerWidget() = default;

void CabanaPlotJugglerWidget::clearData() {
  impl_->layout_loaded = false;
  impl_->capture_ready_emitted = false;
  impl_->batches_applied = 0;
  QMetaObject::invokeMethod(impl_->engine_, "clearData", Qt::QueuedConnection);
  impl_->refreshLayoutOnce();
}

void CabanaPlotJugglerWidget::appendSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
  impl_->queueSegments(segments, route_start_nanos);
}

void CabanaPlotJugglerWidget::setCurrentTime(double relative_sec) {
  QElapsedTimer timer;
  if (impl_->perf.enabled) timer.start();

  double abs_time = impl_->route_start_sec_ + relative_sec;
  impl_->plot_surface_->setTrackerTime(abs_time);
  impl_->toolbar_->setTime(impl_->use_time_offset_ ? relative_sec : abs_time);

  QMetaObject::invokeMethod(impl_->engine_, "seek", Qt::QueuedConnection,
                            Q_ARG(double, abs_time));

  if (impl_->perf.enabled) {
    const qint64 elapsed = timer.elapsed();
    impl_->perf.tracker_calls++;
    impl_->perf.tracker_total_ms += elapsed;
    impl_->perf.tracker_max_ms = std::max(impl_->perf.tracker_max_ms, elapsed);
  }
}

void CabanaPlotJugglerWidget::setPlaybackPaused(bool paused) {
  impl_->toolbar_->setPaused(paused);
  QMetaObject::invokeMethod(impl_->engine_, "setPaused", Qt::QueuedConnection,
                            Q_ARG(bool, paused));
}

void CabanaPlotJugglerWidget::saveLayoutToFile(const QString &path) {
  if (path.isEmpty()) return;
  // Use the UI-thread cached layout — no cross-thread access needed.
  QString xml = cabana::pj_layout::ToXmlString(impl_->cached_layout_, 1);
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    file.write(xml.toUtf8());
  }
}

void CabanaPlotJugglerWidget::loadLayoutFromFile(const QString &path) {
  if (path.isEmpty()) return;
  cabana::pj_layout::LayoutModel model;
  QString error;
  if (!cabana::pj_layout::LoadLayoutFile(path, &model, &error)) {
    qWarning() << "Failed to load layout:" << path << error;
    return;
  }
  impl_->plot_surface_->rebuildFromLayout(model);
  impl_->cached_layout_ = model;
  QMetaObject::invokeMethod(impl_->engine_, "loadLayout", Qt::QueuedConnection,
                            Q_ARG(cabana::pj_layout::LayoutModel, model));
  impl_->layout_path = path;
  impl_->layout_loaded = true;
}

void CabanaPlotJugglerWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
}

QString CabanaPlotJugglerWidget::perfSummary() const {
  return impl_->perfSummary();
}

void CabanaPlotJugglerWidget::emitCaptureReady() {
  emit captureReady();
}
