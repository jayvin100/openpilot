#include "tools/cabana/pj_engine/py_transform.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace cabana::pj_engine {

static QString pyTransformScript() {
  // Look for py_transform.py next to the executable, then in the source tree.
  QStringList candidates = {
      QCoreApplication::applicationDirPath() + "/../tools/cabana/pj_engine/py_transform.py",
      "tools/cabana/pj_engine/py_transform.py",
  };
  for (const auto &path : candidates) {
    if (QFile::exists(path)) return path;
  }
  return "tools/cabana/pj_engine/py_transform.py";
}

PyCustomFunction::PyCustomFunction(SnippetData snippet) : CustomFunction(snippet) {
  initEngine();
}

PyCustomFunction::~PyCustomFunction() {
  if (process_) {
    process_->closeWriteChannel();
    process_->waitForFinished(1000);
    process_->kill();
  }
}

void PyCustomFunction::initEngine() {
  std::lock_guard<std::mutex> lock(mutex_);
  process_ = std::make_unique<QProcess>();
  process_->setProgram("python3");
  process_->setArguments({pyTransformScript()});
  process_->start();
  if (!process_->waitForStarted(3000)) {
    qWarning("PyCustomFunction: failed to start python3");
    process_.reset();
  }
  batch_done_ = false;
}

void PyCustomFunction::calculate() {
  auto *dst = _dst_vector.empty() ? nullptr : _dst_vector.front();
  if (!dst) return;

  auto data_it = plotData()->numeric.find(_linked_plot_name);
  if (data_it == plotData()->numeric.end()) return;

  std::vector<const PJ::PlotData *> src_vec;
  src_vec.push_back(&data_it->second);
  for (const auto &channel : _used_channels) {
    auto it = plotData()->numeric.find(channel);
    if (it == plotData()->numeric.end()) return;
    src_vec.push_back(&it->second);
  }

  dst->setMaximumRangeX(src_vec.front()->maximumRangeX());
  batchCalculate(src_vec, dst);
}

void PyCustomFunction::calculatePoints(const std::vector<const PJ::PlotData *> &channels,
                                       size_t point_index,
                                       std::vector<PJ::PlotData::Point> &points) {
  // The base CustomFunction::calculate() calls this per-point.
  // We override calculate() via batchCalculate to do everything at once.
  // This per-point fallback should not be called, but handle it gracefully.
  if (channels.empty() || point_index >= channels[0]->size()) return;
  const auto &p = channels[0]->at(point_index);
  points.push_back(p);
}

void PyCustomFunction::batchCalculate(const std::vector<const PJ::PlotData *> &channels,
                                      PJ::PlotData *dst) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!process_ || process_->state() != QProcess::Running) return;

  const PJ::PlotData *main_src = channels.empty() ? nullptr : channels[0];
  if (!main_src || main_src->size() == 0) return;

  // Build JSON command with all data arrays.
  QJsonObject cmd;
  cmd["global"] = _snippet.global_vars;
  cmd["function"] = _snippet.function;

  QJsonArray time_arr, value_arr;
  for (size_t i = 0; i < main_src->size(); ++i) {
    time_arr.append(main_src->at(i).x);
    value_arr.append(main_src->at(i).y);
  }
  cmd["time"] = time_arr;
  cmd["value"] = value_arr;

  // Additional sources (v1, v2, ...).
  for (size_t c = 1; c < channels.size(); ++c) {
    QJsonArray arr;
    for (size_t i = 0; i < main_src->size(); ++i) {
      int idx = channels[c]->getIndexFromX(main_src->at(i).x);
      arr.append(idx >= 0 ? channels[c]->at(idx).y : 0.0);
    }
    cmd[QString("v%1").arg(c)] = arr;
  }

  QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
  process_->write(line);
  process_->waitForBytesWritten(5000);

  if (!process_->waitForReadyRead(10000)) {
    qWarning("PyCustomFunction: timeout waiting for Python response");
    return;
  }

  QByteArray response = process_->readLine();
  QJsonObject result = QJsonDocument::fromJson(response).object();

  if (result.contains("error")) {
    qWarning("PyCustomFunction: %s", qPrintable(result["error"].toString()));
    return;
  }

  QJsonArray result_arr = result["result"].toArray();
  dst->clear();
  for (size_t i = 0; i < std::min<size_t>(result_arr.size(), main_src->size()); ++i) {
    double y = result_arr[i].toDouble();
    dst->pushBack({main_src->at(i).x, y});
  }
}

bool PyCustomFunction::xmlLoadState(const QDomElement &parent_element) {
  bool ret = CustomFunction::xmlLoadState(parent_element);
  initEngine();
  return ret;
}

}  // namespace cabana::pj_engine
