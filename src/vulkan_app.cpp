#include "vulkan_app.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
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

  spdlog::info("Vulkan instance and surface created");
}

void VulkanApp::CreateInstance() {
  std::vector<const char*> extensions = Window::GetRequiredInstanceExtensions();

  std::vector<const char*> layers;
#ifndef NDEBUG
  // Debugビルドでのみ検証レイヤーとデバッグメッセンジャー用拡張を要求する。
  // レイヤーが環境に存在しない場合は警告のみでレイヤー無しの起動を継続する。
  const std::vector<vk::LayerProperties> available_layers = context_.enumerateInstanceLayerProperties();
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
  const std::vector<vk::raii::PhysicalDevice> devices = instance_.enumeratePhysicalDevices();
  if (devices.empty()) {
    throw std::runtime_error("failed to find a GPU with Vulkan support");
  }

  // グラフィックス/プレゼント両キューファミリーをサポートするデバイスの中から、
  // ディスクリートGPUがあれば優先し、無ければ最初に見つかったものを使う
  // (学習用途につき詳細なスコアリングは行わない)。
  std::optional<vk::raii::PhysicalDevice> fallback;
  for (const vk::raii::PhysicalDevice& device : devices) {
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
  const SwapchainSupportDetails swapchain_support = QuerySwapchainSupport(device);
  return !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();
}

bool VulkanApp::CheckDeviceExtensionSupport(const vk::raii::PhysicalDevice& device) {
  const std::vector<vk::ExtensionProperties> available_extensions = device.enumerateDeviceExtensionProperties();
  return std::ranges::all_of(kDeviceExtensions, [&available_extensions](std::string_view required) {
    return std::ranges::any_of(available_extensions, [required](const vk::ExtensionProperties& extension) {
      return required == std::string_view(extension.extensionName);
    });
  });
}

VulkanApp::QueueFamilyIndices VulkanApp::FindQueueFamilies(const vk::raii::PhysicalDevice& device) const {
  QueueFamilyIndices indices;
  const std::vector<vk::QueueFamilyProperties> queue_families = device.getQueueFamilyProperties();
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
  const QueueFamilyIndices indices = FindQueueFamilies(physical_device_);

  // グラフィックスキューとプレゼントキューが同一ファミリーの場合、
  // DeviceQueueCreateInfoを1つにまとめる必要があるためstd::setで重複排除する。
  const std::set<uint32_t> unique_queue_families = {indices.graphics_family.value(), indices.present_family.value()};

  constexpr float kQueuePriority = 1.0F;
  std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
  queue_create_infos.reserve(unique_queue_families.size());
  for (uint32_t family : unique_queue_families) {
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
