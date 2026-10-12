# AGENTS.md

このファイルは、このリポジトリでコードを扱う際にエージェント型AIにガイダンスを提供するものです。

## プロジェクト概要

glTF形式のモデルを読み込みレンダリングするためのシンプルなアプリケーション。

- グラフィックスAPI: Vulkan
- 言語: C++23
- ビルドシステム: CMake
- パッケージマネージャー: Conan

## 開発・セッション運用ルール（進捗とタスクの管理）

このプロジェクトは複数のAIエージェント・セッションから並行または順次実行される想定です。開発を円滑に進めるため、以下のルールを遵守してください。

1. **作業開始前の状況確認**: 作業開始時は必ず [PROGRESS.md](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/PROGRESS.md) を確認し、現在の実装状況と次に着手すべきタスクを把握してください。
2. **PROGRESS.mdの更新**: 各タスク（機能実装・リファクタリングなど）の完了時には、必ず PROGRESS.md の実装状況および次タスクのチェックリストを最新状態に更新してください。新規に判明した要件や技術的課題があれば、次タスク欄に追記してください。
3. **walkthrough.mdの作成**: 実装完了時は、実装内容やVulkanにおける役割・仕組みを解説した `walkthrough.md` を作成してください（詳細は「AIに対する注意事項」参照）。
4. **コミット方針**: コード変更とPROGRESS.mdの更新はセットでコミットすることを推奨します。

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

clang-tidyを手動実行する場合は、clangd（v19系）とバージョンを揃えるため `clang-tidy-19` / `run-clang-tidy-19` を明示的に指定すること（無印の`clang-tidy`/`run-clang-tidy`はAPTの都合でv18系を指すため）。clangdは内蔵デフォルトにより一部のチェック（`bugprone-unchecked-optional-access`等）をエディタ上で抑制するため、lintの検知はエディタではなく下記commitフックに委ねている。

### commitフック（lint）

`git config core.hooksPath .githooks` を一度実行すると、コミット時に `.githooks/pre-commit` が有効になる（gitの仕様上リポジトリに含めるだけでは自動有効化されないため、clone後に各自実行が必要）。

`src/` 配下の `.cpp`/`.h`/`.hpp` に変更がある場合のみ `run-clang-tidy-19 -p build/Debug src/` を実行し、警告が1件でもあればコミットを中止する。事前に `build/Debug/compile_commands.json` の生成（configure）と `clang-tidy-19`（`sudo apt install clang-tidy-19`）が必要。緊急時は `git commit --no-verify` でスキップできる。

macOSでは `run-clang-tidy-19` が存在しないため、フックはHomebrewの `llvm@19`（keg-onlyでPATHに入らない）を `brew --prefix llvm@19` で探し、その配下の `run-clang-tidy` / `clang-tidy` を使う。事前に `brew install llvm@19` が必要（PATHへの追加は不要）。

テストの仕組みは未整備。

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

現在の実装状況・次のタスクは [PROGRESS.md](file:///home/caffeine/DevelopEnv/git_repos/my_graphics_engine/gltf_renderer/PROGRESS.md) を参照。

### コミットメッセージ

[Conventional Commits](https://www.conventionalcommits.org/)のフォーマットに従うこと（例: `feat: add surface creation`, `fix: correct swapchain extent calculation`, `refactor: split main.cpp into VulkanApp class`）。

## AIに対する注意事項

### コード変更について

このプロジェクトは学習用に使います。
AIによるコード変更を行う際は、なるべく小さい単位で変更を行い、変更の意図や理由を明確にコメントとして残してください。
イディオムやベストプラクティスに従うことを心がけ、コードの可読性と保守性を重視してください。

指示されていない変更は原則として行わないようにしてください。
例えば、指示とは直接関係ないバグの修正や、細かいリファクタリングなど。
もしその変更が必要な場合は、ユーザーに変更してよいかを聞いてください。

### コミットのタイミングについて

コード変更をした場合は、ユーザーが未コミットの差分を見てコード変更を確認できるように、1回の指示の回答としてコミットまでを行わないようにしてください。
必要になったら、ユーザーからコミットをするように指示します。
ただし、コード変更完了時はcommit hookに設定されているチェックを実行し、問題があれば修正するようにしてください。

### walkthrough.md の作成と学習用解説について

このプロジェクトは学習を目的としています。タスクの実装完了時は、必ず `walkthrough.md` アーティファクトを作成・更新し、以下の内容を記載してください：

- **実装内容の概要**: 何を実装したのか。
- **Vulkanにおける役割や仕組みの解説**: 実装したオブジェクトや機能が、Vulkanのパイプライン全体や他のリソース（RenderPass、ImageView、Pipeline等）の中でどのような役割・関係性を持つのか。理解を深めるため、必要に応じてスマートフォンでも見やすい幅50文字以内のアスキーアート図を含めること。
- **主な変更点**: 追加・変更したファイル、クラス、関数のポイントと設計上の理由（リソースの破棄順序や依存関係など）。
- **検証結果**: ビルド確認、commitフック（clang-tidy）の実行結果、動作ログなど。

チャットの回答においても、この解説に沿って学習に役立つよう要点を分かりやすく構造化して伝えてください。

### チャットでの回答について

必ず日本語で回答してください。
主語を明確にし、箇条書きで文章を構造化するなど、テクニカルライティングの手法を用いて読みやすい文章で回答してください。

### Context7 mcpが利用可能な場合の指示

ライブラリやAPIのドキュメント、コード生成、セットアップや設定手順が必要な場合は、明示的に求められなくても常にContext7 MCPを使用してください。
Vulkanによる実装を行う際は、以下のライブラリを使用してください:

- Vulkanのヘッダ情報: /khronosgroup/vulkan-hpp
- Vulkanに関するドキュメント:  /khronosgroup/vulkan-docs
- Vulkanの公式ガイド: /khronosgroup/vulkan-guide

その他のライブラリは必要に応じてContext7 MCPからクエリしてください。
