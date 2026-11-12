# ウィンドウリサイズ時のSwapchain再生成の実装

PROGRESS.md のタスク「ウィンドウリサイズ時のSwapchain再生成」を実装しました。

## 実装内容の概要

- **GLFWによるウィンドウリサイズ検知とイベント待機**（src/window.h, src/window.cpp）
  - `glfwSetWindowUserPointer` と `glfwSetFramebufferSizeCallback` を用いて、フレームバッファのサイズ変更イベントを検知するコールバック `Window::FramebufferResizeCallback()` を実装しました。
  - ウィンドウ最小化時にビジーループを回避してイベント到着まで待機する `Window::WaitEvents()` を実装しました。
- **Swapchainおよび関連リソースの破棄・再生成**（src/vulkan_app.h, src/vulkan_app.cpp）
  - 古いリソースを依存関係の逆順（Framebuffer -> ImageView -> Swapchain）で明示的に破棄する `VulkanApp::CleanupSwapchain()` を実装しました。
  - 最小化時（0x0）の復元待機、`device_.waitIdle()` によるGPU完了待機、古いリソースの破棄、および `Swapchain` / `ImageView` / `Framebuffer` / `renderFinished` セマフォの再生成を行う `VulkanApp::RecreateSwapchain()` を実装しました。
- **描画ループ `DrawFrame()` における再生成トリガーの組み込み**（src/vulkan_app.cpp）
  - `acquireNextImage` および `presentKHR` における `vk::OutOfDateKHRError`（`VK_ERROR_OUT_OF_DATE_KHR`）と `vk::Result::eSuboptimalKHR`（`VK_SUBOPTIMAL_KHR`）、および `window_.IsFramebufferResized()` フラグを検知して `RecreateSwapchain()` を呼び出すようにしました。

---

## Vulkanにおける役割や仕組みの解説

### 1. なぜウィンドウリサイズ時にSwapchain再生成が必要なのか

VulkanのSwapchain（`vk::raii::SwapchainKHR`）は、作成時点のウィンドウ（Surface）解像度（`imageExtent`）で固定された描画用画像群を保持します。
ユーザーがウィンドウをリサイズすると、ウィンドウのフレームバッファサイズとSwapchain画像のサイズが不一致になります。このときVulkanドライバは以下のステータスを返します。

| ステータス / 例外 | 意味 | 対応方針 |
|---|---|---|
| `VK_ERROR_OUT_OF_DATE_KHR` (`vk::OutOfDateKHRError`) | SurfaceとSwapchainの互換性が失われ、描画・表示に使用不可能 | 即座にSwapchainを再生成する |
| `VK_SUBOPTIMAL_KHR` (`vk::Result::eSuboptimalKHR`) | 画面表示は可能だが、サイズが一致しない等で最適ではない状態 | 現フレームのPresentまで完了させた後にSwapchainを再生成する |
| GLFWのリサイズコールバック (`framebuffer_resized_`) | OS・ウィンドウシステム側でフレームバッファサイズが変化した通知 | ドライバが上記コードを返さないプラットフォームでも確実に再生成する |

### 2. Swapchain再生成の対象リソースと依存関係

Swapchainを作り直す際、Swapchainのサイズや画像に依存するリソースもあわせて作り直す必要があります。一方、動的状態（Dynamic State）としてビューポート・シザーを設定済みのGraphics PipelineやRenderPassは再生成不要です。

```
+----------------------------------------------+
|       Swapchain再生成時のリソース関係        |
+----------------------------------------------+
  [再生成不要]
    RenderPass / GraphicsPipeline
      (Viewport/ScissorはDynamic Stateで動的指定)

  [破棄順序: 下から上 / 生成順序: 上から下]
    SwapchainKHR (新しいExtentで画像群を再確保)
         |
         v
    ImageView    (新しいSwapchain画像を参照)
         |
         v
    Framebuffer  (新しいImageViewとサイズを束ねる)
+----------------------------------------------+
```

### 3. `DrawFrame()` における再生成フローと同期の注意点

```
+----------------------------------------------+
|        DrawFrame() と再生成のフロー          |
+----------------------------------------------+
  1. waitForFences (前フレームGPU完了待機)
         |
  2. acquireNextImage
     +-- [OutOfDate例外]
     |     -> RecreateSwapchain() して return
     |        (Fenceは未リセットのまま維持!)
     +-- [Suboptimal]
     |     -> セマフォはSignal済みのため続行し、
     |        Present後に再生成予約
         |
  3. resetFences (画像取得成功後にリセット)
         |
  4. RecordCommandBuffer -> submit
         |
  5. presentKHR
     +-- [OutOfDate例外 / Suboptimal / Resized]
           -> RecreateSwapchain()
+----------------------------------------------+
```

ここには2つの重要な同期上のポイントがあります。

1. **デッドロック防止（Fenceのリセット位置）**:
   `acquireNextImage` が `vk::OutOfDateKHRError` を送出して早期リターンする場合、`submit` が実行されないためフェンスはシグナルされません。もし `acquireNextImage` より前に `resetFences` を呼んでいると、次フレームの `waitForFences` で永久にブロック（デッドロック）してしまいます。そのため、`acquireNextImage` が成功した後にのみ `resetFences` を呼び出します。
2. **セマフォ状態不整合の防止（`acquireNextImage` の `eSuboptimalKHR`）**:
   `acquireNextImage` が `eSuboptimalKHR` を返した場合、画像の取得自体は成功しており `image_available_semaphores_[current_frame_]` は**シグナル状態**になっています。ここで `submit` せずに早期リターンすると、セマフォがシグナル状態のまま放置され、次回使用時にValidation Layerエラーになります。そのため、`eSuboptimalKHR` の場合はそのフレームの `submit` と `presentKHR` を最後まで実行してセマフォを消費し、フレーム末尾で `RecreateSwapchain()` を呼び出します。

### 4. ウィンドウ最小化（サイズ 0x0）への対処

ウィンドウが最小化されると、フレームバッファの幅・高さが `0` になります。Vulkan仕様上、幅や高さが `0` のSwapchainを作成することは禁止（仕様違反）されています。
そのため、`RecreateSwapchain()` の冒頭でフレームバッファサイズが `0` の間は `Window::WaitEvents()`（`glfwWaitEvents()`）でスレッドをスリープさせ、ウィンドウが元のサイズに復元されてから再生成処理を再開します。

---

## 主な変更点

### src/window.h, src/window.cpp

- コンストラクタ `Window::Window()` で `glfwSetWindowUserPointer(window_, this)` と `glfwSetFramebufferSizeCallback(window_, FramebufferResizeCallback)` を登録しました。
- `Window::IsFramebufferResized()` / `Window::ResetFramebufferResized()` により、リサイズ発生有無の取得とリセットを行えるようにしました。
- `Window::WaitEvents()` を追加し、最小化時にCPUを消費せずイベント待機できるようにしました。

### src/vulkan_app.h, src/vulkan_app.cpp

- `VulkanApp::CleanupSwapchain()`
  - `swapchain_framebuffers_.clear()` -> `swapchain_image_views_.clear()` -> `swapchain_ = nullptr` の順（依存関係の逆順）で古いSwapchain関連リソースを破棄します。
  - 先に古い `swapchain_` を `nullptr` 代入で破棄してから `CreateSwapchain()` を呼ぶことで、同一Surface上に複数のSwapchainが同時存在することによるドライバ制約を回避しています。
- `VulkanApp::RecreateSwapchain()`
  - 最小化（幅または高さが `0`）の間は `Window::WaitEvents()` で待機します。
  - `device_.waitIdle()` でGPUの実行完了を待ってから、`CleanupSwapchain()` -> `CreateSwapchain()` -> `CreateImageViews()` -> `CreateFramebuffers()` を実行します。
  - また、`presentKHR` が `OutOfDate` で失敗した際に未消費のまま残ったシグナル済みセマフォをクリアするため、スワップチェーン画像ごとの `render_finished_semaphores_` も再生成します。
- `VulkanApp::DrawFrame()`
  - `acquireNextImage` と `presentKHR` をそれぞれ `try-catch (const vk::OutOfDateKHRError&)` で囲み、`eSuboptimalKHR` および `window_.IsFramebufferResized()` とあわせて適切なタイミングで `RecreateSwapchain()` を呼び出します。

### PROGRESS.md

- 「ウィンドウリサイズ時のSwapchain再生成」を完了（`[x]`）に更新しました。

---

## 検証結果

- **ビルド**: `cmake --build --preset conan-debug` を実行し、警告・エラーなくビルドが成功することを確認しました。
- **clang-tidy**: `run-clang-tidy -p build/Debug src/` を実行し、`src/window.cpp`、`src/vulkan_app.cpp`、`src/main.cpp` の全ファイルでユーザーコードの警告が0件であることを確認しました。
