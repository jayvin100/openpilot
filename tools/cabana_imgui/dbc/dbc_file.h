#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cabana {
namespace dbc {

struct Signal {
  std::string name;
  int start_bit = 0;
  int size = 0;
  bool is_little_endian = true;
  bool is_signed = false;
  double factor = 1.0;
  double offset = 0.0;
  double min = 0.0;
  double max = 0.0;
  std::string unit;
  std::string comment;

  // Decode value from raw bytes
  double getValue(const uint8_t *data, int data_size) const;
};

struct Message {
  uint32_t address = 0;
  std::string name;
  uint32_t size = 0;
  std::string transmitter;
  std::string comment;
  std::vector<Signal> signals;
};

class DbcFile {
public:
  // Load from file path
  bool load(const std::string &filename);

  // Load from string content
  bool loadFromString(const std::string &content);

  const std::map<uint32_t, Message> &messages() const { return msgs_; }
  const Message *msg(uint32_t address) const;
  int signalCount() const;

private:
  void parse(const std::string &content);

  std::map<uint32_t, Message> msgs_;
  std::string name_;
};

}  // namespace dbc
}  // namespace cabana
