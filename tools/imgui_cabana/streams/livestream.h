#pragma once

#include "tools/imgui_cabana/streams/abstractstream.h"

namespace imgui_cabana {

class LiveStream : public AbstractStream {
 public:
  std::string streamType() const override { return "live"; }
};

}  // namespace imgui_cabana
