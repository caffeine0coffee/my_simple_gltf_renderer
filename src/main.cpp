#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

#include "vulkan_app.h"
#include "window.h"

namespace {
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
}  // namespace

/**
 * @brief アプリケーションのエントリーポイント。
 *
 * GLFWウィンドウおよびVulkanAppの初期化、イベントループの実行、
 * 発生した例外のハンドリングを行います。
 *
 * @return 正常終了時は EXIT_SUCCESS、エラー発生時は EXIT_FAILURE。
 */
int main() {
  try {
    // VulkanApp が保持する Surface はウィンドウハンドルに依存するため、
    // window を app より先に宣言し、スコープ終了時に app (先に破棄)
    // → window (後に破棄) の順になることを保証する。
    Window window(kWindowWidth, kWindowHeight, "glTF Renderer");
    VulkanApp app(window);

    while (!window.ShouldClose()) {
      Window::PollEvents();
      app.DrawFrame();
    }
  } catch (const std::exception& e) {
    spdlog::error("Fatal error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
