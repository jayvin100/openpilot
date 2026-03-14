#include "cabana_plotjuggler_widget.h"

#include <QCommandLineParser>
#include <QFutureWatcher>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QApplication>
#include <QtConcurrent/QtConcurrentRun>

#include <deque>

#include "plotjuggler_app/mainwindow.h"
#include "plotjuggler_app/transforms/absolute_transform.h"
#include "plotjuggler_app/transforms/first_derivative.h"
#include "plotjuggler_app/transforms/integral_transform.h"
#include "plotjuggler_app/transforms/moving_average_filter.h"
#include "plotjuggler_app/transforms/moving_rms.h"
#include "plotjuggler_app/transforms/outlier_removal.h"
#include "plotjuggler_app/transforms/scale_transform.h"
#include "plotjuggler_plugins/DataLoadRlog/rlog_parser.hpp"
#include "PlotJuggler/transform_function.h"

void initPlotJugglerResources() {
  static bool initialized = false;
  if (initialized) return;

  Q_INIT_RESOURCE(resource);
  Q_INIT_RESOURCE(color_widgets);
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

void initPlotJugglerParser(QCommandLineParser *parser) {
  parser->addOption(QCommandLineOption(QStringList{"t", "test"}, "Generate test curves at startup"));
  parser->addOption(QCommandLineOption(QStringList{"d", "datafile"}, "Load a data file", "file_path"));
  parser->addOption(QCommandLineOption(QStringList{"l", "layout"}, "Load a layout", "file_path"));
  parser->addOption(QCommandLineOption(QStringList{"p", "publish"}, "Autostart publishers"));
  parser->addOption(QCommandLineOption(QStringList{"plugin_folders"}, "Extra plugin folders", "directory_paths"));
  parser->addOption(QCommandLineOption(QStringList{"buffer_size"}, "Streaming buffer size", "seconds"));
  parser->addOption(QCommandLineOption(QStringList{"enabled_plugins"}, "Enabled plugins", "name_list"));
  parser->addOption(QCommandLineOption(QStringList{"disabled_plugins"}, "Disabled plugins", "name_list"));
  parser->addOption(QCommandLineOption(QStringList{"skin_path"}, "Skin path", "path"));
  parser->addOption(QCommandLineOption(QStringList{"start_streamer"}, "Start a streaming plugin", "file_name"));
  parser->addOption(QCommandLineOption(QStringList{"window_title"}, "Window title", "window_title"));
  parser->parse(QStringList{QStringLiteral("cabana-plotjuggler")});
}

struct SegmentTask {
  int seg_num = -1;
  std::shared_ptr<Segment> segment;
};

struct ParsedBatch {
  uint64_t generation = 0;
  std::vector<int> segment_numbers;
  PJ::PlotDataMapRef data_map;
};

using ParsedBatchPtr = std::shared_ptr<ParsedBatch>;

ParsedBatchPtr parseSegmentBatch(std::vector<SegmentTask> tasks, uint64_t generation) {
  auto batch = std::make_shared<ParsedBatch>();
  batch->generation = generation;
  RlogMessageParser parser("", batch->data_map);

  for (const auto &task : tasks) {
    batch->segment_numbers.push_back(task.seg_num);
    if (!task.segment || !task.segment->log) {
      continue;
    }

    for (const auto &event : task.segment->log->events) {
      try {
        capnp::FlatArrayMessageReader reader(event.data);
        auto dynamic_event = reader.getRoot<capnp::DynamicStruct>(parser.getSchema());
        parser.parseMessageCereal(dynamic_event);
      } catch (const kj::Exception &) {
      }
    }
  }

  return batch;
}

class CabanaPlotJugglerWidget::Impl {
public:
  Impl(CabanaPlotJugglerWidget *parent, const QString &dbc_name, const QString &layout_path)
      : dbc_name(dbc_name), layout_path(layout_path) {
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    parent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    initPlotJugglerResources();
    initPlotJugglerTransforms();
    qApp->setProperty("PlotJugglerEmbedded", true);
    QCommandLineParser parser;
    initPlotJugglerParser(&parser);
    pj_window = new MainWindow(parser, parent);
    pj_window->setParent(parent, Qt::Widget);
    pj_window->setWindowFlag(Qt::Widget, true);
    pj_window->setWindowFlag(Qt::Window, false);
    pj_window->setMinimumSize(0, 0);
    pj_window->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (auto *central = pj_window->centralWidget()) {
      central->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    layout->addWidget(pj_window, 1);
    pj_window->show();

    if (qEnvironmentVariableIsSet("CABANA_PJ_DEBUG")) {
      QTimer::singleShot(3000, parent, [this, parent]() {
        qInfo() << "CabanaPlotJugglerWidget geom" << parent->geometry() << "visible" << parent->isVisible();
        qInfo() << "Embedded MainWindow geom" << pj_window->geometry() << "visible" << pj_window->isVisible()
                << "sizeHint" << pj_window->sizeHint();
        pj_window->dumpObjectTree();
      });
    }

    QObject::connect(&watcher, &QFutureWatcher<ParsedBatchPtr>::finished, parent, [this]() {
      auto batch = watcher.result();
      if (!batch) {
        startNextBatch();
        return;
      }

      for (int seg_num : batch->segment_numbers) {
        queued_segments.erase(seg_num);
        if (batch->generation == generation) {
          loaded_segments.insert(seg_num);
        }
      }

      if (batch->generation == generation && !batch->data_map.getAllNames().empty()) {
        pj_window->appendExternalData(std::move(batch->data_map));
      }

      startNextBatch();
    });

    if (!dbc_name.isEmpty()) {
      qputenv("DBC_NAME", dbc_name.toUtf8());
    }

    const QString embedded_screenshot_path = qEnvironmentVariable("CABANA_PJ_EMBED_SCREENSHOT");
    if (!embedded_screenshot_path.isEmpty()) {
      QTimer::singleShot(20000, parent, [this, embedded_screenshot_path]() { pj_window->grab().save(embedded_screenshot_path); });
    }
  }

  ~Impl() {
    ++generation;
    loaded_segments.clear();
    queued_segments.clear();
    pending_segments.clear();
    QObject::disconnect(&watcher, nullptr, nullptr, nullptr);
    if (watcher.isRunning()) {
      watcher.waitForFinished();
    }
  }

  void refreshLayoutOnce() {
    if (layout_loaded || layout_path.isEmpty()) return;
    layout_loaded = pj_window->loadLayoutFromFile(layout_path);
    if (qEnvironmentVariableIsSet("CABANA_PJ_DEBUG")) {
      qInfo() << "Cabana PJ layout load" << layout_path << "success" << layout_loaded;
    }
  }

  void queueSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
    route_start_sec = route_start_nanos / 1e9;
    refreshLayoutOnce();

    for (const auto &[seg_num, segment] : segments) {
      if (!segment || !segment->log || loaded_segments.count(seg_num) != 0 || queued_segments.count(seg_num) != 0) {
        continue;
      }
      queued_segments.insert(seg_num);
      pending_segments.push_back({seg_num, segment});
    }

    startNextBatch();
  }

  void startNextBatch() {
    if (watcher.isRunning() || pending_segments.empty()) {
      return;
    }

    std::vector<SegmentTask> batch;
    constexpr size_t kSegmentsPerBatch = 4;
    while (!pending_segments.empty() && batch.size() < kSegmentsPerBatch) {
      batch.push_back(std::move(pending_segments.front()));
      pending_segments.pop_front();
    }
    watcher.setFuture(QtConcurrent::run(parseSegmentBatch, std::move(batch), generation));
  }

  void resizeTo(const QSize &size) {
    if (!pj_window) return;
    pj_window->resize(size);
    pj_window->updateGeometry();
  }

  MainWindow *pj_window = nullptr;
  std::set<int> loaded_segments;
  std::set<int> queued_segments;
  std::deque<SegmentTask> pending_segments;
  QString dbc_name;
  QString layout_path;
  bool layout_loaded = false;
  double route_start_sec = 0.0;
  uint64_t generation = 0;
  QFutureWatcher<ParsedBatchPtr> watcher;
};

CabanaPlotJugglerWidget::CabanaPlotJugglerWidget(const QString &dbc_name, const QString &layout_path, QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this, dbc_name, layout_path)) {
}

CabanaPlotJugglerWidget::~CabanaPlotJugglerWidget() = default;

void CabanaPlotJugglerWidget::clearData() {
  ++impl_->generation;
  impl_->loaded_segments.clear();
  impl_->queued_segments.clear();
  impl_->pending_segments.clear();
  impl_->layout_loaded = false;
  impl_->route_start_sec = 0.0;
  impl_->pj_window->clearExternalData();
  impl_->refreshLayoutOnce();
}

void CabanaPlotJugglerWidget::appendSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
  impl_->queueSegments(segments, route_start_nanos);
}

void CabanaPlotJugglerWidget::setCurrentTime(double relative_sec) {
  impl_->pj_window->setExternalTrackerTime(impl_->route_start_sec + relative_sec);
}

void CabanaPlotJugglerWidget::setPlaybackPaused(bool paused) {
  impl_->pj_window->setExternalPlaybackPaused(paused);
}

void CabanaPlotJugglerWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  impl_->resizeTo(event->size());
}
