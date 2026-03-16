#pragma once

#include "tools/imgui_cabana/streams/abstractstream.h"

namespace imgui_cabana {

class PandaStream : public AbstractStream {
 public:
  std::string streamType() const override { return "panda"; }
};

}  // namespace imgui_cabana
