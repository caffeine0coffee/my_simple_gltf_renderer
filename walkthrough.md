# フレームバッファ（Framebuffer）作成の実装

[PROGRESS.md](PROGRESS.md) の次のタスクである「フレームバッファ（Framebuffer）の作成」を実装しました。

## 概要

Vulkanのレンダーパス（[`vk::raii::RenderPass`](src/vulkan_app.h)）は描画時に使用するアタッチメント（カラーバッファや深度バッファ）の仕様・フォーマットのみを定義します。
フレームバッファ（[`vk::raii::Framebuffer`](src/vulkan_app.h)）は、その定義に対して実際の画像（[`vk::raii::ImageView`](src/vulkan_app.h)）をバインドする役割を持ちます。

スワップチェーンの画像は複数枚（ダブルバッファリングやトリプルバッファリング等）存在するため、各イメージビューに対応するフレームバッファを1対1で作成します。

```
+-----------------------------------+
|            RenderPass             |
|   (カラーアタッチメントの定義)     |
+-----------------+-----------------+
                  | 参照
                  v
+-----------------+-----------------+
|           Framebuffer             |
|   (実際のImageViewをバインド)     |
+-----------------+-----------------+
                  | バインド
                  v
+-----------------+-----------------+
|       Swapchain ImageView         |
+-----------------------------------+
```

## 変更内容

### 1. `src/vulkan_app.h`
- [`VulkanApp::CreateFramebuffers()`](src/vulkan_app.h) メソッド宣言を追加。
- メンバ変数 `swapchain_framebuffers_`（`std::vector<vk::raii::Framebuffer>`）を追加。
  - RAIIリソースの破棄順序（宣言の逆順）に基づき、依存元である `swapchain_framebuffers_` を `render_pass_` や `swapchain_image_views_` より後に破棄されるよう宣言末尾に配置。

### 2. `src/vulkan_app.cpp`
- コンストラクタ `VulkanApp::VulkanApp` で `CreateGraphicsPipeline()` に続いて `CreateFramebuffers()` を呼び出すよう追加。
- `VulkanApp::CreateFramebuffers()` を実装:
  - 各 `swapchain_image_views_` を反復し、`vk::FramebufferCreateInfo` にレンダーパス・画像サイズ（`swapchain_extent_`）・イメージビューを設定して生成。

### 3. `PROGRESS.md`
- 実装状況に「フレームバッファ（Framebuffer）生成」を追加。
- 次のタスクの「フレームバッファ（Framebuffer）の作成」を完了（`[x]`）に更新。

## 検証結果

- **ビルド**: `cmake --build --preset conan-debug` が警告・エラーなく成功。
- **commit hook (clang-tidy)**: `run-clang-tidy-19 -p build/Debug src/` を実行し、全3ファイルとも警告0件でパス。
- **動作検証**: 実行バイナリを実行し、バリデーションレイヤーのエラー・警告なく、スワップチェーンの画像数に応じた4個のフレームバッファが正常に生成されたことを確認。
