#include "vulkan_app.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
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
  CreateGraphicsPipeline();
  CreateFramebuffers();
  CreateCommandPool();
  CreateCommandBuffers();
  CreateSyncObjects();

  spdlog::info(
      "Vulkan instance, device, swapchain, image views, render pass, graphics pipeline, framebuffers, "
      "command buffers, and sync objects created");
}

VulkanApp::~VulkanApp() {
  if (*device_) {
    // オブジェクトの破棄時にGPUがまだコマンドを実行していると未定義動作になるため、
    // 全キューのアイドル状態（完了）を待機してからRAIIメンバの破棄を開始する。
    // デストラクタからの例外送出を防ぐため、例外を捕捉してログ出力します。
    try {
      device_.waitIdle();
    } catch (const std::exception& e) {
      spdlog::error("Failed to wait for device idle in destructor: {}", e.what());
    }
  }
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

  // macOS (MoltenVK) では VK_KHR_portability_enumeration が必要な場合がある
  vk::InstanceCreateFlags flags{};
  const auto available_extensions = context_.enumerateInstanceExtensionProperties();
  const bool has_portability_enumeration =
      std::ranges::any_of(available_extensions, [](const vk::ExtensionProperties& ext) {
        return std::string_view(ext.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
      });
  if (has_portability_enumeration) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
  }

  const vk::ApplicationInfo app_info{
      .pApplicationName = "glTF Renderer",
      .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
      .pEngineName = "gltf_renderer",
      .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
      .apiVersion = VK_API_VERSION_1_3,
  };
  const vk::InstanceCreateInfo instance_create_info{
      .flags = flags,
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

  // macOS (MoltenVK) では VK_KHR_portability_subset 拡張がデバイスで利用可能な場合、有効化が必須
  std::vector<const char*> enabled_extensions(kDeviceExtensions.begin(), kDeviceExtensions.end());
  const auto available_device_extensions = physical_device_.enumerateDeviceExtensionProperties();
  constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";
  const bool has_portability_subset =
      std::ranges::any_of(available_device_extensions, [](const vk::ExtensionProperties& ext) {
        return std::string_view(ext.extensionName) == kPortabilitySubsetExtension;
      });
  if (has_portability_subset) {
    enabled_extensions.push_back(kPortabilitySubsetExtension);
  }

  // 検証レイヤーはInstanceレベルで既に有効化済みのため、Device側では設定しない
  // (新しいVulkanではDevice側のenabledLayerCountは非推奨/無視される)。
  const vk::DeviceCreateInfo device_create_info{
      .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(),
      .enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
      .ppEnabledExtensionNames = enabled_extensions.data(),
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

void VulkanApp::CleanupSwapchain() {
  // Framebuffer -> ImageView -> Swapchain の依存関係に従い、逆順で明示的に破棄する。
  swapchain_framebuffers_.clear();
  swapchain_image_views_.clear();
  swapchain_ = nullptr;
}

void VulkanApp::RecreateSwapchain() {
  // ウィンドウ最小化時などはフレームバッファサイズが 0x0 になるため、
  // サイズが非 0 に復帰するまでイベントを待機して一時停止する
  // （Vulkan仕様上、幅または高さが 0 のスワップチェーンは作成できないため）。
  auto extent = window_.GetFramebufferSize();
  while (extent.width == 0 || extent.height == 0) {
    Window::WaitEvents();
    extent = window_.GetFramebufferSize();
  }

  // acquireNextImage の OutOfDate 等から呼ばれた場合も含め、今回のリサイズイベントを消化扱いにする。
  window_.ResetFramebufferResized();

  // 使用中のリソースを破棄しないよう、GPU側の処理がすべて完了するまで待機する。
  device_.waitIdle();

  CleanupSwapchain();

  CreateSwapchain();
  CreateImageViews();
  CreateFramebuffers();

  // スワップチェーン再生成に伴いイメージ数が変化した場合や、presentKHR が
  // OutOfDate で失敗しシグナル状態のまま残ったセマフォをリセットするため、
  // スワップチェーン画像ごとの renderFinished セマフォも作り直す。
  render_finished_semaphores_.clear();
  render_finished_semaphores_.reserve(swapchain_images_.size());
  const vk::SemaphoreCreateInfo semaphore_info{};
  for (size_t i = 0; i < swapchain_images_.size(); ++i) {
    render_finished_semaphores_.emplace_back(device_, semaphore_info);
  }
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

std::vector<char> VulkanApp::ReadBinaryFile(const std::filesystem::path& file_path) {
  // std::ios::ate でファイル末尾から開くことで、tellg() により即座にファイルサイズを取得する
  std::ifstream file(file_path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + file_path.string());
  }

  const auto file_size = file.tellg();
  if (file_size <= 0) {
    throw std::runtime_error("shader file is empty: " + file_path.string());
  }

  std::vector<char> buffer(static_cast<size_t>(file_size));
  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(file_size));
  return buffer;
}

vk::raii::ShaderModule VulkanApp::CreateShaderModule(const std::vector<char>& code) const {
  if (code.empty() || (code.size() % sizeof(uint32_t) != 0)) {
    throw std::runtime_error("invalid shader bytecode size");
  }

  const vk::ShaderModuleCreateInfo create_info{
      .codeSize = code.size(),
      // std::vectorのメモリは最大アライメント(max_align_t)が保証されているためuint32_t*へのキャストは安全
      .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };

  return {device_, create_info};
}

void VulkanApp::CreateGraphicsPipeline() {
  // 1. シェーダーバイトコード（SPIR-V）の読み込み
  // SHADER_BINARY_DIR マクロが定義されていればビルド出力先を優先して探索し、
  // 存在しない場合はカレント作業ディレクトリ相対の shaders/ をフォールバックとして探索する
  std::filesystem::path shader_dir = "shaders";
#ifdef SHADER_BINARY_DIR
  if (std::filesystem::exists(SHADER_BINARY_DIR)) {
    shader_dir = SHADER_BINARY_DIR;
  }
#endif

  const auto vert_shader_code = ReadBinaryFile(shader_dir / "shader.vert.spv");
  const auto frag_shader_code = ReadBinaryFile(shader_dir / "shader.frag.spv");

  const auto vert_shader_module = CreateShaderModule(vert_shader_code);
  const auto frag_shader_module = CreateShaderModule(frag_shader_code);

  // 2. プログラマブルシェーダーステージの設定
  const vk::PipelineShaderStageCreateInfo vert_shader_stage_info{
      .stage = vk::ShaderStageFlagBits::eVertex,
      .module = *vert_shader_module,
      .pName = "main",
  };

  const vk::PipelineShaderStageCreateInfo frag_shader_stage_info{
      .stage = vk::ShaderStageFlagBits::eFragment,
      .module = *frag_shader_module,
      .pName = "main",
  };

  const std::array shader_stages = {vert_shader_stage_info, frag_shader_stage_info};

  // 3. 動的状態（Dynamic State）の設定
  // ビューポートとシザー矩形を実行時に動的変更可能にすることで、ウィンドウリサイズ時のパイプライン再作成を不要にする
  const std::array dynamic_states = {
      vk::DynamicState::eViewport,
      vk::DynamicState::eScissor,
  };

  const vk::PipelineDynamicStateCreateInfo dynamic_state{
      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data(),
  };

  // 4. 頂点入力状態（Vertex Input State）の設定
  // 現時点では頂点座標・カラーをシェーダー内に直接ハードコードしているため、
  // 頂点バッファのバインディング・アトリビュート記述は空として設定します（将来のタスクで拡張予定）。
  const vk::PipelineVertexInputStateCreateInfo vertex_input_info{
      .vertexBindingDescriptionCount = 0,
      .pVertexBindingDescriptions = nullptr,
      .vertexAttributeDescriptionCount = 0,
      .pVertexAttributeDescriptions = nullptr,
  };

  // 5. 入力アセンブリ状態（Input Assembly State）の設定
  // 3頂点で1つの三角形を描画するリストトポロジー
  const vk::PipelineInputAssemblyStateCreateInfo input_assembly{
      .topology = vk::PrimitiveTopology::eTriangleList,
      .primitiveRestartEnable = vk::False,
  };

  // 6. ビューポート状態（Viewport State）の設定
  // 動的状態として設定しているため、パイプライン作成時点では個数のみ指定しポインタはnullptrとする
  const vk::PipelineViewportStateCreateInfo viewport_state{
      .viewportCount = 1,
      .pViewports = nullptr,
      .scissorCount = 1,
      .pScissors = nullptr,
  };

  // 7. ラスタライザー状態（Rasterization State）の設定
  // プリミティブをフラグメントへ展開する設定
  const vk::PipelineRasterizationStateCreateInfo rasterizer{
      .depthClampEnable = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eBack,
      // 頂点シェーダー内の三角形頂点は時計回り(Clockwise)順で定義されているため eClockwise を指定
      .frontFace = vk::FrontFace::eClockwise,
      .depthBiasEnable = vk::False,
      .lineWidth = 1.0F,
  };

  // 8. マルチサンプリング状態（Multisample State）の設定
  // 現時点ではアンチエイリアシングは無効（1サンプル/ピクセル）
  const vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = vk::SampleCountFlagBits::e1,
      .sampleShadingEnable = vk::False,
  };

  // 9. カラーブレンディング状態（Color Blend State）の設定
  // フレームバッファの既存ピクセルとフラグメントカラーの合成設定（現時点ではブレンド無効・上書き）
  const vk::PipelineColorBlendAttachmentState color_blend_attachment{
      .blendEnable = vk::False,
      .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
  };

  const vk::PipelineColorBlendStateCreateInfo color_blending{
      .logicOpEnable = vk::False,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
  };

  // 10. パイプラインレイアウト（Pipeline Layout）の生成
  // シェーダー内で参照するUniform変数やPush Constantsのレイアウト（現時点では空）
  const vk::PipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_ = vk::raii::PipelineLayout(device_, pipeline_layout_info);

  // 11. グラフィックスパイプラインの生成
  const vk::GraphicsPipelineCreateInfo pipeline_info{
      .stageCount = static_cast<uint32_t>(shader_stages.size()),
      .pStages = shader_stages.data(),
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_blending,
      .pDynamicState = &dynamic_state,
      .layout = *pipeline_layout_,
      .renderPass = *render_pass_,
      .subpass = 0,
      .basePipelineHandle = nullptr,
      .basePipelineIndex = -1,
  };

  graphics_pipeline_ = vk::raii::Pipeline(device_, nullptr, pipeline_info);

  spdlog::info("graphics pipeline created");
}

void VulkanApp::CreateFramebuffers() {
  swapchain_framebuffers_.reserve(swapchain_image_views_.size());

  // 各スワップチェーンのImageViewに対してFramebufferを作成します。
  // Framebufferは、RenderPassで宣言されたアタッチメント（カラーバッファ等）に対して
  // 実際の描画ターゲットとなるImageViewをバインドする役割を持ちます。
  for (const auto& image_view : swapchain_image_views_) {
    // レンダーパス作成時に定義したアタッチメント順にImageViewを指定します
    const std::array attachments = {*image_view};

    const vk::FramebufferCreateInfo framebuffer_info{
        .renderPass = *render_pass_,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .width = swapchain_extent_.width,
        .height = swapchain_extent_.height,
        .layers = 1,
    };

    swapchain_framebuffers_.emplace_back(device_, framebuffer_info);
  }

  spdlog::info("framebuffers created (count: {})", swapchain_framebuffers_.size());
}

void VulkanApp::CreateCommandPool() {
  const auto indices = FindQueueFamilies(physical_device_);
  // PickPhysicalDevice()がIsDeviceSuitable()で必須キューファミリーの存在を
  // 確認済みのdeviceのみを選択しているため本来失敗しないが、
  // bugprone-unchecked-optional-access対応を兼ねて明示的に検証する。
  if (!indices.graphics_family.has_value()) {
    throw std::runtime_error("selected physical device unexpectedly lacks a graphics queue family");
  }

  const vk::CommandPoolCreateInfo pool_info{
      // eResetCommandBuffer: プールから確保したコマンドバッファを1個ずつ reset() できるようにする。
      // 毎フレーム同じコマンドバッファをリセットして記録し直すために必要。
      // （このフラグが無い場合は、プール全体をまとめてリセットする必要がある）
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      // コマンドプールから確保したコマンドバッファは、ここで指定したキューファミリーの
      // キューにしか送信できない。描画コマンドを送るのでグラフィックスキューファミリーを指定する。
      .queueFamilyIndex = *indices.graphics_family,
  };

  command_pool_ = vk::raii::CommandPool(device_, pool_info);

  spdlog::info("command pool created");
}

void VulkanApp::CreateCommandBuffers() {
  const vk::CommandBufferAllocateInfo alloc_info{
      .commandPool = *command_pool_,
      // ePrimary: キューへ直接送信できるコマンドバッファ。
      // （eSecondaryは直接送信できず、プライマリから呼び出して使う。マルチスレッド記録などで利用する）
      .level = vk::CommandBufferLevel::ePrimary,
      // in-flightフレームごとに1個ずつ確保する。GPUがフレームNのコマンドバッファを実行中でも、
      // CPUは別のコマンドバッファへフレームN+1を記録できる（実際の切り替えは描画ループのタスクで実装する）。
      .commandBufferCount = kMaxFramesInFlight,
  };

  command_buffers_ = device_.allocateCommandBuffers(alloc_info);

  spdlog::info("command buffers allocated (count: {})", command_buffers_.size());
}

void VulkanApp::CreateSyncObjects() {
  image_available_semaphores_.reserve(kMaxFramesInFlight);
  in_flight_fences_.reserve(kMaxFramesInFlight);

  const vk::SemaphoreCreateInfo semaphore_info{};
  // フェンスは初回フレームの waitForFences() でブロックされないよう、Signaled 状態で作成する。
  const vk::FenceCreateInfo fence_info{
      .flags = vk::FenceCreateFlagBits::eSignaled,
  };

  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    image_available_semaphores_.emplace_back(device_, semaphore_info);
    in_flight_fences_.emplace_back(device_, fence_info);
  }

  // スワップチェーンイメージ数分のレンダリング完了セマフォ。
  // Validation Layerの推奨に従い、プレゼンテーション待機中のセマフォ再利用を防ぐため
  // スワップチェーン画像ごとに保持する。
  render_finished_semaphores_.reserve(swapchain_images_.size());
  for (size_t i = 0; i < swapchain_images_.size(); ++i) {
    render_finished_semaphores_.emplace_back(device_, semaphore_info);
  }

  spdlog::info("sync objects created (in-flight: {}, swapchain images: {})", kMaxFramesInFlight,
               swapchain_images_.size());
}

void VulkanApp::RecordCommandBuffer(const vk::raii::CommandBuffer& command_buffer, uint32_t image_index) const {
  // 1. 記録の開始
  // 毎フレーム記録し直す前提のため、特別な使用フラグ（eOneTimeSubmit等）は指定しない。
  command_buffer.begin(vk::CommandBufferBeginInfo{});

  // 2. レンダーパスの開始
  // RenderPassのloadOp=eClearで使われるクリア色（黒・不透明）。
  // vk::ClearValue/vk::ClearColorValue は共用体（union）であり、
  // VULKAN_HPP_NO_STRUCT_CONSTRUCTORS の影響を受けないためコンストラクタで初期化する。
  const vk::ClearValue clear_color{vk::ClearColorValue{0.0F, 0.0F, 0.0F, 1.0F}};

  const vk::RenderPassBeginInfo render_pass_info{
      .renderPass = *render_pass_,
      // 描画先のスワップチェーンイメージに対応するフレームバッファを選ぶ。
      .framebuffer = *swapchain_framebuffers_.at(image_index),
      // シェーダーの読み書きが行われる領域。アタッチメント全体を対象にする。
      .renderArea =
          {
              .offset = {.x = 0, .y = 0},
              .extent = swapchain_extent_,
          },
      // アタッチメント（ここではカラー1つ）ごとのクリア値。
      .clearValueCount = 1,
      .pClearValues = &clear_color,
  };

  // eInline: サブパスのコマンドをこのプライマリコマンドバッファへ直接記録する
  // （セカンダリコマンドバッファは使わない）。
  command_buffer.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);

  // 3. グラフィックスパイプラインのバインド
  command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphics_pipeline_);

  // 4. 動的状態（ビューポート/シザー）の設定
  // パイプライン作成時にこれらをDynamic Stateとしたため、描画前に必ず設定する必要がある。
  const vk::Viewport viewport{
      .x = 0.0F,
      .y = 0.0F,
      .width = static_cast<float>(swapchain_extent_.width),
      .height = static_cast<float>(swapchain_extent_.height),
      .minDepth = 0.0F,
      .maxDepth = 1.0F,
  };
  command_buffer.setViewport(0, viewport);

  const vk::Rect2D scissor{
      .offset = {.x = 0, .y = 0},
      .extent = swapchain_extent_,
  };
  command_buffer.setScissor(0, scissor);

  // 5. 描画コマンド
  // 頂点座標は頂点シェーダー内にハードコードされているため、頂点バッファは不要。
  // vertexCount=3（三角形1つ）, instanceCount=1, firstVertex=0, firstInstance=0。
  command_buffer.draw(3, 1, 0, 0);

  // 6. レンダーパスの終了と記録の終了
  // レンダーパス終了時に、カラーアタッチメントは finalLayout（ePresentSrcKHR）へ遷移する。
  command_buffer.endRenderPass();
  command_buffer.end();
}

void VulkanApp::DrawFrame() {
  // 1. 前回のこのフレームスロットのGPU処理完了を待機する。
  // タイムアウトには最大値（UINT64_MAXナノ秒）を指定して完了を待つ。
  const auto wait_result =
      device_.waitForFences(*in_flight_fences_[current_frame_], VK_TRUE, std::numeric_limits<uint64_t>::max());
  if (wait_result != vk::Result::eSuccess) {
    spdlog::warn("waitForFences returned {}", vk::to_string(wait_result));
  }

  // 2. スワップチェーンから次の利用可能イメージを取得する。
  // 利用可能になった時点で image_available_semaphores_[current_frame_] がシグナル状態になる。
  // OutOfDate の場合は描画できないため即座にスワップチェーンを再生成してこのフレームを中断する。
  // Suboptimal の場合はイメージ取得自体は成功しておりセマフォもシグナルされているため、
  // このフレームの描画・Presentを最後まで完了させてからスワップチェーンを再生成する。
  uint32_t image_index = 0;
  bool should_recreate_swapchain = false;
  try {
    const auto [acquire_result, acquired_index] = swapchain_.acquireNextImage(
        std::numeric_limits<uint64_t>::max(), *image_available_semaphores_[current_frame_]);
    image_index = acquired_index;
    if (acquire_result == vk::Result::eSuboptimalKHR) {
      should_recreate_swapchain = true;
    }
  } catch (const vk::OutOfDateKHRError&) {
    RecreateSwapchain();
    return;
  }

  // 3. フェンスを未シグナル状態にリセットする。
  // acquireNextImage 成功後にリセットすることで、OutOfDate で早期リターンした場合に
  // フェンスが未シグナルのまま次フレームの waitForFences で永久ブロックするのを防ぐ。
  device_.resetFences(*in_flight_fences_[current_frame_]);

  // 4. コマンドバッファをリセットし、描画コマンドを再記録する。
  command_buffers_[current_frame_].reset();
  RecordCommandBuffer(command_buffers_[current_frame_], image_index);

  // 5. グラフィックスキューへコマンドバッファを送信（Submit）する。
  // スワップチェーンイメージが準備できるまでカラーアタッチメント出力ステージで待機し、
  // 描画が完了したら render_finished_semaphores_[image_index] と
  // in_flight_fences_[current_frame_] をシグナル状態にする。
  const vk::PipelineStageFlags wait_stages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  const vk::SubmitInfo submit_info{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*image_available_semaphores_[current_frame_],
      .pWaitDstStageMask = &wait_stages,
      .commandBufferCount = 1,
      .pCommandBuffers = &*command_buffers_[current_frame_],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &*render_finished_semaphores_[image_index],
  };

  graphics_queue_.submit(submit_info, *in_flight_fences_[current_frame_]);

  // 6. 描画結果を画面へプレゼンテーション（Present）する。
  // render_finished_semaphores_[image_index] がシグナル状態になるまで待機する。
  const vk::PresentInfoKHR present_info{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*render_finished_semaphores_[image_index],
      .swapchainCount = 1,
      .pSwapchains = &*swapchain_,
      .pImageIndices = &image_index,
  };

  try {
    const auto present_result = present_queue_.presentKHR(present_info);
    if (present_result == vk::Result::eSuboptimalKHR) {
      should_recreate_swapchain = true;
    } else if (present_result != vk::Result::eSuccess) {
      spdlog::warn("presentKHR returned {}", vk::to_string(present_result));
    }
  } catch (const vk::OutOfDateKHRError&) {
    should_recreate_swapchain = true;
  }

  if (should_recreate_swapchain || window_.IsFramebufferResized()) {
    RecreateSwapchain();
  }

  // 7. 次のin-flightフレームへインデックスを進める。
  current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
}
