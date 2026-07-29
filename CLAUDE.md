# CLAUDE.md

このファイルは、このリポジトリでコードを扱う際にClaude Code (claude.ai/code) にガイダンスを提供するものです。

## プロジェクト概要

glTF形式のモデルを読み込みレンダリングするためのシンプルなアプリケーション。

- グラフィックスAPI: Vulkan
- 言語: C++23
- ビルドシステム: CMake
- パッケージマネージャー: Conan

## ビルド・実行コマンド

依存関係はConan 2、ビルドはCMake（`cmake_layout()`によるConan標準レイアウト）を使用する。

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

Releaseビルドの場合は `-s build_type=Release` / `--preset conan-release` に読み替える。

`CMAKE_EXPORT_COMPILE_COMMANDS ON` により `build/Debug/compile_commands.json` が生成され、リポジトリ直下に `compile_commands.json` としてシンボリックリンクしてある（clangd等のIDEツール用。`.gitignore`対象）。configureをやり直した場合はリンクを再作成すること。

テスト・lintの仕組みは未整備（現時点ではコードが`main.cpp`一つのみ）。

## 依存関係（Conan / ConanCenter）

`conanfile.py` で管理:
- `glfw/3.4` — ウィンドウ管理
- `vulkan-headers/1.3.290.0` / `vulkan-loader/1.3.290.0` — Vulkan本体（バージョンは揃えて更新すること。loaderがheadersに依存するため、揃えないと解決時にバージョン衝突しうる）

Vulkan Validation Layer本体（`VK_LAYER_KHRONOS_validation`）はConanでは取得せず、システムにapt導入済みのもの（`vulkan-validationlayers`パッケージ、`/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json`）をローダーの標準検索パス経由で利用する。CI/新規開発環境では別途 `sudo apt install vulkan-validationlayers` 等が必要になる想定。

CMakeの`find_package(Vulkan REQUIRED)` / `Vulkan::Vulkan`は、Conanの`CMakeDeps`が生成する`FindVulkan.cmake`互換シムによって`vulkan-loader`/`vulkan-headers`にマッピングされる（Conanパッケージ名の`VulkanLoader`/`VulkanHeaders`を直接find_packageする必要はない）。

## アーキテクチャ

現状は `src/main.cpp` 一つに手続き的に実装されている（意図的 — クラス分割は責務が増えるタイミングで行う方針）:

1. GLFWでウィンドウを作成（`GLFW_CLIENT_API = GLFW_NO_API`でOpenGLコンテキストは作らない）
2. `glfwGetRequiredInstanceExtensions()` でプラットフォーム別に必要なInstance拡張を取得
3. Debugビルド（`#ifndef NDEBUG`）でのみ、`VK_LAYER_KHRONOS_validation` の利用可否を確認した上で検証レイヤーと `VK_EXT_debug_utils` 拡張を追加
4. `vulkan_raii.hpp`（`vk::raii::*`、C++ RAIIバインディング）で `vk::raii::Instance` を生成。Debugビルドでは `vk::raii::DebugUtilsMessengerEXT` も生成し、検証メッセージを`std::cerr`に出力
5. RAIIラッパー（`vk::-prefixed`構造体はdesignated initializer前提のため `VULKAN_HPP_NO_STRUCT_CONSTRUCTORS` を定義済み）はスコープを抜ける際に宣言と逆順で自動破棄される。GLFWのウィンドウ/ライブラリのみ明示的に`glfwDestroyWindow`/`glfwTerminate`で後始末する
6. Vulkan由来の例外（`vk::SystemError`系）・GLFW初期化失敗は`main()`の`try/catch`で捕捉し、`EXIT_FAILURE`で終了する

次のマイルストーンでSurface/物理デバイス選択/論理デバイス作成に着手する際、責務が増える段階で`main.cpp`をクラス・ファイル単位に分割する想定。

### コミットメッセージ

[Conventional Commits](https://www.conventionalcommits.org/)のフォーマットに従うこと（例: `feat: add surface creation`, `fix: correct swapchain extent calculation`, `refactor: split main.cpp into VulkanApp class`）。

## AIに対する注意事項

### コード変更について

このプロジェクトは学習用に使います。
AIによるコード変更を行う際は、なるべく小さい単位で変更を行い、変更の意図や理由を明確にコメントとして残してください。
イディオムやベストプラクティスに従うことを心がけ、コードの可読性と保守性を重視してください。

### チャットでの回答について

必ず日本語で回答してください。
主語を明確にし、箇条書きで文章を構造化するなど、テクニカルライティングの手法を用いて読みやすい文章で回答してください。
