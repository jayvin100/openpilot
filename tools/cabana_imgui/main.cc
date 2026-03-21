#include "app/application.h"

int main(int argc, char *argv[]) {
  Application app;
  if (!app.init(argc, argv)) {
    return 1;
  }
  return app.run();
}
