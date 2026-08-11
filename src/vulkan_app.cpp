#include "vulkan_app.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "window.h"

namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

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
