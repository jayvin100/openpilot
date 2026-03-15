#pragma once

#include <QResizeEvent>
#include <QWidget>
#include <memory>

#include "tools/replay/seg_mgr.h"

class CabanaPlotJugglerWidget : public QWidget {
  Q_OBJECT

public:
  explicit CabanaPlotJugglerWidget(const QString &dbc_name, const QString &layout_path, QWidget *parent = nullptr);
  ~CabanaPlotJugglerWidget() override;

  void clearData();
  void appendSegments(const SegmentMap &segments, uint64_t route_start_nanos);
  void setCurrentTime(double relative_sec);
  void setPlaybackPaused(bool paused);
  QString perfSummary() const;
  Q_INVOKABLE void emitCaptureReady();

private:
  void resizeEvent(QResizeEvent *event) override;

  class Impl;
  std::unique_ptr<Impl> impl_;

signals:
  void captureReady();
};
