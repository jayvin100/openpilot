#pragma once

#include "tools/imgui_cabana/streams/abstractstream.h"

namespace imgui_cabana {

class DeviceStream : public AbstractStream {
 public:
  std::string streamType() const override { return "device"; }
};

}  // namespace imgui_cabana
