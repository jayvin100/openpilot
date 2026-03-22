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

  bool save() const;
  bool saveAs(const std::string &filename);

  const std::map<uint32_t, Message> &messages() const { return msgs_; }
  Message *msgMutable(uint32_t address);
  const Message *msg(uint32_t address) const;
  int signalCount() const;
  const std::string &filename() const { return filename_; }

  void updateMessage(uint32_t address, const std::string &name, uint32_t size,
                     const std::string &transmitter, const std::string &comment);
  bool removeMessage(uint32_t address);
  bool addSignal(uint32_t address, const Signal &signal);
  bool updateSignal(uint32_t address, const std::string &original_name, const Signal &signal);
  bool removeSignal(uint32_t address, const std::string &name);

private:
  void parse(const std::string &content);
  bool writeContents(const std::string &filename) const;
  void markDirty();
  std::string generateDBC() const;

  std::map<uint32_t, Message> msgs_;
  std::string header_;
  std::string name_;
  std::string filename_;
  std::string raw_content_;
};

}  // namespace dbc
}  // namespace cabana
