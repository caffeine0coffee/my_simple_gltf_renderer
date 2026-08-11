#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <optional>
#include <vector>

class Window;

// Vulkanリソース一式（Instance/DebugMessenger/Surface/...）の生成・所有を担うクラス。
// vk::raii オブジェクトはメンバ宣言順に構築され、逆順に破棄される。
// これらのオブジェクト間には Device -> PhysicalDevice -> Surface -> Instance -> Context
// という依存関係があるため、メンバ宣言順をこの依存関係の逆順（依存先が後、依存元が先）
// と一致させることで、破棄順序の正しさをメンバ宣言順という1つのルールで保証する。
class VulkanApp {
 public:
  // window はSurface作成・所有のライフタイム全体で参照されるため、
  // 呼び出し側は window を VulkanApp より先に宣言し、より後に破棄されることを
  // 保証しなければならない（main.cpp参照）。
  explicit VulkanApp(Window& window);

 private:
  // グラフィックス描画・プレゼンテーションの各操作に使うキューファミリーのインデックス。
  // 同一ファミリーが両方を兼ねる場合と、別々のファミリーが必要な場合がある。
  struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    [[nodiscard]] bool HasAllQueueFamilies() const { return graphics_family.has_value() && present_family.has_value(); }
  };

  // スワップチェーン作成に必要な、Surfaceが持つ能力・対応フォーマット・
  // 対応プレゼンテーションモードの情報。
  struct SwapchainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
  };

  void CreateInstance();
  void SetupDebugMessenger();
  void CreateSurface();
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateSwapchain();

  [[nodiscard]] bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device) const;
  [[nodiscard]] QueueFamilyIndices FindQueueFamilies(const vk::raii::PhysicalDevice& device) const;
  [[nodiscard]] static bool CheckDeviceExtensionSupport(const vk::raii::PhysicalDevice& device);
  [[nodiscard]] SwapchainSupportDetails QuerySwapchainSupport(const vk::raii::PhysicalDevice& device) const;
  [[nodiscard]] static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(
      const std::vector<vk::SurfaceFormatKHR>& available_formats);
  [[nodiscard]] static vk::PresentModeKHR ChooseSwapPresentMode(
      const std::vector<vk::PresentModeKHR>& available_present_modes);
  [[nodiscard]] vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const;

  Window& window_;  // 非所有参照。破棄順序の制約は呼び出し側(main.cpp)の変数宣言順で保証する。

  // CreateInstance() で判定し、SetupDebugMessenger() でも参照する
  // (Debugビルドのみ意味を持つ値のため通常ビルドでは未使用)。
  bool validation_available_ = false;

  vk::raii::Context context_;
  vk::raii::Instance instance_ = nullptr;
  std::optional<vk::raii::DebugUtilsMessengerEXT> debug_messenger_;
  vk::raii::SurfaceKHR surface_ = nullptr;
  vk::raii::PhysicalDevice physical_device_ = nullptr;
  vk::raii::Device device_ = nullptr;
  vk::raii::Queue graphics_queue_ = nullptr;
  vk::raii::Queue present_queue_ = nullptr;
  vk::raii::SwapchainKHR swapchain_ = nullptr;
  std::vector<vk::Image> swapchain_images_;
  vk::Format swapchain_image_format_ = vk::Format::eUndefined;
  vk::Extent2D swapchain_extent_;
};
