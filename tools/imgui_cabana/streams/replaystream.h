#pragma once

#include "tools/imgui_cabana/streams/abstractstream.h"

namespace imgui_cabana {

class ReplayStream : public AbstractStream {
 public:
  std::string streamType() const override { return "replay"; }
};

}  // namespace imgui_cabana
