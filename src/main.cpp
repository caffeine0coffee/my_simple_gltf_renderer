// vk::Xxx構造体をコンストラクタなしの集成体にし、
// { .field = value, ... } という指示付き初期化を使えるようにする。
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

// vulkan_raii.hpp を先にインクルードしておくことで、GLFW側の
// vulkan/vulkan.h 二重インクルードを回避しつつ、GLFW_INCLUDE_VULKAN による
// glfwGetRequiredInstanceExtensions() 等のVulkan連携APIを有効化する。
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

void GlfwErrorCallback(int error, const char* description) { spdlog::error("GLFW error {}: {}", error, description); }

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

int main() {
  try {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
      throw std::runtime_error("failed to initialize GLFW");
    }

    // Vulkanを使うのでGLFWにOpenGLコンテキストを作らせない。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "glTF Renderer", nullptr, nullptr);
    if (window == nullptr) {
      throw std::runtime_error("failed to create GLFW window");
    }

    const vk::raii::Context context;

    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

    std::vector<const char*> layers;
#ifndef NDEBUG
    // Debugビルドでのみ検証レイヤーとデバッグメッセンジャー用拡張を要求する。
    // レイヤーが環境に存在しない場合は警告のみでレイヤー無しの起動を継続する。
    const std::vector<vk::LayerProperties> available_layers = context.enumerateInstanceLayerProperties();
    const bool validation_available = std::ranges::any_of(available_layers, [](const vk::LayerProperties& layer) {
      return std::strcmp(layer.layerName, kValidationLayerName) == 0;
    });
    if (validation_available) {
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
    const vk::raii::Instance instance(context, instance_create_info);

    std::optional<vk::raii::DebugUtilsMessengerEXT> debug_messenger;
#ifndef NDEBUG
    if (validation_available) {
      const vk::DebugUtilsMessengerCreateInfoEXT messenger_create_info{
          .messageSeverity =
              vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
          .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                         vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                         vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
          .pfnUserCallback = &DebugCallback,
      };
      debug_messenger.emplace(instance, messenger_create_info);
    }
#endif

    // glfwCreateWindowSurface() はRAII非対応のC APIで、出力引数として
    // 生のVkSurfaceKHRを返すため、一旦生ハンドルで受け取ってから
    // vk::raii::SurfaceKHRへラップして所有権を移す。
    std::optional<vk::raii::SurfaceKHR> surface;
    {
      VkSurfaceKHR raw_surface{};
      if (glfwCreateWindowSurface(*instance, window, nullptr, &raw_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
      }
      surface.emplace(instance, raw_surface);
    }

    spdlog::info("Vulkan instance and surface created ({} extensions, {} layers enabled)", extensions.size(),
                 layers.size());

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }

    // vk::raii::DebugUtilsMessengerEXT / Instance はスコープを抜ける際に
    // 宣言と逆順で自動破棄されるため、基本的に明示的な後始末はGLFW分のみでよい。
    // ただしSurfaceはウィンドウハンドルに依存するため、例外的に
    // ウィンドウを破棄する前に明示的に破棄しておく必要がある。
    surface.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception& e) {
    spdlog::error("Fatal error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
