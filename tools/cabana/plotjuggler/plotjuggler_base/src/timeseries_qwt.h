/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef TIMESERIES_QWT_H
#define TIMESERIES_QWT_H

#include "qwt_series_data.h"
#include "PlotJuggler/plotdata.h"
#include "PlotJuggler/transform_function.h"
#include "tools/cabana/pj_engine/series_snapshot.h"

using namespace PJ;

// wrapper to Timeseries inclduing a time offset
class QwtSeriesWrapper : public QwtSeriesData<QPointF>
{
protected:
  const PlotDataXY* _data;
  cabana::pj_engine::SeriesSnapshotPtr _snapshot;
  cabana::pj_engine::SeriesSnapshotLookup _snapshot_lookup;

public:
  QwtSeriesWrapper(const PlotDataXY* data,
                   cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup = {})
    : _data(data), _snapshot_lookup(std::move(snapshot_lookup))
  {
  }

  QPointF sample(size_t i) const override;

  size_t size() const override;

  QRectF boundingRect() const override;

  virtual const PlotDataXY* plotData() const;

  virtual RangeOpt getVisualizationRangeX();

  virtual RangeOpt getVisualizationRangeY(Range range_X);

  virtual void updateCache(bool reset_old_data);

protected:
  void setSnapshot(cabana::pj_engine::SeriesSnapshotPtr snapshot);
  const cabana::pj_engine::SeriesSnapshotPtr& snapshot() const;
};

class QwtTimeseries : public QwtSeriesWrapper
{
public:
  QwtTimeseries(const PlotData* data,
                cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup = {})
    : QwtSeriesWrapper(data, std::move(snapshot_lookup)), _ts_data(data)
  {
  }

  QPointF sample(size_t i) const override;

  QRectF boundingRect() const override;

  void setTimeOffset(double offset);

  virtual RangeOpt getVisualizationRangeX() override;

  virtual RangeOpt getVisualizationRangeY(Range range_X) override;

  virtual std::optional<QPointF> sampleFromTime(double t);

  void updateCache(bool) override;

protected:
  const PlotData* _ts_data;
  double _time_offset = 0.0;
};

//------------------------------------

class TransformedTimeseries : public QwtTimeseries
{
public:
  TransformedTimeseries(const PlotData* source_data,
                       cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup = {});

  TransformFunction::Ptr transform();

  void setTransform(QString transform_ID);

  virtual void updateCache(bool reset_old_data) override;

  QString transformName();

  QString alias() const;

  void setAlias(QString alias);

  const PlotDataXY* plotData() const override;

protected:
  QString _alias;
  PlotData _dst_data;
  const PlotData* _src_data;
  TransformFunction_SISO::Ptr _transform;
};

//---------------------------------------------------------

#endif  // PLOTDATA_H
