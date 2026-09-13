#pragma once

// vulkan_raii.hpp を先にインクルードしておくことで、GLFW側の
// vulkan/vulkan.h 二重インクルードを回避しつつ、GLFW_INCLUDE_VULKAN による
// glfwGetRequiredInstanceExtensions() 等のVulkan連携APIを有効化する。
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

/**
 * @brief GLFWライブラリ初期化・ウィンドウ・Vulkan Surfaceの生成に必要なGLFW側リソースのRAIIラッパー。
 *
 * GLFWwindow* は1つのウィンドウを一意に所有するため、コピー・ムーブともに禁止します。
 */
class Window {
 public:
  /**
   * @brief GLFWの初期化とウィンドウの生成を行います。
   *
   * @param width ウィンドウの幅（ピクセル単位）。
   * @param height ウィンドウの高さ（ピクセル単位）。
   * @param title ウィンドウのタイトル文字列。
   * @throw std::runtime_error GLFWの初期化またはウィンドウ生成に失敗した場合。
   */
  Window(int width, int height, const char* title);

  /**
   * @brief ウィンドウを破棄し、GLFWライブラリを終了します。
   */
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  /**
   * @brief ウィンドウが閉じられるべき状態かどうかを取得します。
   *
   * @return ウィンドウを閉じる要求が来ている場合は true、そうでない場合は false。
   */
  [[nodiscard]] bool ShouldClose() const;

  /**
   * @brief 保留中のウィンドウイベント（入力・リサイズ等）を処理します。
   */
  static void PollEvents();

  /**
   * @brief Vulkan Instanceの作成に必要なGLFWの拡張機能名リストを取得します。
   *
   * @return 必要な拡張機能名のリスト。
   */
  [[nodiscard]] static std::vector<const char*> GetRequiredInstanceExtensions();

  /**
   * @brief ウィンドウのフレームバッファサイズ（ピクセル単位）を取得します。
   *
   * @return フレームバッファの解像度 (vk::Extent2D)。
   */
  [[nodiscard]] vk::Extent2D GetFramebufferSize() const;

  /**
   * @brief 指定したVulkan Instanceに対応するウィンドウのSurfaceを生成します。
   *
   * glfwCreateWindowSurface() はRAII非対応のC APIで、出力引数として
   * 生のVkSurfaceKHRを返すため、生ハンドルを扱うのはこの関数内に閉じ込め、
   * 呼び出し側には vk::raii::SurfaceKHR として所有権を返します。
   *
   * @param instance Surfaceを関連付けるVulkan Instance。
   * @return 生成されたSurfaceのRAIIラッパー (vk::raii::SurfaceKHR)。
   * @throw std::runtime_error Surfaceの生成に失敗した場合。
   */
  [[nodiscard]] vk::raii::SurfaceKHR CreateSurface(const vk::raii::Instance& instance) const;

 private:
  GLFWwindow* window_ = nullptr;
};
