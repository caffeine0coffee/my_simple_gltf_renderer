#pragma once

// vulkan_raii.hpp を先にインクルードしておくことで、GLFW側の
// vulkan/vulkan.h 二重インクルードを回避しつつ、GLFW_INCLUDE_VULKAN による
// glfwGetRequiredInstanceExtensions() 等のVulkan連携APIを有効化する。
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// GLFWライブラリ初期化・ウィンドウ・Vulkan Surfaceの生成に必要な
// GLFW側リソースのRAIIラッパー。
// GLFWWwindow* は1つのウィンドウを一意に所有するため、コピー・ムーブともに禁止する。
class Window {
 public:
  Window(int width, int height, const char* title);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  [[nodiscard]] bool ShouldClose() const;
  static void PollEvents();

  [[nodiscard]] static std::vector<const char*> GetRequiredInstanceExtensions();
  [[nodiscard]] vk::Extent2D GetFramebufferSize() const;

  // glfwCreateWindowSurface() はRAII非対応のC APIで、出力引数として
  // 生のVkSurfaceKHRを返すため、生ハンドルを扱うのはこの関数内に閉じ込め、
  // 呼び出し側には vk::raii::SurfaceKHR として所有権を返す。
  [[nodiscard]] vk::raii::SurfaceKHR CreateSurface(const vk::raii::Instance& instance) const;

 private:
  GLFWwindow* window_ = nullptr;
};
