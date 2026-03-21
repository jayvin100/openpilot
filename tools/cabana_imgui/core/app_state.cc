#include "core/app_state.h"

namespace cabana {

AppState &app_state() {
  static AppState s;
  return s;
}

}  // namespace cabana
