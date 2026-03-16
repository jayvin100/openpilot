#include "tools/imgui_cabana/app_state.h"
#include "tools/imgui_cabana/mainwin.h"

int main(int argc, char *argv[]) {
  return imgui_cabana::runMainWindow(imgui_cabana::parseArgs(argc, argv));
}
