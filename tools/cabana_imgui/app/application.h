#pragma once

struct GLFWwindow;

class Application {
public:
  bool init(int argc, char *argv[]);
  int run();
  void shutdown();

private:
  GLFWwindow *window = nullptr;
};
