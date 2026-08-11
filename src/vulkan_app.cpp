#include "vulkan_app.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "window.h"

namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
constexpr std::array kDeviceExtensions = {vk::KHRSwapchainExtensionName};

// Validation Layer からのメッセージを受け取るコールバック。
// PFN_vkDebugUtilsMessengerCallbackEXT の型に厳密に一致させる必要があるため
// vk:: ラッパー型ではなく生のVulkan C型で宣言する。
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*message_type*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                             void* /*user_data*/) {
  // メッセンジャーはWarning/Errorのみ購読しているため、ここでは両者を
  // ログレベルとして区別するだけでよい。
  if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    spdlog::error("[validation] {}", callback_data->pMessage);
  } else {
    spdlog::warn("[validation] {}", callback_data->pMessage);
  }
  return VK_FALSE;
}

}  // namespace

VulkanApp::VulkanApp(Window& window) : window_(window) {
  CreateInstance();
  SetupDebugMessenger();
  CreateSurface();
  PickPhysicalDevice();
  CreateLogicalDevice();
  CreateSwapchain();

  spdlog::info("Vulkan instance and surface created");
}

void VulkanApp::CreateInstance() {
  auto extensions = Window::GetRequiredInstanceExtensions();

  std::vector<const char*> layers;
#ifndef NDEBUG
  // Debugビルドでのみ検証レイヤーとデバッグメッセンジャー用拡張を要求する。
  // レイヤーが環境に存在しない場合は警告のみでレイヤー無しの起動を継続する。
  const auto available_layers = context_.enumerateInstanceLayerProperties();
  validation_available_ = std::ranges::any_of(available_layers, [](const vk::LayerProperties& layer) {
    return std::strcmp(layer.layerName, kValidationLayerName) == 0;
  });
  if (validation_available_) {
    layers.push_back(kValidationLayerName);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  } else {
    spdlog::warn("{} not available, continuing without validation", kValidationLayerName);
  }
#endif

  const vk::ApplicationInfo app_info{
      .pApplicationName = "glTF Renderer",
      .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
      .pEngineName = "gltf_renderer",
      .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
      .apiVersion = VK_API_VERSION_1_3,
  };
  const vk::InstanceCreateInfo instance_create_info{
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };
  instance_ = vk::raii::Instance(context_, instance_create_info);
}

void VulkanApp::SetupDebugMessenger() {
#ifndef NDEBUG
  if (validation_available_) {
    const vk::DebugUtilsMessengerCreateInfoEXT messenger_create_info{
        .messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = &DebugCallback,
    };
    debug_messenger_.emplace(instance_, messenger_create_info);
  }
#endif
}

void VulkanApp::CreateSurface() { surface_ = window_.CreateSurface(instance_); }

void VulkanApp::PickPhysicalDevice() {
  const auto devices = instance_.enumeratePhysicalDevices();
  if (devices.empty()) {
    throw std::runtime_error("failed to find a GPU with Vulkan support");
  }

  // グラフィックス/プレゼント両キューファミリーをサポートするデバイスの中から、
  // ディスクリートGPUがあれば優先し、無ければ最初に見つかったものを使う
  // (学習用途につき詳細なスコアリングは行わない)。
  std::optional<vk::raii::PhysicalDevice> fallback;
  for (const auto& device : devices) {
    if (!IsDeviceSuitable(device)) {
      continue;
    }
    if (!fallback.has_value()) {
      fallback = device;
    }
    if (device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
      physical_device_ = device;
      spdlog::info("selected discrete GPU: {}", std::string_view(device.getProperties().deviceName));
      return;
    }
  }

  if (!fallback.has_value()) {
    throw std::runtime_error("failed to find a GPU with required queue families");
  }
  physical_device_ = *fallback;
  spdlog::info("selected GPU: {}", std::string_view(fallback->getProperties().deviceName));
}

bool VulkanApp::IsDeviceSuitable(const vk::raii::PhysicalDevice& device) const {
  if (!FindQueueFamilies(device).HasAllQueueFamilies() || !CheckDeviceExtensionSupport(device)) {
    return false;
  }
  // VK_KHR_swapchain拡張自体はサポートしていても、実際に使えるフォーマットや
  // プレゼンテーションモードが1つも無いデバイスは描画に使えないため除外する。
  const auto swapchain_support = QuerySwapchainSupport(device);
  return !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();
}

bool VulkanApp::CheckDeviceExtensionSupport(const vk::raii::PhysicalDevice& device) {
  const auto available_extensions = device.enumerateDeviceExtensionProperties();
  return std::ranges::all_of(kDeviceExtensions, [&available_extensions](std::string_view required) {
    return std::ranges::any_of(available_extensions, [required](const vk::ExtensionProperties& extension) {
      return required == std::string_view(extension.extensionName);
    });
  });
}

VulkanApp::QueueFamilyIndices VulkanApp::FindQueueFamilies(const vk::raii::PhysicalDevice& device) const {
  QueueFamilyIndices indices;
  const auto queue_families = device.getQueueFamilyProperties();
  for (uint32_t i = 0; i < queue_families.size(); ++i) {
    if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
      indices.graphics_family = i;
    }
    if (device.getSurfaceSupportKHR(i, *surface_)) {
      indices.present_family = i;
    }
    if (indices.HasAllQueueFamilies()) {
      break;
    }
  }
  return indices;
}

void VulkanApp::CreateLogicalDevice() {
  const auto indices = FindQueueFamilies(physical_device_);

  // グラフィックスキューとプレゼントキューが同一ファミリーの場合、
  // DeviceQueueCreateInfoを1つにまとめる必要があるためstd::setで重複排除する。
  const std::set<uint32_t> unique_queue_families = {indices.graphics_family.value(), indices.present_family.value()};

  constexpr float kQueuePriority = 1.0F;
  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
  queue_create_infos.reserve(unique_queue_families.size());
  for (const auto family : unique_queue_families) {
    queue_create_infos.push_back(vk::DeviceQueueCreateInfo{
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &kQueuePriority,
    });
  }

  // 検証レイヤーはInstanceレベルで既に有効化済みのため、Device側では設定しない
  // (新しいVulkanではDevice側のenabledLayerCountは非推奨/無視される)。
  const vk::DeviceCreateInfo device_create_info{
      .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(),
      .enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size()),
      .ppEnabledExtensionNames = kDeviceExtensions.data(),
  };

  device_ = vk::raii::Device(physical_device_, device_create_info);
  graphics_queue_ = device_.getQueue(indices.graphics_family.value(), 0);
  present_queue_ = device_.getQueue(indices.present_family.value(), 0);
}

VulkanApp::SwapchainSupportDetails VulkanApp::QuerySwapchainSupport(const vk::raii::PhysicalDevice& device) const {
  return SwapchainSupportDetails{
      .capabilities = device.getSurfaceCapabilitiesKHR(*surface_),
      .formats = device.getSurfaceFormatsKHR(*surface_),
      .present_modes = device.getSurfacePresentModesKHR(*surface_),
  };
}

vk::SurfaceFormatKHR VulkanApp::ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available_formats) {
  for (const auto& format : available_formats) {
    if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      return format;
    }
  }
  // 希望フォーマットが無ければ先頭を採用する。
  return available_formats.front();
}

vk::PresentModeKHR VulkanApp::ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& available_present_modes) {
  if (std::ranges::contains(available_present_modes, vk::PresentModeKHR::eMailbox)) {
    return vk::PresentModeKHR::eMailbox;
  }
  // FIFOは仕様上必ずサポートされるので安全なフォールバックになる。
  return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanApp::ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }
  // ウィンドウマネージャがcurrentExtentを固定していない(高DPI等)場合は、
  // フレームバッファの実サイズをGLFWから取得し、min/maxでクランプする。
  const auto framebuffer_size = window_.GetFramebufferSize();
  return vk::Extent2D{
      .width = std::clamp(framebuffer_size.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
      .height =
          std::clamp(framebuffer_size.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
  };
}

void VulkanApp::CreateSwapchain() {
  const auto support = QuerySwapchainSupport(physical_device_);
  const auto surface_format = ChooseSwapSurfaceFormat(support.formats);
  const auto present_mode = ChooseSwapPresentMode(support.present_modes);
  const auto extent = ChooseSwapExtent(support.capabilities);

  // 最小数ちょうどだとドライバの内部処理待ちでスループットが落ちうるため+1する。
  // ただしmaxImageCountが0(上限無し)でない場合はその上限でクランプする。
  uint32_t image_count = support.capabilities.minImageCount + 1;
  if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
    image_count = support.capabilities.maxImageCount;
  }

  const auto indices = FindQueueFamilies(physical_device_);
  const std::array<uint32_t, 2> queue_family_indices = {indices.graphics_family.value(),
                                                        indices.present_family.value()};
  const auto same_family = indices.graphics_family.value() == indices.present_family.value();

  const vk::SwapchainCreateInfoKHR create_info{
      .surface = *surface_,
      .minImageCount = image_count,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
      // グラフィックスキューとプレゼントキューが別ファミリーの場合、両方から
      // イメージにアクセスできるようConcurrentモードにする(所有権移譲が不要な分シンプル)。
      .imageSharingMode = same_family ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
      .queueFamilyIndexCount = same_family ? 0U : static_cast<uint32_t>(queue_family_indices.size()),
      .pQueueFamilyIndices = same_family ? nullptr : queue_family_indices.data(),
      .preTransform = support.capabilities.currentTransform,
      .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode = present_mode,
      .clipped = vk::True,
  };

  swapchain_ = vk::raii::SwapchainKHR(device_, create_info);
  swapchain_images_ = swapchain_.getImages();
  swapchain_image_format_ = surface_format.format;
  swapchain_extent_ = extent;

  spdlog::info("swapchain created ({}x{}, format={}, {} images)", swapchain_extent_.width, swapchain_extent_.height,
               vk::to_string(swapchain_image_format_), swapchain_images_.size());
}
