#pragma once

#include <QMimeData>
#include <QStringList>

#include <vector>

enum class CurveDragMode
{
  AddCurves,
  NewXYAxis,
  Invalid,
};

struct CurveDragPayload
{
  CurveDragMode mode = CurveDragMode::Invalid;
  QStringList curves;

  bool isValid() const
  {
    return mode != CurveDragMode::Invalid && !curves.isEmpty();
  }
};

CurveDragPayload DecodeCurveDragPayload(const QMimeData* mime_data);
QMimeData* EncodeCurveDragPayload(const std::vector<std::string>& curve_names,
                                  CurveDragMode mode);
