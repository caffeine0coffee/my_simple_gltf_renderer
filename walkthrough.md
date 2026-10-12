# コマンドプール/コマンドバッファ作成の実装

[PROGRESS.md](PROGRESS.md) の次のタスク「コマンドプール/コマンドバッファの作成」を実装しました。

## 概要

- **コマンドプール**（`vk::raii::CommandPool`）を生成しました。グラフィックスキューファミリー用です。
- **コマンドバッファ**（`vk::raii::CommandBuffer`）を、in-flightフレーム数（`kMaxFramesInFlight = 2`）分確保しました。
- **描画コマンドの記録関数** `RecordCommandBuffer()` を実装しました。三角形を1つ描くコマンドを記録します。
  - 記録したコマンドのキューへの送信（submit）と画面表示（present）は、次のタスク「同期オブジェクト・描画ループ」で実装します。
  - そのため、この関数はまだどこからも呼ばれていません。

## Vulkanにおける役割と仕組み

### 1. 「記録」と「実行」の分離

OpenGLでは `glDraw*` を呼ぶとすぐにドライバへ命令が渡ります。
Vulkanでは、命令はまず**コマンドバッファに記録**されます。
その後、記録したコマンドバッファを**まとめてキューへ送信**し、GPUが実行します。

```
 CPU側                         GPU側
+----------------+
| CommandPool    |  メモリの確保元
+-------+--------+
        | allocate
        v
+----------------+  submit  +--------------+
| CommandBuffer  |--------->| Graphics     |
|  begin         |          | Queue        |
|  beginRP       |          +------+-------+
|  bindPipeline  |                 |
|  setViewport   |                 v
|  draw(3)       |          +--------------+
|  endRP         |          | GPUが実行    |
|  end           |          +--------------+
+----------------+
 (今回はここまで)            (次のタスク)
```

この方式には、次の利点があります。

- 記録をマルチスレッドで並列に行える。
- 送信の直前まで、ドライバがコマンドをまとめて最適化できる。

### 2. コマンドプールの役割

- コマンドプールは、コマンドバッファのメモリを管理する「確保元」です。
- プールは**1つのキューファミリーに紐づきます**。このプールから確保したコマンドバッファは、そのファミリーのキューにしか送信できません。
  - 今回は描画を行うので、グラフィックスキューファミリーを指定しました。
- `eResetCommandBuffer` フラグを付けました。これにより、コマンドバッファを1個ずつ `reset()` して記録し直せます。

### 3. なぜコマンドバッファを2個確保するのか（in-flightフレーム）

コマンドバッファが1個だけだと、GPUが実行を終えるまでCPUは次の記録を始められません。
2個あれば、GPUがフレームNを描画している間に、CPUはもう一方へフレームN+1を記録できます。

```
時間 ---------------------------------->
CPU : [記録 CB0][記録 CB1][記録 CB0]...
GPU :           [実行 CB0][実行 CB1]...
          CB0/CB1を交互に使い、
          CPUとGPUが並行して動く
```

- 「GPUがCB0を使い終えたか」の判定には **Fence** を使います（次のタスク）。
- 値を大きくするほど並行性は上がりますが、入力から表示までの遅延も増えます。一般的には2が使われます。

### 4. 記録するコマンドと、既存オブジェクトとの関係

`RecordCommandBuffer()` は、これまでに作ったオブジェクトを次のように使います。

```
beginRenderPass
 |-- RenderPass   : loadOp=Clearで黒に塗る
 |-- Framebuffer  : image_index番目を描画先に
 v
bindPipeline     : GraphicsPipeline
 v
setViewport/     : Dynamic Stateにしたので
setScissor         描画前に設定が必須
 v
draw(3,1,0,0)    : 頂点はシェーダー内で定義
 v
endRenderPass    : PresentSrcKHRへ自動遷移
```

- **Framebuffer**: スワップチェーンイメージごとに1つずつあります。どのイメージに描くか（`image_index`）は、描画ループで `acquireNextImage` を呼ぶまで決まりません。そのため、記録関数の引数で受け取ります。
- **ClearValue**: RenderPassのカラーアタッチメントを `loadOp = eClear` にしたので、そのクリア色をここで渡します。

## 主な変更点

### [vulkan_app.h](src/vulkan_app.h)

- 次のメソッド宣言を追加しました。
  - `CreateCommandPool()`
  - `CreateCommandBuffers()`
  - `RecordCommandBuffer(const vk::raii::CommandBuffer&, uint32_t) const`
- 定数 `kMaxFramesInFlight = 2` を追加しました。
- メンバ `command_pool_` と `command_buffers_` を、メンバ宣言の**末尾**に追加しました。
  - `vk::raii` オブジェクトは、宣言と逆の順番で破棄されます。
  - `vk::raii::CommandBuffer` は、破棄時に確保元のプールへ自身を返却します（`vkFreeCommandBuffers`）。
  - そのため、破棄の順番は「コマンドバッファ → コマンドプール → … → Device」でなければなりません。この順番を、宣言順で保証しています。

### [vulkan_app.cpp](src/vulkan_app.cpp)

- コンストラクタで、`CreateFramebuffers()` の後に `CreateCommandPool()` と `CreateCommandBuffers()` を呼ぶようにしました。
- `CreateCommandPool()`
  - グラフィックスキューファミリーを指定し、`eResetCommandBuffer` 付きでプールを生成します。
  - optionalのチェックは、既存の `CreateLogicalDevice()` / `CreateSwapchain()` と同じ書き方に揃えました。
- `CreateCommandBuffers()`
  - `device_.allocateCommandBuffers()` で、Primaryレベルのコマンドバッファを2個確保します。
- `RecordCommandBuffer()`
  - 上の図の順番でコマンドを記録します。
  - `vk::ClearValue` は共用体（union）です。`VULKAN_HPP_NO_STRUCT_CONSTRUCTORS` を定義していてもコンストラクタが使えるため、コンストラクタで初期化しています。
  - フレームバッファの取得は、範囲チェック付きの `.at()` で行います。

### [PROGRESS.md](PROGRESS.md)

- このタスクを完了にしました。
- 次のタスクで必要になる事項を追記しました（Semaphore/Fenceの単位、`waitIdle`）。

## 検証結果

- **ビルド**: `cmake --build --preset conan-debug` が、警告・エラーなしで成功しました。
- **clang-tidy**: `run-clang-tidy`（LLVM 19、Homebrewの `llvm@19`）で `src/` を検査し、警告0件でした。
- **実行**: 起動ログは以下のとおりです。Validation Layerからの警告・エラーはありませんでした。

```
[info] selected GPU: Apple M2
[info] swapchain created (1600x1200, ...)
[info] framebuffers created (count: 3)
[info] command pool created
[info] command buffers allocated (count: 2)
```

> [!NOTE]
> この時点では、ウィンドウはまだ何も描画されません。三角形が表示されるのは、次のタスク（同期オブジェクト・描画ループ）の完了後です。
