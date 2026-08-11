#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <optional>

class Window;

// Vulkanリソース一式（Instance/DebugMessenger/Surface/...）の生成・所有を担うクラス。
// vk::raii オブジェクトはメンバ宣言順に構築され、逆順に破棄される。
// これらのオブジェクト間には Surface -> Instance -> Context という依存関係があるため、
// メンバ宣言順をこの依存関係の逆順（依存先が後、依存元が先）と一致させることで、
// 破棄順序の正しさをメンバ宣言順という1つのルールで保証する。
class VulkanApp {
 public:
  // window はSurface作成・所有のライフタイム全体で参照されるため、
  // 呼び出し側は window を VulkanApp より先に宣言し、より後に破棄されることを
  // 保証しなければならない（main.cpp参照）。
  explicit VulkanApp(Window& window);

 private:
  void CreateInstance();
  void SetupDebugMessenger();
  void CreateSurface();

  Window& window_;  // 非所有参照。破棄順序の制約は呼び出し側(main.cpp)の変数宣言順で保証する。

  // CreateInstance() で判定し、SetupDebugMessenger() でも参照する
  // (Debugビルドのみ意味を持つ値のため通常ビルドでは未使用)。
  bool validation_available_ = false;

  vk::raii::Context context_;
  vk::raii::Instance instance_ = nullptr;
  std::optional<vk::raii::DebugUtilsMessengerEXT> debug_messenger_;
  vk::raii::SurfaceKHR surface_ = nullptr;
};
