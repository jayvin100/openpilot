#include "dbc/dbc_file.h"

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
  name_ = filename;
  parse(content);
  return true;
}

bool DbcFile::loadFromString(const std::string &content) {
  parse(content);
  return true;
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

void DbcFile::parse(const std::string &content) {
  msgs_.clear();

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

  while (std::getline(stream, line)) {
    std::smatch match;

    if (std::regex_search(line, match, bo_re)) {
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
      uint32_t id = std::stoul(match[1]) & 0x1FFFFFFF;
      auto it = msgs_.find(id);
      if (it != msgs_.end()) it->second.comment = match[2];
    } else if (std::regex_search(line, match, cm_sg_re)) {
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
    } else {
      current_msg = nullptr;  // end of BO_ block
    }
  }
}

}  // namespace dbc
}  // namespace cabana
