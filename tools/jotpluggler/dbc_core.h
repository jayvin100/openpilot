#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace jotpluggler::dbc_core {

struct ValueDescriptionEntry {
  double value = 0.0;
  std::string text;
};

struct Signal {
  enum class Type {
    Normal = 0,
    Multiplexed,
    Multiplexor,
  };

  Type type = Type::Normal;
  std::string name;
  int start_bit = 0;
  int msb = 0;
  int lsb = 0;
  int size = 0;
  double factor = 1.0;
  double offset = 0.0;
  bool is_signed = false;
  bool is_little_endian = false;
  int multiplex_value = 0;
  int multiplexor_index = -1;
  std::vector<ValueDescriptionEntry> value_descriptions;
};

struct Message {
  uint32_t address = 0;
  std::string name;
  uint32_t size = 0;
  std::vector<Signal> signals;
  int multiplexor_index = -1;
};

class Database {
public:
  Database() = default;
  explicit Database(const std::filesystem::path &path);

  const Message *message(uint32_t address) const;
  std::vector<std::string> enum_names(const Signal &signal) const;

private:
  void parse(const std::string &content, const std::string &filename);
  void parse_bo(const std::string &line, int line_number, Message **current_message);
  void parse_sg(const std::string &line, int line_number, Message *current_message);
  void parse_val(const std::string &line, int line_number);
  void finalize();

  std::unordered_map<uint32_t, Message> messages_;
};

std::optional<double> signal_value(const Signal &signal, const Message &message, const uint8_t *data, size_t data_size);

}  // namespace jotpluggler::dbc_core
