#pragma once

#include "tools/imgui_cabana/streams/abstractstream.h"

namespace imgui_cabana {

class SocketCanStream : public AbstractStream {
 public:
  std::string streamType() const override { return "socketcan"; }
};

}  // namespace imgui_cabana
