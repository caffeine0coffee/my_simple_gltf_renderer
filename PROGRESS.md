# PROGRESS.md

このファイルは、本プロジェクトの実装状況と次のタスクを記録する進捗管理ファイルです。
作業を開始するエージェントは着手前に必ずこのファイルを確認し、タスク完了時にはチェックリストを更新してください（詳細は [AGENTS.md](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/AGENTS.md) を参照）。

## 実装状況

- [x] GLFWウィンドウ生成（[`Window`](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/src/window.h)クラス）
- [x] Vulkan Instance生成（Debugビルドでは Validation Layer + `DebugUtilsMessengerEXT` を追加）
- [x] Surface生成
- [x] 物理デバイス選択・論理デバイス生成（グラフィックスキュー/プレゼントキュー）
- [x] Swapchain生成
- [x] Swapchain Image View生成
- [x] レンダーパス生成（[`src/vulkan_app.h`](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/src/vulkan_app.h)）
- [x] `main.cpp`から`VulkanApp`/`Window`クラスへの分割（[`src/vulkan_app.h`](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/src/vulkan_app.h)）
- [x] グラフィックスパイプライン・シェーダー（SPIR-V）生成（[`shaders/`](shaders/), [`src/vulkan_app.h`](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/src/vulkan_app.h)）
- [x] フレームバッファ（Framebuffer）生成（[`src/vulkan_app.h`](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/src/vulkan_app.h)）
- [x] コマンドプール生成・コマンドバッファ確保（in-flightフレーム数 `kMaxFramesInFlight=2` 個）、描画コマンド記録関数 `RecordCommandBuffer()`（[`src/vulkan_app.h`](src/vulkan_app.h)）
- [x] 同期オブジェクト（Semaphore/Fence）の作成、描画ループの実装（[`src/vulkan_app.h`](src/vulkan_app.h), [`src/main.cpp`](src/main.cpp)）
  - in-flightフレームごとの `imageAvailable` Semaphore / `inFlight` Fence（`kMaxFramesInFlight=2` 個）と、`current_frame_` によるコマンドバッファの切り替え
  - `renderFinished` Semaphoreはスワップチェーンイメージごとに保持（Validation Layer推奨・競合安全）
  - `main.cpp` のイベントループから `app.DrawFrame()` を毎フレーム呼び出し
  - デストラクタ `~VulkanApp()` で `device_.waitIdle()` を呼び、GPU処理の完了を待ってからRAIIで安全に破棄
- [x] ウィンドウリサイズ時のSwapchain再生成（src/window.h, src/window.cpp, src/vulkan_app.h, src/vulkan_app.cpp）
  - GLFWフレームバッファサイズ変更コールバック `FramebufferResizeCallback()` と `WaitEvents()` の実装
  - `CleanupSwapchain()` / `RecreateSwapchain()` によるスワップチェーン・ImageView・Framebuffer・renderFinishedセマフォの再生成と最小化時（0x0）の待機処理
  - `DrawFrame()` における `vk::OutOfDateKHRError` / `vk::Result::eSuboptimalKHR` / リサイズフラグの検知と再生成呼び出し

## 次のタスク

三角形描画までの一連の実装が完了しました。

- [x] Swapchain Image Viewの作成
- [x] レンダーパス（Render Pass）の作成
- [x] グラフィックスパイプライン（シェーダー含む）の作成
- [x] フレームバッファ（Framebuffer）の作成
- [x] コマンドプール/コマンドバッファの作成
- [x] 同期オブジェクト（Semaphore/Fence）の作成、描画ループの実装
- [x] ウィンドウリサイズ時のSwapchain再生成

その先の展望:

- [ ] glTFモデルの読み込み・頂点バッファ等のリソース管理
- [ ] （優先度低）テスト基盤の整備
