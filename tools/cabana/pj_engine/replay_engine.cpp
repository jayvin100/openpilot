#include "tools/cabana/pj_engine/replay_engine.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include "plotjuggler_plugins/DataLoadRlog/rlog_parser.hpp"

namespace cabana::pj_engine {

namespace {

constexpr size_t kSegmentsPerBatch = 1;
constexpr int kInterBatchDelayMs = 25;

}  // namespace

ParsedBatchPtr ReplayEngine::parseSegmentBatch(std::vector<SegmentTask> tasks, uint64_t generation) {
  QElapsedTimer timer;
  timer.start();

  auto batch = std::make_shared<ParsedBatch>();
  batch->generation = generation;
  RlogMessageParser parser("", batch->data_map);

  for (const auto &task : tasks) {
    batch->segment_numbers.push_back(task.seg_num);
    if (!task.segment || !task.segment->log) {
      continue;
    }

    for (const auto &event : task.segment->log->events) {
      batch->event_count++;
      try {
        capnp::FlatArrayMessageReader reader(event.data);
        auto dynamic_event = reader.getRoot<capnp::DynamicStruct>(parser.getSchema());
        parser.parseMessageCereal(dynamic_event);
      } catch (const kj::Exception &) {
      }
    }
  }

  batch->parse_ms = timer.elapsed();
  return batch;
}

ReplayEngine::ReplayEngine(QObject *parent) : QObject(parent) {
  QObject::connect(&watcher_, &QFutureWatcher<ParsedBatchPtr>::finished, this, &ReplayEngine::handleBatchFinished);
}

ReplayEngine::~ReplayEngine() {
  batch_ready_handler_ = {};
  QObject::disconnect(&watcher_, nullptr, this, nullptr);
  if (watcher_.isRunning()) {
    watcher_.waitForFinished();
  }
}

void ReplayEngine::clear() {
  ++generation_;
  loaded_segments_.clear();
  queued_segments_.clear();
  pending_segments_.clear();
  route_start_sec_ = 0.0;
}

void ReplayEngine::queueSegments(const SegmentMap &segments, uint64_t route_start_nanos) {
  route_start_sec_ = route_start_nanos / 1e9;

  for (const auto &[seg_num, segment] : segments) {
    if (!segment || !segment->log || loaded_segments_.count(seg_num) != 0 || queued_segments_.count(seg_num) != 0) {
      continue;
    }
    queued_segments_.insert(seg_num);
    pending_segments_.push_back({seg_num, segment});
  }

  startNextBatch();
}

void ReplayEngine::setBatchReadyHandler(BatchReadyHandler handler) {
  batch_ready_handler_ = std::move(handler);
}

void ReplayEngine::noteBatchConsumed(const ParsedBatch &batch, qint64 append_ms) {
  if (!perf_.enabled) {
    return;
  }

  perf_.append_calls++;
  perf_.append_total_ms += append_ms;
  perf_.append_max_ms = std::max(perf_.append_max_ms, append_ms);

  if (append_ms >= 16 || batch.parse_ms >= 16) {
    qInfo().noquote() << QString("CABANA_PJ_PERF batch segs=%1 events=%2 parse=%3ms append=%4ms")
                             .arg(batch.segment_numbers.size())
                             .arg(batch.event_count)
                             .arg(batch.parse_ms)
                             .arg(append_ms);
  }
}

double ReplayEngine::routeStartSec() const {
  return route_start_sec_;
}

QString ReplayEngine::perfSummary() const {
  if (!perf_.enabled) {
    return {};
  }

  const auto avg = [](qint64 total, qint64 calls) -> qint64 { return calls > 0 ? total / calls : 0; };
  return QString("app %1/%2ms max %3 | parse %4/%5ms max %6 | q %7")
      .arg(perf_.append_calls)
      .arg(avg(perf_.append_total_ms, perf_.append_calls))
      .arg(perf_.append_max_ms)
      .arg(perf_.parse_calls)
      .arg(avg(perf_.parse_total_ms, perf_.parse_calls))
      .arg(perf_.parse_max_ms)
      .arg(pending_segments_.size());
}

void ReplayEngine::startNextBatch() {
  if (watcher_.isRunning() || pending_segments_.empty()) {
    return;
  }

  std::vector<SegmentTask> batch;
  while (!pending_segments_.empty() && batch.size() < kSegmentsPerBatch) {
    batch.push_back(std::move(pending_segments_.front()));
    pending_segments_.pop_front();
  }
  watcher_.setFuture(QtConcurrent::run(parseSegmentBatch, std::move(batch), generation_));
}

void ReplayEngine::handleBatchFinished() {
  auto batch = watcher_.result();
  if (!batch) {
    startNextBatch();
    return;
  }

  for (int seg_num : batch->segment_numbers) {
    queued_segments_.erase(seg_num);
    if (batch->generation == generation_) {
      loaded_segments_.insert(seg_num);
    }
  }

  if (perf_.enabled) {
    perf_.parse_calls++;
    perf_.parse_total_ms += batch->parse_ms;
    perf_.parse_max_ms = std::max(perf_.parse_max_ms, batch->parse_ms);
    perf_.parse_events += batch->event_count;
  }

  if (batch->generation == generation_ && batch_ready_handler_) {
    batch_ready_handler_(batch);
  }

  if (!pending_segments_.empty()) {
    QTimer::singleShot(kInterBatchDelayMs, this, &ReplayEngine::startNextBatch);
  }
}

}  // namespace cabana::pj_engine
