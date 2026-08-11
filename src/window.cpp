#include "window.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace {

void GlfwErrorCallback(int error, const char* description) { spdlog::error("GLFW error {}: {}", error, description); }

}  // namespace

Window::Window(int width, int height, const char* title) {
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    throw std::runtime_error("failed to initialize GLFW");
  }

  // Vulkanを使うのでGLFWにOpenGLコンテキストを作らせない。
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (window_ == nullptr) {
    throw std::runtime_error("failed to create GLFW window");
  }
}

Window::~Window() {
  glfwDestroyWindow(window_);
  glfwTerminate();
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(window_); }

void Window::PollEvents() { glfwPollEvents(); }

std::vector<const char*> Window::GetRequiredInstanceExtensions() {
  uint32_t glfw_extension_count = 0;
  const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
  return {glfw_extensions, glfw_extensions + glfw_extension_count};
}

vk::Extent2D Window::GetFramebufferSize() const {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  return vk::Extent2D{
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
  };
}

vk::raii::SurfaceKHR Window::CreateSurface(const vk::raii::Instance& instance) const {
  VkSurfaceKHR raw_surface{};
  if (glfwCreateWindowSurface(*instance, window_, nullptr, &raw_surface) != VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface");
  }
  return {instance, raw_surface};
}
