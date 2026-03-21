#include <GLFW/glfw3.h>

#ifndef GLFW_PLATFORM_X11
#define GLFW_PLATFORM_X11 0x00060001
#endif

extern "C" __attribute__((weak)) int glfwGetPlatform(void) {
  return GLFW_PLATFORM_X11;
}
