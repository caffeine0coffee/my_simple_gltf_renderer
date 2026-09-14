#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <optional>
#include <vector>

class Window;

/**
 * @brief Vulkanリソース一式（Instance/DebugMessenger/Surface/...）の生成・所有を担うクラス。
 *
 * vk::raii オブジェクトはメンバ宣言順に構築され、逆順に破棄されます。
 * これらのオブジェクト間には Device -> PhysicalDevice -> Surface -> Instance -> Context
 * という依存関係があるため、メンバ宣言順をこの依存関係の逆順（依存先が後、依存元が先）
 * と一致させることで、破棄順序の正しさをメンバ宣言順という1つのルールで保証します。
 */
class VulkanApp {
 public:
  /**
   * @brief VulkanAppを初期化し、必要なVulkanリソース一式を生成します。
   *
   * window はSurface作成・所有のライフタイム全体で参照されるため、
   * 呼び出し側は window を VulkanApp より先に宣言し、より後に破棄されることを
   * 保証しなければなりません（main.cpp参照）。
   *
   * @param window アプリケーションが描画対象とするWindowオブジェクトの参照。
   * @throw std::runtime_error GPU選択やキューファミリー検索に失敗した場合。
   */
  explicit VulkanApp(Window& window);

 private:
  /**
   * @brief グラフィックス描画・プレゼンテーションの各操作に使うキューファミリーのインデックス。
   *
   * 同一ファミリーが両方を兼ねる場合と、別々のファミリーが必要な場合があります。
   */
  struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    /**
     * @brief 必要なすべてのキューファミリー（グラフィックスおよびプレゼント）が揃っているかを判定します。
     *
     * @return 必要なキューファミリーがすべて存在する場合は true、そうでない場合は false。
     */
    [[nodiscard]] bool HasAllQueueFamilies() const { return graphics_family.has_value() && present_family.has_value(); }
  };

  /**
   * @brief スワップチェーン作成に必要な、Surfaceが持つ能力・対応フォーマット・対応プレゼンテーションモードの情報。
   */
  struct SwapchainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
  };

  /**
   * @brief Vulkan Instanceを生成します。
   *
   * 必要な拡張機能および（Debugビルドの場合は）Validation Layerを有効化します。
   */
  void CreateInstance();

  /**
   * @brief Debugビルド時にValidation Layerからのメッセージを受け取るデバッグメッセンジャーを設定します。
   */
  void SetupDebugMessenger();

  /**
   * @brief ウィンドウに対応するVulkan Surfaceを生成します。
   */
  void CreateSurface();

  /**
   * @brief 要件を満たす物理デバイス（GPU）を列挙・選択します。
   *
   * @throw std::runtime_error Vulkan対応GPUまたは必須キューファミリーを持つGPUが見つからない場合。
   */
  void PickPhysicalDevice();

  /**
   * @brief 選択された物理デバイスから論理デバイスおよびキューを生成します。
   *
   * @throw std::runtime_error 必須キューファミリーが取得できない場合。
   */
  void CreateLogicalDevice();

  /**
   * @brief スワップチェーンを生成し、関連するイメージを取得します。
   *
   * @throw std::runtime_error 必須キューファミリーが取得できない場合。
   */
  void CreateSwapchain();

  /**
   * @brief スワップチェーンイメージごとにImageView（描画・参照用ビュー）を生成します。
   */
  void CreateImageViews();

  /**
   * @brief レンダーパス（描画アタッチメントの設定・サブパス・レイアウト遷移）を生成します。
   *
   * カラーアタッチメント（スワップチェーンフォーマット）、サブパス、および
   * スワップチェーン画像が利用可能になるまで待機するサブパス依存関係を設定します。
   * なお、Vulkan 1.3ではDynamic RenderingによってRenderPassオブジェクトを省略する手法も存在します。
   */
  void CreateRenderPass();

  /**
   * @brief 指定した物理デバイスがアプリケーションの要件（キューファミリー、拡張機能、スワップチェーンサポート）を満たしているかを検証します。
   *
   * @param device 検証対象の物理デバイス。
   * @return 要件を満たしている場合は true、そうでない場合は false。
   */
  [[nodiscard]] bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device) const;

  /**
   * @brief 物理デバイスがサポートするキューファミリーの中から、グラフィックスとプレゼンテーションに必要なインデックスを検索します。
   *
   * @param device 検索対象の物理デバイス。
   * @return 検出されたキューファミリーのインデックス情報。
   */
  [[nodiscard]] QueueFamilyIndices FindQueueFamilies(const vk::raii::PhysicalDevice& device) const;

  /**
   * @brief 物理デバイスが必要な拡張機能（VK_KHR_swapchain等）をすべてサポートしているかを判定します。
   *
   * @param device 検証対象の物理デバイス。
   * @return すべての必須拡張機能がサポートされている場合は true、そうでない場合は false。
   */
  [[nodiscard]] static bool CheckDeviceExtensionSupport(const vk::raii::PhysicalDevice& device);

  /**
   * @brief 指定した物理デバイスと現在のSurfaceにおけるスワップチェーンサポート状況（機能・フォーマット・プレゼントモード）を問い合わせます。
   *
   * @param device 問い合わせ対象の物理デバイス。
   * @return スワップチェーンサポート情報の構造体。
   */
  [[nodiscard]] SwapchainSupportDetails QuerySwapchainSupport(const vk::raii::PhysicalDevice& device) const;

  /**
   * @brief 利用可能なサーフェスフォーマットの中から、最適なカラーフォーマットと色空間を選択します。
   *
   * @param available_formats 利用可能なサーフェスフォーマットのリスト。
   * @return 選択されたサーフェスフォーマット（推奨: B8G8R8A8_SRGB かつ SRGB_NONLINEAR）。
   */
  [[nodiscard]] static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(
      const std::vector<vk::SurfaceFormatKHR>& available_formats);

  /**
   * @brief 利用可能なプレゼンテーションモードの中から、最適なモードを選択します。
   *
   * @param available_present_modes 利用可能なプレゼンテーションモードのリスト。
   * @return 選択されたプレゼンテーションモード（優先: Mailbox、フォールバック: Fifo）。
   */
  [[nodiscard]] static vk::PresentModeKHR ChooseSwapPresentMode(
      const std::vector<vk::PresentModeKHR>& available_present_modes);

  /**
   * @brief Surfaceのケーパビリティおよび現在のウィンドウサイズから、最適なスワップチェーン解像度（Extent）を決定します。
   *
   * @param capabilities Surfaceのケーパビリティ情報。
   * @return 決定された解像度 (vk::Extent2D)。
   */
  [[nodiscard]] vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const;

  /// @brief 描画対象ウィンドウへの非所有参照。破棄順序の制約は呼び出し側の変数宣言順で保証します。
  Window& window_;

  /// @brief Validation Layerが利用可能かどうか。CreateInstance()で判定し、SetupDebugMessenger()でも参照します（Debugビルドのみ有効）。
  bool validation_available_ = false;

  /// @brief Vulkan関数ポインタの動的ロード・初期化を管理するRAIIコンテキスト。
  vk::raii::Context context_;
  vk::raii::Instance instance_ = nullptr;
  /// @brief Validation Layerからのログを受け取るデバッグメッセンジャー（Debugビルドかつレイヤー利用可能時のみ生成）。
  std::optional<vk::raii::DebugUtilsMessengerEXT> debug_messenger_;
  vk::raii::SurfaceKHR surface_ = nullptr;
  vk::raii::PhysicalDevice physical_device_ = nullptr;
  vk::raii::Device device_ = nullptr;
  vk::raii::Queue graphics_queue_ = nullptr;
  vk::raii::Queue present_queue_ = nullptr;
  vk::raii::SwapchainKHR swapchain_ = nullptr;
  /// @brief スワップチェーンから取得したイメージ群。スワップチェーン自身が破棄責務を持つため生ハンドルで保持します。
  std::vector<vk::Image> swapchain_images_;
  vk::Format swapchain_image_format_ = vk::Format::eUndefined;
  vk::Extent2D swapchain_extent_;
  /// @brief スワップチェーンイメージを参照・操作するためのImageViewのリスト。
  std::vector<vk::raii::ImageView> swapchain_image_views_;
  /// @brief 描画パス（アタッチメント・サブパス・同期設定）を定義するレンダーパス。
  vk::raii::RenderPass render_pass_ = nullptr;
};
