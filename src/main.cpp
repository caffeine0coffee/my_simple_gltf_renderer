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
    // 現時点ではappに対する操作が無くconst化を提案されるが、
    // 今後のマイルストーンで描画ループ等の非const関数を呼ぶ想定のため、
    // 意図的にnon-constのまま残す。
    VulkanApp app(window);  // NOLINT(misc-const-correctness)

    while (!window.ShouldClose()) {
      Window::PollEvents();
    }
  } catch (const std::exception& e) {
    spdlog::error("Fatal error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
