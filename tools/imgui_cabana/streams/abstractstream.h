#pragma once

#include <string>

namespace imgui_cabana {

class AbstractStream {
 public:
  virtual ~AbstractStream() = default;
  virtual std::string streamType() const = 0;
};

}  // namespace imgui_cabana
