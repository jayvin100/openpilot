#pragma once
#include <PlotJuggler/messageparser_base.h>
#include <QInputDialog>
#include <QDebug>

#ifndef DYNAMIC_CAPNP
#define DYNAMIC_CAPNP  // Do not depend on generated log.capnp.h structure
#endif

#include <capnp/schema-parser.h>
#include <capnp/dynamic.h>
#include <capnp/serialize.h>

using namespace PJ;

class RlogMessageParser : MessageParser
{
private:
  bool show_deprecated;
  capnp::ParsedSchema schema;
  capnp::StructSchema event_struct_schema;

public:
  RlogMessageParser(const std::string& topic_name, PJ::PlotDataMapRef& plot_data);

  capnp::StructSchema getSchema();
  bool parseMessageCereal(capnp::DynamicStruct::Reader event);
  bool parseMessageImpl(const std::string& topic_name, capnp::DynamicValue::Reader node, double timestamp, uint64_t last_sec, bool is_root);
  bool parseMessage(const MessageRef serialized_msg, double &timestamp) { return false; };  // not implemented
};
