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

/**
 * @brief Validation Layer からのメッセージを受け取るコールバック関数。
 *
 * PFN_vkDebugUtilsMessengerCallbackEXT の型に厳密に一致させる必要があるため、
 * vk:: ラッパー型ではなく生のVulkan C型で宣言しています。
 *
 * @param message_severity メッセージの重大度（エラー、警告等）。
 * @param message_type メッセージの種別（一般的な仕様違反、パフォーマンス等、未使用）。
 * @param callback_data コールバックメッセージの詳細データ。
 * @param user_data ユーザー定義データポインタ（未使用）。
 * @return 常に VK_FALSE を返します（VK_TRUE を返すと呼び出し元APIが VK_ERROR_VALIDATION_FAILED_EXT で失敗扱いとなるため）。
 */
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
  CreateImageViews();
  CreateRenderPass();

  spdlog::info("Vulkan instance, device, swapchain, image views, and render pass created");
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
  // PickPhysicalDevice()がIsDeviceSuitable()で必須キューファミリーの存在を
  // 確認済みのdeviceのみを選択しているため本来失敗しないが、
  // bugprone-unchecked-optional-access対応を兼ねて明示的に検証する。
  if (!indices.graphics_family.has_value() || !indices.present_family.has_value()) {
    throw std::runtime_error("selected physical device unexpectedly lacks required queue families");
  }
  const auto graphics_family = *indices.graphics_family;
  const auto present_family = *indices.present_family;

  // グラフィックスキューとプレゼントキューが同一ファミリーの場合、
  // DeviceQueueCreateInfoを1つにまとめる必要があるためstd::setで重複排除する。
  const std::set<uint32_t> unique_queue_families = {graphics_family, present_family};

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
  graphics_queue_ = device_.getQueue(graphics_family, 0);
  present_queue_ = device_.getQueue(present_family, 0);
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
  // PickPhysicalDevice()がIsDeviceSuitable()で必須キューファミリーの存在を
  // 確認済みのdeviceのみを選択しているため本来失敗しないが、
  // bugprone-unchecked-optional-access対応を兼ねて明示的に検証する。
  if (!indices.graphics_family.has_value() || !indices.present_family.has_value()) {
    throw std::runtime_error("selected physical device unexpectedly lacks required queue families");
  }
  const auto graphics_family = *indices.graphics_family;
  const auto present_family = *indices.present_family;
  const std::array<uint32_t, 2> queue_family_indices = {graphics_family, present_family};
  const auto same_family = graphics_family == present_family;

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

void VulkanApp::CreateImageViews() {
  swapchain_image_views_.reserve(swapchain_images_.size());

  for (const auto& image : swapchain_images_) {
    // スワップチェーン画像を参照・操作するためのImageViewを作成する。
    // 画像そのもの（vk::Image）はGPUメモリ上のバッファを指すのみであり、
    // レンダリングやサンプリングの対象として扱うには、フォーマットや
    // アクセス範囲（サブリソース）を定義する vk::ImageView が必要となる。
    const vk::ImageViewCreateInfo create_info{
        .image = image,
        // スワップチェーン画像は標準的な2D画像として扱う。
        .viewType = vk::ImageViewType::e2D,
        .format = swapchain_image_format_,
        // 色チャンネルのマッピング設定。eIdentityにより各チャンネルを変更せずそのまま渡す。
        .components = {
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity,
        },
        // 画像のどの部分にアクセスするかを定義するサブリソース範囲。
        .subresourceRange = {
            // スワップチェーン画像はカラーレンダーターゲットとして使用する。
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    swapchain_image_views_.emplace_back(device_, create_info);
  }

  spdlog::info("swapchain image views created ({} views)", swapchain_image_views_.size());
}

void VulkanApp::CreateRenderPass() {
  // =========================================================================
  // 【Dynamic Rendering（Vulkan 1.3）に関する補足】
  //
  // Vulkan 1.3 ではコア機能として Dynamic Rendering（VK_KHR_dynamic_rendering）が導入され、
  // 事前に vk::RenderPass や vk::Framebuffer オブジェクトを生成・管理することなく、
  // コマンドバッファ記録時に vkCmdBeginRendering（vk::raii::CommandBuffer::beginRendering）
  // へ直接アタッチメントの ImageView や Clear 値（vk::RenderingInfo）を渡すだけで
  // レンダリングパスを開始・実行できるようになりました。
  //
  // Dynamic Rendering の利点:
  // - RenderPass / Framebuffer の事前作成ボイラープレートが不要になる
  // - パイプライン作成時もフォーマット一覧（VkPipelineRenderingCreateInfo）を指定するだけで済む
  // - スワップチェーン再生成時の Framebuffer 再生成処理が不要になる
  //
  // 本プロジェクトで従来の vk::RenderPass を採用する理由:
  // - 本プロジェクトは Vulkan の学習を主目的としており、アタッチメントのライフタイム、
  //   ロード/ストア操作、サブパス構造、サブパス依存関係（メモリ・実行同期）
  //   といった Vulkan の中核概念を明示的に理解・習得するため、標準的な vk::RenderPass を用いています。
  // =========================================================================

  // 1. カラーアタッチメントの設定
  // スワップチェーン画像を描画先（カラーバッファ）としてどのように扱うかを定義する。
  const vk::AttachmentDescription color_attachment{
      .format = swapchain_image_format_,
      // マルチサンプリング（MSAA）は行わないため 1 サンプルを指定。
      .samples = vk::SampleCountFlagBits::e1,
      // レンダリング開始時にアタッチメントの内容をクリア（背景色で塗りつぶす）。
      .loadOp = vk::AttachmentLoadOp::eClear,
      // レンダリング終了時に描画結果をメモリ（スワップチェーン画像）に保存（画面表示するため）。
      .storeOp = vk::AttachmentStoreOp::eStore,
      // ステンシルバッファは現時点では使用しないため Don't Care。
      .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
      .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
      // レンダーパス開始前の画像レイアウト。直後にクリアするため以前の内容は不問（Undefined）。
      .initialLayout = vk::ImageLayout::eUndefined,
      // レンダーパス終了後の画像レイアウト。スワップチェーンのプレゼンテーションエンジンへ
      // そのまま渡せるよう PresentSrcKHR に自動遷移させる。
      .finalLayout = vk::ImageLayout::ePresentSrcKHR,
  };

  // 2. サブパスにおけるアタッチメントの参照設定
  // フラグメントシェーダーの出力（layout(location = 0) out vec4 outColor）が
  // どのアタッチメントに対応するかを定義する。
  const vk::AttachmentReference color_attachment_ref{
      .attachment = 0,  // color_attachment 配列のインデックス 0 を参照
      .layout = vk::ImageLayout::eColorAttachmentOptimal,
  };

  // 3. サブパスの定義
  // グラフィックス描画を行う単一のサブパスを定義する。
  const vk::SubpassDescription subpass{
      .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_ref,
  };

  // 4. サブパス依存関係（同期設定）の定義
  // スワップチェーン画像は、ウィンドウシステム／プレゼンテーションエンジンによる表示処理
  // （読み取り）が完了するまで書き込んではならない。
  // vkAcquireNextImageKHR でシグナルされるセマフォ待機に加え、レンダーパス開始時
  // （外部操作: VK_SUBPASS_EXTERNAL）からサブパス 0 への依存関係を明示することで、
  // カラーアタッチメント出力ステージへの書き込み（ColorAttachmentWrite）が安全に行われるよう同期する。
  const vk::SubpassDependency dependency{
      .srcSubpass = vk::SubpassExternal,
      .dstSubpass = 0,
      .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
      .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
      .srcAccessMask = vk::AccessFlags{},
      .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
  };

  // 5. レンダーパスの生成
  const vk::RenderPassCreateInfo render_pass_info{
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency,
  };

  render_pass_ = vk::raii::RenderPass(device_, render_pass_info);

  spdlog::info("render pass created");
}

