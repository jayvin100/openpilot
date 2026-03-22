#include "tools/cabana/dbc/dbcfile.h"

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <sstream>

#ifdef signals
#pragma push_macro("signals")
#undef signals
#define CABANA_RESTORE_SIGNALS_MACRO
#endif
#include "tools/cabana/dbc/dbc_core.h"
#ifdef CABANA_RESTORE_SIGNALS_MACRO
#pragma pop_macro("signals")
#undef CABANA_RESTORE_SIGNALS_MACRO
#endif

DBCFile::DBCFile(const std::string &dbc_file_name) {
  QFile file(QString::fromStdString(dbc_file_name));
  if (file.open(QIODevice::ReadOnly)) {
    name_ = QFileInfo(QString::fromStdString(dbc_file_name)).baseName().toStdString();
    filename = dbc_file_name;
    parse(file.readAll());
  } else {
    throw std::runtime_error("Failed to open file.");
  }
}

DBCFile::DBCFile(const std::string &name, const std::string &content) : name_(name), filename("") {
  parse(QString::fromStdString(content));
}

bool DBCFile::save() {
  assert(!filename.empty());
  return writeContents(filename);
}

bool DBCFile::saveAs(const std::string &new_filename) {
  filename = new_filename;
  return save();
}

bool DBCFile::writeContents(const std::string &fn) {
  QFile file(QString::fromStdString(fn));
  if (file.open(QIODevice::WriteOnly)) {
    std::string content = generateDBC();
    return file.write(content.c_str(), content.size()) >= 0;
  }
  return false;
}

void DBCFile::updateMsg(const MessageId &id, const std::string &name, uint32_t size, const std::string &node, const std::string &comment) {
  auto &m = msgs[id.address];
  m.address = id.address;
  m.name = name;
  m.size = size;
  m.transmitter = node.empty() ? DEFAULT_NODE_NAME : node;
  m.comment = comment;
}

cabana::Msg *DBCFile::msg(uint32_t address) {
  auto it = msgs.find(address);
  return it != msgs.end() ? &it->second : nullptr;
}

cabana::Msg *DBCFile::msg(const std::string &name) {
  auto it = std::find_if(msgs.begin(), msgs.end(), [&name](auto &m) { return m.second.name == name; });
  return it != msgs.end() ? &(it->second) : nullptr;
}

cabana::Signal *DBCFile::signal(uint32_t address, const std::string &name) {
  auto m = msg(address);
  return m ? (cabana::Signal *)m->sig(name) : nullptr;
}

void DBCFile::parse(const QString &content) {
  msgs.clear();

  bool seen_first = false;
  std::istringstream stream(content.toStdString());
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const std::string line = QString::fromStdString(raw_line).trimmed().toStdString();
    const bool seen = line.rfind("BO_ ", 0) == 0 ||
                      line.rfind("SG_ ", 0) == 0 ||
                      line.rfind("VAL_ ", 0) == 0 ||
                      line.rfind("CM_ BO_", 0) == 0 ||
                      line.rfind("CM_ SG_", 0) == 0;
    if (seen) {
      seen_first = true;
    } else if (!seen_first) {
      header += raw_line + "\n";
    }
  }

  const std::string source_name = filename.empty() ? name() : filename;
  const dbc::Database parsed = dbc::Database::fromContent(content.toStdString(), source_name);
  for (const auto &[address, parsed_msg] : parsed.messages()) {
    cabana::Msg &msg = msgs[address];
    msg.address = parsed_msg.address;
    msg.name = parsed_msg.name;
    msg.size = parsed_msg.size;
    msg.comment = parsed_msg.comment;
    msg.transmitter = parsed_msg.transmitter;
    for (const dbc::Signal &parsed_sig : parsed_msg.getSignals()) {
      cabana::Signal sig;
      sig.type = static_cast<cabana::Signal::Type>(parsed_sig.type);
      sig.name = parsed_sig.name;
      sig.start_bit = parsed_sig.start_bit;
      sig.msb = parsed_sig.msb;
      sig.lsb = parsed_sig.lsb;
      sig.size = parsed_sig.size;
      sig.factor = parsed_sig.factor;
      sig.offset = parsed_sig.offset;
      sig.is_signed = parsed_sig.is_signed;
      sig.is_little_endian = parsed_sig.is_little_endian;
      sig.min = parsed_sig.min;
      sig.max = parsed_sig.max;
      sig.unit = parsed_sig.unit;
      sig.comment = parsed_sig.comment;
      sig.receiver_name = parsed_sig.receiver_name;
      sig.multiplex_value = parsed_sig.multiplex_value;
      for (const auto &entry : parsed_sig.value_descriptions) {
        sig.val_desc.push_back({entry.value, entry.text});
      }
      msg.sigs.push_back(new cabana::Signal(sig));
    }
    msg.update();
  }
}

std::string DBCFile::generateDBC() {
  std::string dbc_string, comment, val_desc;
  for (const auto &[address, m] : msgs) {
    const std::string &transmitter = m.transmitter.empty() ? DEFAULT_NODE_NAME : m.transmitter;
    dbc_string += "BO_ " + std::to_string(address) + " " + m.name + ": " + std::to_string(m.size) + " " + transmitter + "\n";
    if (!m.comment.empty()) {
      std::string escaped_comment = m.comment;
      // Replace " with \"
      for (size_t pos = 0; (pos = escaped_comment.find('"', pos)) != std::string::npos; pos += 2)
        escaped_comment.replace(pos, 1, "\\\"");
      comment += "CM_ BO_ " + std::to_string(address) + " \"" + escaped_comment + "\";\n";
    }
    for (auto sig : m.getSignals()) {
      std::string multiplexer_indicator;
      if (sig->type == cabana::Signal::Type::Multiplexor) {
        multiplexer_indicator = "M ";
      } else if (sig->type == cabana::Signal::Type::Multiplexed) {
        multiplexer_indicator = "m" + std::to_string(sig->multiplex_value) + " ";
      }
      const std::string &recv = sig->receiver_name.empty() ? DEFAULT_NODE_NAME : sig->receiver_name;
      dbc_string += " SG_ " + sig->name + " " + multiplexer_indicator + ": " +
                    std::to_string(sig->start_bit) + "|" + std::to_string(sig->size) + "@" +
                    std::string(1, sig->is_little_endian ? '1' : '0') +
                    std::string(1, sig->is_signed ? '-' : '+') +
                    " (" + doubleToString(sig->factor) + "," + doubleToString(sig->offset) + ")" +
                    " [" + doubleToString(sig->min) + "|" + doubleToString(sig->max) + "]" +
                    " \"" + sig->unit + "\" " + recv + "\n";
      if (!sig->comment.empty()) {
        std::string escaped_comment = sig->comment;
        for (size_t pos = 0; (pos = escaped_comment.find('"', pos)) != std::string::npos; pos += 2)
          escaped_comment.replace(pos, 1, "\\\"");
        comment += "CM_ SG_ " + std::to_string(address) + " " + sig->name + " \"" + escaped_comment + "\";\n";
      }
      if (!sig->val_desc.empty()) {
        std::string text;
        for (auto &[val, desc] : sig->val_desc) {
          if (!text.empty()) text += " ";
          char val_buf[64];
          snprintf(val_buf, sizeof(val_buf), "%g", val);
          text += std::string(val_buf) + " \"" + desc + "\"";
        }
        val_desc += "VAL_ " + std::to_string(address) + " " + sig->name + " " + text + ";\n";
      }
    }
    dbc_string += "\n";
  }
  return header + dbc_string + comment + val_desc;
}
