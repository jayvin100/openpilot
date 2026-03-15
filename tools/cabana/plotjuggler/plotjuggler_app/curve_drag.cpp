#include "curve_drag.h"

#include <QDataStream>

namespace {

QString MimeTypeForMode(CurveDragMode mode)
{
  switch (mode)
  {
    case CurveDragMode::AddCurves:
      return "curveslist/add_curve";
    case CurveDragMode::NewXYAxis:
      return "curveslist/new_XY_axis";
    case CurveDragMode::Invalid:
      break;
  }
  return {};
}

CurveDragMode ModeForMimeType(const QString& mime_type)
{
  if (mime_type == "curveslist/add_curve")
  {
    return CurveDragMode::AddCurves;
  }
  if (mime_type == "curveslist/new_XY_axis")
  {
    return CurveDragMode::NewXYAxis;
  }
  return CurveDragMode::Invalid;
}

}  // namespace

CurveDragPayload DecodeCurveDragPayload(const QMimeData* mime_data)
{
  if (!mime_data)
  {
    return {};
  }

  for (const auto& format : mime_data->formats())
  {
    CurveDragMode mode = ModeForMimeType(format);
    if (mode == CurveDragMode::Invalid)
    {
      continue;
    }

    CurveDragPayload payload;
    payload.mode = mode;

    QByteArray encoded = mime_data->data(format);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    while (!stream.atEnd())
    {
      QString curve_name;
      stream >> curve_name;
      if (!curve_name.isEmpty())
      {
        payload.curves.push_back(curve_name);
      }
    }
    return payload;
  }

  return {};
}

QMimeData* EncodeCurveDragPayload(const std::vector<std::string>& curve_names, CurveDragMode mode)
{
  const QString mime_type = MimeTypeForMode(mode);
  if (mime_type.isEmpty())
  {
    return nullptr;
  }

  QByteArray payload_bytes;
  QDataStream stream(&payload_bytes, QIODevice::WriteOnly);
  for (const auto& curve_name : curve_names)
  {
    stream << QString::fromStdString(curve_name);
  }

  auto* mime_data = new QMimeData;
  mime_data->setData(mime_type, payload_bytes);
  return mime_data;
}
