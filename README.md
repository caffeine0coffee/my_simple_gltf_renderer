# GLTF Renderer

このプロジェクトは、glTF形式のモデルを読み込みレンダリングするためのシンプルなアプリケーションです。

- グラフィックスAPI: Vulkan
- 言語: C++23
- ビルドシステム: CMake
- パッケージマネージャー: Conan

## ビルド・実行方法

依存関係はConan 2、ビルドはCMake（`cmake_layout()`によるConan標準レイアウト）を使用します。

```bash
# 依存関係のインストール + CMakeToolchain/CMakeDeps生成（初回・依存関係変更時）
conan install . --build=missing -s build_type=Debug

# configure（Conanが生成した`conan-debug`プリセットを使用）
cmake --preset conan-debug

# ビルド
cmake --build --preset conan-debug

# 実行
./build/Debug/gltf_renderer
```

Releaseビルドの場合は `-s build_type=Release` / `--preset conan-release` に読み替えてください。

なお、Vulkan Validation Layer本体（`VK_LAYER_KHRONOS_validation`）はConanでは取得しないため、Debugビルドで検証レイヤーを有効にする場合はシステムに別途インストールする必要があります（Ubuntu/Debianの場合 `sudo apt install vulkan-validationlayers`）。

## Lint（clang-tidy）

チェック内容は`.clang-tidy`で定義しています。手動実行する場合は以下のコマンドを使用してください（要 `sudo apt install clang-tidy-19`）。

```bash
run-clang-tidy-19 -p build/Debug src/
```

また、コミット時に自動でlintを実行するGit hookを用意しています。以下を一度実行すると有効になります（clone後に各自実行が必要です）。

```bash
git config core.hooksPath .githooks
```

`src/`配下のファイルに変更がある場合のみ実行され、警告が見つかるとコミットが中止されます。緊急時は`git commit --no-verify`でスキップできます。
