#pragma once

#include <QProcess>
#include <QString>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "PlotJuggler/plotdata.h"
#include "plotjuggler_app/transforms/custom_function.h"

namespace cabana::pj_engine {

/// Custom math transform that uses Python instead of Lua.
/// Spawns a Python subprocess and communicates via JSON over stdin/stdout.
class PyCustomFunction : public CustomFunction {
public:
  PyCustomFunction(SnippetData snippet);
  ~PyCustomFunction() override;

  const char *name() const override { return "PyCustomFunction"; }
  QString language() const override { return "Python"; }
  void initEngine() override;

  void calculate() override;
  void calculatePoints(const std::vector<const PJ::PlotData *> &channels,
                       size_t point_index,
                       std::vector<PJ::PlotData::Point> &points) override;

  bool xmlLoadState(const QDomElement &parent_element) override;

private:
  void batchCalculate(const std::vector<const PJ::PlotData *> &channels,
                      PJ::PlotData *dst);

  std::unique_ptr<QProcess> process_;
  std::mutex mutex_;
  bool batch_done_ = false;
};

}  // namespace cabana::pj_engine
