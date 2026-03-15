#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "PlotJuggler/plotdata.h"
#include "tools/replay/seg_mgr.h"

namespace cabana::pj_engine {

struct ParsedBatch {
  uint64_t generation = 0;
  std::vector<int> segment_numbers;
  PJ::PlotDataMapRef data_map;
  qint64 parse_ms = 0;
  qint64 event_count = 0;
};

using ParsedBatchPtr = std::shared_ptr<ParsedBatch>;

class ReplayEngine : public QObject {
public:
  using BatchReadyHandler = std::function<void(const ParsedBatchPtr&)>;

  explicit ReplayEngine(QObject *parent = nullptr);
  ~ReplayEngine() override;

  void clear();
  void queueSegments(const SegmentMap &segments, uint64_t route_start_nanos);
  void setBatchReadyHandler(BatchReadyHandler handler);
  void noteBatchConsumed(const ParsedBatch &batch, qint64 append_ms);

  double routeStartSec() const;
  QString perfSummary() const;

private:
  struct SegmentTask {
    int seg_num = -1;
    std::shared_ptr<Segment> segment;
  };

  struct PerfStats {
    bool enabled = qEnvironmentVariableIsSet("CABANA_PJ_PERF");
    qint64 append_calls = 0;
    qint64 append_total_ms = 0;
    qint64 append_max_ms = 0;
    qint64 parse_calls = 0;
    qint64 parse_total_ms = 0;
    qint64 parse_max_ms = 0;
    qint64 parse_events = 0;
  };

  static ParsedBatchPtr parseSegmentBatch(std::vector<SegmentTask> tasks, uint64_t generation);
  void startNextBatch();
  void handleBatchFinished();

  std::set<int> loaded_segments_;
  std::set<int> queued_segments_;
  std::deque<SegmentTask> pending_segments_;
  double route_start_sec_ = 0.0;
  uint64_t generation_ = 0;
  QFutureWatcher<ParsedBatchPtr> watcher_;
  PerfStats perf_;
  BatchReadyHandler batch_ready_handler_;
};

}  // namespace cabana::pj_engine
