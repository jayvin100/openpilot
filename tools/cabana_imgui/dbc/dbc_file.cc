#include "dbc/dbc_file.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>

namespace cabana {
namespace dbc {

// Signal value decoding — ported from tools/cabana/dbc/dbc.cc (Qt-free)
static inline int flipBitPos(int start_bit) {
  return 8 * (start_bit / 8) + 7 - start_bit % 8;
}

static std::string double_to_string(double value) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.12g", value);
  return buf;
}

double Signal::getValue(const uint8_t *data, int data_size) const {
  int lsb, msb;
  if (is_little_endian) {
    lsb = start_bit;
    msb = start_bit + size - 1;
  } else {
    msb = start_bit;
    lsb = flipBitPos(flipBitPos(start_bit) + size - 1);
  }

  int msb_byte = msb / 8;
  if (msb_byte >= data_size) return 0;
  int lsb_byte = lsb / 8;

  uint64_t val = 0;
  if (msb_byte == lsb_byte) {
    val = (data[msb_byte] >> (lsb & 7)) & ((1ULL << size) - 1);
  } else {
    int bits = size;
    int i = msb_byte;
    int step = is_little_endian ? -1 : 1;
    while (i >= 0 && i < data_size && bits > 0) {
      int m = (i == msb_byte) ? msb & 7 : 7;
      int l = (i == lsb_byte) ? lsb & 7 : 0;
      int nbits = m - l + 1;
      val = (val << nbits) | ((data[i] >> l) & ((1ULL << nbits) - 1));
      bits -= nbits;
      i += step;
    }
  }

  if (is_signed && (val & (1ULL << (size - 1)))) {
    val |= ~((1ULL << size) - 1);
  }
  return static_cast<int64_t>(val) * factor + offset;
}

bool DbcFile::load(const std::string &filename) {
  std::ifstream f(filename);
  if (!f.is_open()) return false;
  std::string content((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  raw_content_ = content;
  filename_ = filename;
  name_ = filename;
  parse(content);
  return true;
}

bool DbcFile::loadFromString(const std::string &content) {
  raw_content_ = content;
  filename_.clear();
  parse(content);
  return true;
}

bool DbcFile::save() const {
  if (filename_.empty()) return false;
  return writeContents(filename_);
}

bool DbcFile::saveAs(const std::string &filename) {
  if (!writeContents(filename)) return false;
  filename_ = filename;
  return true;
}

Message *DbcFile::msgMutable(uint32_t address) {
  auto it = msgs_.find(address);
  return it != msgs_.end() ? &it->second : nullptr;
}

const Message *DbcFile::msg(uint32_t address) const {
  auto it = msgs_.find(address);
  return it != msgs_.end() ? &it->second : nullptr;
}

int DbcFile::signalCount() const {
  int count = 0;
  for (const auto &[_, m] : msgs_) count += (int)m.signals.size();
  return count;
}

bool DbcFile::writeContents(const std::string &filename) const {
  std::ofstream output(filename);
  if (!output.is_open()) return false;
  output << raw_content_;
  return output.good();
}

void DbcFile::updateMessage(uint32_t address, const std::string &name, uint32_t size,
                            const std::string &transmitter, const std::string &comment) {
  auto &msg = msgs_[address];
  msg.address = address;
  msg.name = name;
  msg.size = size;
  msg.transmitter = transmitter.empty() ? "XXX" : transmitter;
  msg.comment = comment;
  markDirty();
}

bool DbcFile::removeMessage(uint32_t address) {
  auto it = msgs_.find(address);
  if (it == msgs_.end()) return false;
  msgs_.erase(it);
  markDirty();
  return true;
}

bool DbcFile::addSignal(uint32_t address, const Signal &signal) {
  auto *msg = msgMutable(address);
  if (!msg) return false;
  if (std::any_of(msg->signals.begin(), msg->signals.end(), [&](const auto &existing) {
        return existing.name == signal.name;
      })) {
    return false;
  }
  msg->signals.push_back(signal);
  markDirty();
  return true;
}

bool DbcFile::updateSignal(uint32_t address, const std::string &original_name, const Signal &signal) {
  auto *msg = msgMutable(address);
  if (!msg) return false;

  auto it = std::find_if(msg->signals.begin(), msg->signals.end(), [&](const auto &existing) {
    return existing.name == original_name;
  });
  if (it == msg->signals.end()) return false;

  if (signal.name != original_name && std::any_of(msg->signals.begin(), msg->signals.end(), [&](const auto &existing) {
        return existing.name == signal.name;
      })) {
    return false;
  }

  *it = signal;
  markDirty();
  return true;
}

bool DbcFile::removeSignal(uint32_t address, const std::string &name) {
  auto *msg = msgMutable(address);
  if (!msg) return false;

  const auto old_size = msg->signals.size();
  msg->signals.erase(std::remove_if(msg->signals.begin(), msg->signals.end(), [&](const auto &signal) {
    return signal.name == name;
  }), msg->signals.end());
  if (msg->signals.size() == old_size) return false;
  markDirty();
  return true;
}

void DbcFile::markDirty() {
  raw_content_ = generateDBC();
}

std::string DbcFile::generateDBC() const {
  std::string dbc;
  std::string comments;

  for (const auto &[address, msg] : msgs_) {
    const std::string transmitter = msg.transmitter.empty() ? "XXX" : msg.transmitter;
    dbc += "BO_ " + std::to_string(address) + " " + msg.name + ": " + std::to_string(msg.size) + " " + transmitter + "\n";
    for (const auto &signal : msg.signals) {
      dbc += " SG_ " + signal.name + " : " + std::to_string(signal.start_bit) + "|" + std::to_string(signal.size) + "@" +
             (signal.is_little_endian ? "1" : "0") + std::string(signal.is_signed ? "-" : "+") +
             " (" + double_to_string(signal.factor) + "," + double_to_string(signal.offset) + ")" +
             " [" + double_to_string(signal.min) + "|" + double_to_string(signal.max) + "]" +
             " \"" + signal.unit + "\" XXX\n";
    }
    dbc += "\n";

    if (!msg.comment.empty()) {
      comments += "CM_ BO_ " + std::to_string(address) + " \"" + msg.comment + "\";\n";
    }
    for (const auto &signal : msg.signals) {
      if (!signal.comment.empty()) {
        comments += "CM_ SG_ " + std::to_string(address) + " " + signal.name + " \"" + signal.comment + "\";\n";
      }
    }
  }

  return header_ + dbc + comments;
}

void DbcFile::parse(const std::string &content) {
  msgs_.clear();
  header_.clear();

  // Regex for BO_ lines: BO_ <id> <name>: <size> <transmitter>
  static const std::regex bo_re(R"re(^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))re");
  // Regex for SG_ lines
  static const std::regex sg_re(R"re(^\s+SG_\s+(\w+)\s+(?:[Mm]\d*\s+)?:\s*(\d+)\|(\d+)@([01])([+-])\s+\(([\d.eE+-]+),([\d.eE+-]+)\)\s+\[([\d.eE+-]+)\|([\d.eE+-]+)\]\s+"([^"]*)"\s+(\w+))re");
  // Regex for CM_ BO_ (message comments)
  static const std::regex cm_bo_re(R"re(^CM_\s+BO_\s+(\d+)\s+"([^"]*)")re");
  // Regex for CM_ SG_ (signal comments)
  static const std::regex cm_sg_re(R"re(^CM_\s+SG_\s+(\d+)\s+(\w+)\s+"([^"]*)")re");

  std::istringstream stream(content);
  std::string line;
  Message *current_msg = nullptr;
  bool seen_first = false;

  while (std::getline(stream, line)) {
    std::smatch match;

    if (std::regex_search(line, match, bo_re)) {
      seen_first = true;
      uint32_t id = std::stoul(match[1]);
      // DBC uses 29-bit extended IDs with bit 31 set for extended
      uint32_t address = id & 0x1FFFFFFF;
      Message m;
      m.address = address;
      m.name = match[2];
      m.size = std::stoul(match[3]);
      m.transmitter = match[4];
      msgs_[address] = std::move(m);
      current_msg = &msgs_[address];
    } else if (current_msg && std::regex_search(line, match, sg_re)) {
      seen_first = true;
      Signal s;
      s.name = match[1];
      s.start_bit = std::stoi(match[2]);
      s.size = std::stoi(match[3]);
      s.is_little_endian = (match[4] == "1");
      s.is_signed = (match[5] == "-");
      s.factor = std::stod(match[6]);
      s.offset = std::stod(match[7]);
      s.min = std::stod(match[8]);
      s.max = std::stod(match[9]);
      s.unit = match[10];
      current_msg->signals.push_back(std::move(s));
    } else if (std::regex_search(line, match, cm_bo_re)) {
      seen_first = true;
      uint32_t id = std::stoul(match[1]) & 0x1FFFFFFF;
      auto it = msgs_.find(id);
      if (it != msgs_.end()) it->second.comment = match[2];
    } else if (std::regex_search(line, match, cm_sg_re)) {
      seen_first = true;
      uint32_t id = std::stoul(match[1]) & 0x1FFFFFFF;
      auto it = msgs_.find(id);
      if (it != msgs_.end()) {
        for (auto &sig : it->second.signals) {
          if (sig.name == match[2].str()) {
            sig.comment = match[3];
            break;
          }
        }
      }
    } else if (line.empty() || line[0] == ' ' || line[0] == '\t') {
      // continuation or blank — skip
      if (!seen_first) {
        header_ += line + "\n";
      }
    } else {
      if (!seen_first) {
        header_ += line + "\n";
      }
      current_msg = nullptr;  // end of BO_ block
    }
  }
}

}  // namespace dbc
}  // namespace cabana
