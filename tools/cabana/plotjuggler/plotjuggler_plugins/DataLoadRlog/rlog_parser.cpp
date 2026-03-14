#include "rlog_parser.hpp"

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include <QDir>
#include <QString>

RlogMessageParser::RlogMessageParser(const std::string& topic_name, PJ::PlotDataMapRef& plot_data) :
  MessageParser(topic_name, plot_data)
{
  show_deprecated = std::getenv("SHOW_DEPRECATED");

  // load schema
  QString schema_path(std::getenv("BASEDIR"));

  if (schema_path.isNull())
  {
    schema_path = QDir(getpwuid(getuid())->pw_dir).filePath("openpilot"); // fallback to $HOME/openpilot
  }
  schema_path = QDir(schema_path).filePath("cereal/log.capnp");
  qDebug() << "Loading schema:" << schema_path;
  schema_path.remove(0, 1);

  // Parse the schema
  auto fs = kj::newDiskFilesystem();
  capnp::SchemaParser *schema_parser = new capnp::SchemaParser;
  this->schema = schema_parser->parseFromDirectory(fs->getRoot(), kj::Path::parse(schema_path.toStdString()), nullptr);
  this->event_struct_schema = schema.getNested("Event").asStruct();
}

capnp::StructSchema RlogMessageParser::getSchema()
{
  return event_struct_schema;
}

bool RlogMessageParser::parseMessageCereal(capnp::DynamicStruct::Reader event)
{
  uint64_t last_nanos = event.get("logMonoTime").as<uint64_t>();
  double time_stamp = (double)last_nanos / 1e9;
  return parseMessageImpl("", event, time_stamp, last_nanos, true);
}

bool RlogMessageParser::parseMessageImpl(const std::string& topic_name, capnp::DynamicValue::Reader value, double time_stamp, uint64_t last_nanos, bool is_root)
{
  PJ::PlotData& _data_series = getSeries(topic_name);

  switch (value.getType())
  {
    case capnp::DynamicValue::BOOL:
    {
      _data_series.pushBack({time_stamp, (double)value.as<bool>()});
      break;
    }

    case capnp::DynamicValue::INT:
    {
      _data_series.pushBack({time_stamp, (double)value.as<int64_t>()});
      break;
    }

    case capnp::DynamicValue::UINT:
    {
      _data_series.pushBack({time_stamp, (double)value.as<uint64_t>()});
      break;
    }

    case capnp::DynamicValue::FLOAT:
    {
      _data_series.pushBack({time_stamp, (double)value.as<double>()});
      break;
    }

    case capnp::DynamicValue::LIST:
    {
      int i = 0;
      for(auto element : value.as<capnp::DynamicList>())
      {
        parseMessageImpl(topic_name + '/' + std::to_string(i), element, time_stamp, last_nanos, false);
        i++;
      }
      break;
    }

    case capnp::DynamicValue::ENUM:
    {
      auto enumValue = value.as<capnp::DynamicEnum>();
      _data_series.pushBack({time_stamp, (double)enumValue.getRaw()});
      break;
    }

    case capnp::DynamicValue::STRUCT:
    {
      auto structValue = value.as<capnp::DynamicStruct>();
      std::string structName;
      KJ_IF_MAYBE(e_, structValue.which())
      {
        structName = e_->getProto().getName();
      }
      // skips root structs that are deprecated
      if (!show_deprecated && structName.find("DEPRECATED") != std::string::npos)
      {
        break;
      }

      const int offset = structValue.getSchema().getProto().getStruct().getDiscriminantCount();
      for (const auto &field : structValue.getSchema().getFields())
      {
        std::string name = field.getProto().getName();
        if (structValue.has(field) && (show_deprecated || name.find("DEPRECATED") == std::string::npos))
        {
          // field is in a union if discriminant is less than the size of the union
          // https://github.com/capnproto/capnproto/blob/master/c++/src/capnp/schema.capnp
          const bool in_union = field.getProto().getDiscriminantValue() < offset;

          if (!is_root || in_union)
          {
            parseMessageImpl(topic_name + '/' + name, structValue.get(field), time_stamp, last_nanos, false);
          }
          else if (is_root && !in_union)
          {
            parseMessageImpl(topic_name + '/' + structName + "/__" + name, structValue.get(field), time_stamp, last_nanos, false);
            if (name == "logMonoTime") {
              parseMessageImpl(topic_name + '/' + structName + "/__logMonoTimeSeconds", ((double)structValue.get(field).as<double>())*1e-9, time_stamp, last_nanos, false);
            }
          }
        }
      }
      break;
    }

    default:
    {
      // We currently don't support: DATA, ANY_POINTER, TEXT, CAPABILITIES, VOID
      break;
    }
  }
  return true;
}
