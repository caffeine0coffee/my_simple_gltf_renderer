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
- [x] コマンドプール生成・コマンドバッファ確保（in-flightフレーム数 `kMaxFramesInFlight=2` 個）、描画コマンド記録関数 `RecordCommandBuffer()`（[`src/vulkan_app.h`](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/src/vulkan_app.h)）
  - `RecordCommandBuffer()` は現時点では未使用。キューへのsubmit・presentは次タスク（描画ループ）で実装する

## 次のタスク

三角形描画までの一連の実装を優先順に進める想定です。

- [x] Swapchain Image Viewの作成
- [x] レンダーパス（Render Pass）の作成
- [x] グラフィックスパイプライン（シェーダー含む）の作成
- [x] フレームバッファ（Framebuffer）の作成
- [x] コマンドプール/コマンドバッファの作成
- [ ] 同期オブジェクト（Semaphore/Fence）の作成、描画ループの実装
  - in-flightフレームごとの `imageAvailable` Semaphore / `inFlight` Fence（`kMaxFramesInFlight` 個）と、`current_frame_` によるコマンドバッファの切り替え
  - `renderFinished` Semaphoreはスワップチェーンイメージごとに持つ方が安全（Validation Layerの推奨）
  - 終了時に `device_.waitIdle()` を呼び、GPU処理の完了を待ってからRAIIで破棄する
- [ ] ウィンドウリサイズ時のSwapchain再生成

その先の展望:

- [ ] glTFモデルの読み込み・頂点バッファ等のリソース管理
- [ ] （優先度低）テスト基盤の整備
