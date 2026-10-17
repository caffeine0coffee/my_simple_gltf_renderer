# 同期オブジェクト作成と描画ループの実装

[PROGRESS.md](PROGRESS.md) の次のタスク「同期オブジェクト（Semaphore/Fence）の作成、描画ループの実装」を実装しました。

## 概要

- **同期オブジェクト**（`vk::raii::Semaphore`, `vk::raii::Fence`）を生成しました。
  - スワップチェーン画像取得待ちセマフォ（`image_available_semaphores_`: in-flightフレーム毎、2個）
  - 描画完了・プレゼント待ちセマフォ（`render_finished_semaphores_`: スワップチェーン画像毎、3個）
  - CPU側の前フレーム完了待ちフェンス（`in_flight_fences_`: in-flightフレーム毎、2個）
- **描画ループ関数** `DrawFrame()` を実装し、`main.cpp` のイベントループから毎フレーム呼び出すようにしました。
- **安全な終了同期**: デストラクタ `~VulkanApp()` で `device_.waitIdle()` を実行し、未完了のGPU処理を待ってからRAIIで安全にリソースを破棄するようにしました。

---

## Vulkanにおける役割と仕組み

### 1. Semaphore と Fence の違い

VulkanはCPUとGPUが非同期かつ並列に動作するため、明示的な同期が必要です。

| 種類 | 同期対象 | 用途 |
|---|---|---|
| **Fence** | **CPU - GPU 間** | CPUがGPUの処理完了を待機する（`waitForFences`） |
| **Semaphore** | **GPU - GPU 間** | キュー送信同士や表示エンジン間の実行順序をGPU内で制御する |

### 2. 1フレームの同期の流れ

毎フレーム、以下のサイクルでCPUとGPU、ディスプレイ表示が同期します。

```
+-----------------------------------------------+
|         フレーム同期のライフサイクル          |
+-----------------------------------------------+
  [CPU]
    1. waitForFences (前フレームGPU待機)
    2. acquireNextImage
         |-- (Signal) -> [imageAvailable]
    3. resetFences
    4. RecordCommandBuffer
    5. submit
         |-- (Wait)   <- [imageAvailable]
         |-- (Signal) -> [renderFinished]
         |-- (Signal) -> [inFlightFence]
    6. presentKHR
         |-- (Wait)   <- [renderFinished]
+-----------------------------------------------+
```

1. **CPU待機**: `waitForFences` で、該当フレームスロット（`current_frame_`）の前回GPU処理が完了するまでCPUを待機させます。初回フレームで永久ブロックしないよう、フェンスは `eSignaled` 状態で生成します。
2. **画像取得**: `acquireNextImage` で次に描画可能なスワップチェーン画像のインデックスを取得します。利用可能になると `imageAvailable` セマフォがシグナルされます。
3. **フェンスリセット**: `acquireNextImage` 成功後に `resetFences` でフェンスを未シグナルに戻します。
4. **コマンド送信**: `submit` では `imageAvailable` セマフォを待ってから描画を開始し、描画完了時に `renderFinished` セマフォと `inFlightFence` をシグナルします。
5. **プレゼンテーション**: `presentKHR` は `renderFinished` セマフォを待って画面へ画像を表示します。

### 3. なぜ `renderFinished` セマフォをスワップチェーン画像ごとに持つのか

`acquireNextImage` が返す画像インデックスの順序は、GPUやディスプレイのタイミングによって順不同になる可能性があります。
もし `renderFinished` セマフォを in-flight ごと（2個）に持つと、直前の画像がまだ画面表示処理中であるにもかかわらず、同じセマフォが再利用されて競合するリスクがあります。
Validation Layer の推奨に従い、スワップチェーン画像数分（通常3個）のセマフォを用意し、取得された `image_index` に対応するセマフォを使うことで、この競合を確実に防止しています。

### 4. in-flight フレーム（多重化）による並列処理

`kMaxFramesInFlight = 2` とすることで、GPUがフレーム0を描画している間に、CPUはフレーム1のコマンド記録と送信を行えます。

```
+-----------------------------------------------+
|    in-flight (フレーム多重化) のタイムライン   |
+-----------------------------------------------+
 Frame 0:
   CPU: [Rec 0]---------> [Rec 0]
   GPU:         [Exec 0]---------> [Exec 0]

 Frame 1:
   CPU:         [Rec 1]---------> [Rec 1]
   GPU:                 [Exec 1]--------->
+-----------------------------------------------+
```

これにより、CPUとGPUのアイドル時間を削減し、フレームレートと効率を最大化します。

### 5. 終了時のGPU待機とデストラクタ設計

ウィンドウを閉じてアプリケーションが終了する際、GPUがコマンドを実行している最中にリソースを破棄するとクラッシュや未定義動作の原因になります。
`~VulkanApp()` のデストラクタ内で `device_.waitIdle()` を実行することで、全キューの処理完了を待ってからリソース破棄を開始します。また、デストラクタから例外が送出されて強制終了することを防ぐため、`try-catch` で安全に保護しています。

---

## 主な変更点

### [vulkan_app.h](src/vulkan_app.h)

- public に `~VulkanApp();` および `void DrawFrame();` を追加しました。
- private に `void CreateSyncObjects();` を追加しました。
- private メンバ変数として、以下を追加しました。
  - `image_available_semaphores_`（サイズ `kMaxFramesInFlight = 2`）
  - `render_finished_semaphores_`（サイズ `swapchain_images_.size()`）
  - `in_flight_fences_`（サイズ `kMaxFramesInFlight = 2`）
  - `current_frame_`（フレーム進行インデックス）
- **破棄順序の整合**: 同期オブジェクトは `command_buffers_` より後、すなわち `device_` よりも後に宣言されているため、RAIIにより `device_` より先に安全に逆順破棄されます。

### [vulkan_app.cpp](src/vulkan_app.cpp)

- コンストラクタで `CreateCommandBuffers()` の後に `CreateSyncObjects()` を呼び出すようにしました。
- `~VulkanApp()`
  - `device_.waitIdle()` を呼び出します。clang-tidy の `bugprone-exception-escape` を防ぐため、`try-catch` で例外を捕捉してログ出力します。
- `CreateSyncObjects()`
  - フェンスは初回待機を通過できるよう `vk::FenceCreateFlagBits::eSignaled` を指定して生成します。
  - `render_finished_semaphores_` はスワップチェーンイメージ数分確保します。
- `DrawFrame()`
  - 上述のフローに従って、フェンス待機、画像取得、フェンスリセット、コマンドバッファ再記録、キューへのsubmit、プレゼンテーション、`current_frame_` 更新を実行します。

### [main.cpp](src/main.cpp)

- `while (!window.ShouldClose())` ループ内で `app.DrawFrame();` を呼び出すようにしました。

### [PROGRESS.md](PROGRESS.md)

- このタスクを完了に更新し、次タスクを「ウィンドウリサイズ時のSwapchain再生成」に更新しました。

---

## 検証結果

- **ビルド**: `cmake --build --preset conan-debug` が警告・エラーなしで成功しました。
- **clang-tidy**: `run-clang-tidy`（LLVM 19、Homebrewの `llvm@19`）で `src/` を検査し、警告0件でした（`~VulkanApp` の例外対策含む）。
- **実行**: `./build/Debug/gltf_renderer` の起動ログは以下のとおりです。Validation Layerからの警告・エラーなく描画ループが稼働することを確認しました。

```text
[info] selected GPU: Apple M2
[info] swapchain created (1600x1200, format=B8G8R8A8Srgb, 3 images)
[info] swapchain image views created (3 views)
[info] render pass created
[info] graphics pipeline created
[info] framebuffers created (count: 3)
[info] command pool created
[info] command buffers allocated (count: 2)
[info] sync objects created (in-flight: 2, swapchain images: 3)
[info] Vulkan instance, device, swapchain, image views, render pass, graphics pipeline, framebuffers, command buffers, and sync objects created
```
