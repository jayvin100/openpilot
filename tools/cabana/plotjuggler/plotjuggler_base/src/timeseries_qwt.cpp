/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "timeseries_qwt.h"
#include <limits>
#include <stdexcept>
#include <QMessageBox>
#include <QPushButton>
#include <QString>

#include <algorithm>

RangeOpt QwtSeriesWrapper::getVisualizationRangeY(Range range_x)
{
  if (_snapshot && _snapshot->range_y &&
      range_x.min <= std::numeric_limits<double>::lowest() &&
      range_x.min <= std::numeric_limits<double>::max())
  {
    return _snapshot->range_y;
  }

  double min_y = (std::numeric_limits<double>::max());
  double max_y = (std::numeric_limits<double>::lowest());

  for (size_t i = 0; i < size(); i++)
  {
    const double Y = sample(i).y();
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }
  return Range{ min_y, max_y };
}

RangeOpt QwtTimeseries::getVisualizationRangeY(Range range_X)
{
  if (!_snapshot || _snapshot->points.empty())
  {
    return {};
  }

  const double raw_min = range_X.min + _time_offset;
  const double raw_max = range_X.max + _time_offset;
  auto first = std::lower_bound(_snapshot->points.begin(), _snapshot->points.end(), raw_min,
                                [](const QPointF& point, double value) { return point.x() < value; });
  auto last = std::upper_bound(_snapshot->points.begin(), _snapshot->points.end(), raw_max,
                               [](double value, const QPointF& point) { return value < point.x(); });

  if (first == _snapshot->points.end() || first >= last)
  {
    return {};
  }

  if (first == _snapshot->points.begin() && last == _snapshot->points.end())
  {
    return _snapshot->range_y;
  }

  double min_y = (std::numeric_limits<double>::max());
  double max_y = (std::numeric_limits<double>::lowest());

  for (auto it = first; it != last; ++it)
  {
    const double Y = it->y();
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }
  return Range{ min_y, max_y };
}

std::optional<QPointF> QwtTimeseries::sampleFromTime(double t)
{
  if (!_snapshot || _snapshot->points.empty())
  {
    return {};
  }

  auto lower = std::lower_bound(_snapshot->points.begin(), _snapshot->points.end(), t,
                                [](const QPointF& point, double value) { return point.x() < value; });
  if (lower == _snapshot->points.end())
  {
    lower = std::prev(_snapshot->points.end());
  }
  else if (lower != _snapshot->points.begin())
  {
    auto prev = std::prev(lower);
    if (std::abs(prev->x() - t) < std::abs(lower->x() - t))
    {
      lower = prev;
    }
  }
  return QPointF(lower->x() - _time_offset, lower->y());
}

TransformedTimeseries::TransformedTimeseries(const PlotData* source_data,
                                             cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup)
  : QwtTimeseries(&_dst_data, std::move(snapshot_lookup))
  , _dst_data(source_data->plotName(), {})
  , _src_data(source_data)
{
}

TransformFunction::Ptr TransformedTimeseries::transform()
{
  return _transform;
}

void TransformedTimeseries::setTransform(QString transform_ID)
{
  if (transformName() == transform_ID)
  {
    return;
  }
  if (transform_ID.isEmpty())
  {
    _transform.reset();
  }
  else
  {
    _dst_data.clear();
    _transform = TransformFactory::create(transform_ID.toStdString());
    if (!_transform)
    {
      qWarning() << "Unknown transform:" << transform_ID;
      return;
    }
    std::vector<PlotData*> dest = { &_dst_data };
    _transform->setData(nullptr, { _src_data }, dest);
  }
}

void TransformedTimeseries::updateCache(bool reset_old_data)
{
  if (_transform)
  {
    if (reset_old_data)
    {
      _dst_data.clear();
      _transform->reset();
    }
    std::vector<PlotData*> dest = { &_dst_data };
    _transform->calculate();
  }
  else
  {
    if (_snapshot_lookup)
    {
      auto cached_snapshot = _snapshot_lookup(_src_data);
      if (cached_snapshot)
      {
        setSnapshot(std::move(cached_snapshot));
        return;
      }
    }

    // TODO: optimize ??
    _dst_data.clear();
    for (size_t i = 0; i < _src_data->size(); i++)
    {
      _dst_data.pushBack(_src_data->at(i));
    }
  }
  setSnapshot(cabana::pj_engine::BuildSeriesSnapshot(_dst_data));
}

QString TransformedTimeseries::transformName()
{
  return (!_transform) ? QString() : _transform->name();
}

QString TransformedTimeseries::alias() const
{
  return _alias;
}

void TransformedTimeseries::setAlias(QString alias)
{
  _alias = alias;
}

const PlotDataXY* TransformedTimeseries::plotData() const
{
  return _transform ? &_dst_data : _src_data;
}

QRectF QwtSeriesWrapper::boundingRect() const
{
  if (!_snapshot || size() == 0 || !_snapshot->range_x || !_snapshot->range_y)
  {
    return {};
  }
  auto range_x = _snapshot->range_x.value();
  auto range_y = _snapshot->range_y.value();

  QRectF box;
  box.setLeft(range_x.min);
  box.setRight(range_x.max);
  box.setTop(range_y.max);
  box.setBottom(range_y.min);
  return box;
}

QRectF QwtTimeseries::boundingRect() const
{
  if (!_snapshot || size() == 0 || !_snapshot->range_x || !_snapshot->range_y)
  {
    return {};
  }
  auto range_x = _snapshot->range_x.value();
  auto range_y = _snapshot->range_y.value();

  QRectF box;
  box.setLeft(range_x.min - _time_offset);
  box.setRight(range_x.max - _time_offset);
  box.setTop(range_y.max);
  box.setBottom(range_y.min);
  return box;
}

QPointF QwtSeriesWrapper::sample(size_t i) const
{
  if (_snapshot && i < _snapshot->points.size())
  {
    return _snapshot->points[i];
  }
  const auto& p = _data->at(i);
  return QPointF(p.x, p.y);
}

QPointF QwtTimeseries::sample(size_t i) const
{
  if (_snapshot && i < _snapshot->points.size())
  {
    const auto& p = _snapshot->points[i];
    return QPointF(p.x() - _time_offset, p.y());
  }
  const auto& p = _ts_data->at(i);
  return QPointF(p.x - _time_offset, p.y);
}

size_t QwtSeriesWrapper::size() const
{
  if (_snapshot)
  {
    return _snapshot->points.size();
  }
  return _data->size();
}

void QwtSeriesWrapper::updateCache(bool)
{
  if (_snapshot_lookup)
  {
    if (auto cached_snapshot = _snapshot_lookup(plotData()))
    {
      _snapshot = std::move(cached_snapshot);
      return;
    }
  }
  if (_data)
  {
    _snapshot = cabana::pj_engine::BuildSeriesSnapshot(*_data);
  }
}

void QwtTimeseries::updateCache(bool)
{
  if (_snapshot_lookup)
  {
    if (auto cached_snapshot = _snapshot_lookup(_ts_data))
    {
      _snapshot = std::move(cached_snapshot);
      return;
    }
  }
  if (_ts_data)
  {
    _snapshot = cabana::pj_engine::BuildSeriesSnapshot(*_ts_data);
  }
}

void QwtTimeseries::setTimeOffset(double offset)
{
  _time_offset = offset;
}

RangeOpt QwtSeriesWrapper::getVisualizationRangeX()
{
  if (!_snapshot || this->size() < 2)
  {
    return {};
  }
  return _snapshot->range_x;
}

RangeOpt QwtTimeseries::getVisualizationRangeX()
{
  if (!_snapshot || this->size() < 2 || !_snapshot->range_x)
  {
    return {};
  }
  auto range = _snapshot->range_x.value();
  return RangeOpt({ range.min - _time_offset, range.max - _time_offset });
}

const PlotDataBase<double, double>* QwtSeriesWrapper::plotData() const
{
  return _data;
}

void QwtSeriesWrapper::setSnapshot(cabana::pj_engine::SeriesSnapshotPtr snapshot)
{
  _snapshot = std::move(snapshot);
}

const cabana::pj_engine::SeriesSnapshotPtr& QwtSeriesWrapper::snapshot() const
{
  return _snapshot;
}
