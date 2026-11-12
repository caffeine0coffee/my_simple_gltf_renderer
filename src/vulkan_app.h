#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <filesystem>
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

  /**
   * @brief デストラクタ。GPUの実行完了（device_.waitIdle()）を待機してからリソースを破棄します。
   */
  ~VulkanApp();

  /**
   * @brief 1フレーム分の描画処理を実行します。
   *
   * 前フレームのGPU完了待機、スワップチェーンイメージの取得、コマンドバッファの記録、
   * キューへの送信（submit）、画面へのプレゼンテーション（present）を行います。
   */
  void DrawFrame();

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
   * @brief スワップチェーンおよびそれに依存するリソース（Framebuffer、ImageView）を依存関係の逆順で破棄します。
   */
  void CleanupSwapchain();

  /**
   * @brief ウィンドウリサイズ時等にスワップチェーンと関連リソース一式を再生成します。
   *
   * ウィンドウ最小化中（サイズ0x0）は復元されるまで待機し、GPUのアイドル待機と古いリソースの
   * 破棄を行った上で、Swapchain、ImageView、Framebuffer、renderFinishedセマフォを再作成します。
   */
  void RecreateSwapchain();

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
   * @brief グラフィックスパイプラインおよびパイプラインレイアウトを生成します。
   *
   * 頂点・フラグメントシェーダーのSPIR-Vを読み込んでモジュールを作成し、
   * ビューポート/シザー動的状態、入力アセンブリ、ラスタライザー、マルチサンプリング、
   * カラーブレンディングなどの固定機能パイプライン状態を設定してパイプラインを構築します。
   */
  void CreateGraphicsPipeline();

  /**
   * @brief スワップチェーンの各ImageViewに対応するフレームバッファを生成します。
   *
   * レンダーパスで定義されたアタッチメント（カラーバッファ）に実際のスワップチェーンImageViewを
   * バインドし、描画先ターゲットとして利用可能にします。
   */
  void CreateFramebuffers();

  /**
   * @brief コマンドバッファの確保元となるコマンドプールを生成します。
   *
   * コマンドプールは特定のキューファミリーに紐づくため、描画コマンドを
   * 送信するグラフィックスキューファミリーを指定して作成します。
   *
   * @throw std::runtime_error 必須キューファミリーが取得できない場合。
   */
  void CreateCommandPool();

  /**
   * @brief コマンドプールから、同時に処理中（in-flight）となりうるフレーム数分のコマンドバッファを確保します。
   */
  void CreateCommandBuffers();

  /**
   * @brief 同期オブジェクト（セマフォ・フェンス）を生成します。
   *
   * in-flightフレーム数分の imageAvailable セマフォと inFlight フェンス、
   * およびスワップチェーンイメージ数分の renderFinished セマフォを生成します。
   */
  void CreateSyncObjects();

  /**
   * @brief 指定したスワップチェーンイメージへ三角形を描画するコマンドをコマンドバッファに記録します。
   *
   * レンダーパス開始 → パイプラインのバインド → ビューポート/シザー設定 → 描画 → レンダーパス終了
   * の順にコマンドを記録します。記録のみを行い、キューへの送信（submit）は行いません。
   *
   * @param command_buffer 記録先のコマンドバッファ（リセット済み、または未記録の状態であること）。
   * @param image_index 描画先となるスワップチェーンイメージ（フレームバッファ）のインデックス。
   */
  void RecordCommandBuffer(const vk::raii::CommandBuffer& command_buffer, uint32_t image_index) const;

  /**
   * @brief SPIR-Vバイトコードからシェーダーモジュールを生成します。
   *
   * @param code SPIR-Vバイナリのバイト列。
   * @return 生成されたvk::raii::ShaderModule。
   */
  [[nodiscard]] vk::raii::ShaderModule CreateShaderModule(const std::vector<char>& code) const;

  /**
   * @brief 指定したパスからバイナリファイル全体をメモリ上に読み込みます。
   *
   * @param file_path 読み込むファイルのパス。
   * @return 読み込んだバイナリデータのバイト列。
   * @throw std::runtime_error ファイルが開けない場合。
   */
  [[nodiscard]] static std::vector<char> ReadBinaryFile(const std::filesystem::path& file_path);

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

  /// @brief CPUが同時に準備（記録・送信）できるフレームの最大数。
  /// 2にすると、GPUが1フレーム目を描画している間にCPUが2フレーム目のコマンドを記録でき、
  /// CPUとGPUが互いの完了を待つ時間を減らせます。値を増やすほど入力遅延（レイテンシ）は増えます。
  static constexpr uint32_t kMaxFramesInFlight = 2;

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
  /// @brief パイプライン内で使用するシェーダーリソース（Uniform/Push Constants等）のレイアウト定義。
  vk::raii::PipelineLayout pipeline_layout_ = nullptr;
  /// @brief プログラマブルシェーダーおよび固定機能パイプライン状態をカプセル化するグラフィックスパイプライン。
  vk::raii::Pipeline graphics_pipeline_ = nullptr;
  /// @brief 各スワップチェーンイメージビューを描画アタッチメントとしてバインドしたフレームバッファのリスト。
  std::vector<vk::raii::Framebuffer> swapchain_framebuffers_;
  /// @brief コマンドバッファの確保元。コマンドバッファより先に宣言し、後に破棄されるようにします。
  vk::raii::CommandPool command_pool_ = nullptr;
  /// @brief in-flightフレームごとのコマンドバッファ（要素数は kMaxFramesInFlight）。
  /// vk::raii::CommandBuffer は破棄時に確保元のプールへ返却（vkFreeCommandBuffers）するため、
  /// command_pool_ より後に宣言して先に破棄されるようにします。
  std::vector<vk::raii::CommandBuffer> command_buffers_;

  /// @brief スワップチェーン画像が利用可能になるのを待機するためのセマフォ（in-flightフレームごと、要素数は kMaxFramesInFlight）。
  std::vector<vk::raii::Semaphore> image_available_semaphores_;
  /// @brief レンダリング完了を待機するためのセマフォ（スワップチェーンイメージごと、要素数は swapchain_images_.size()）。
  /// Validation Layerの推奨に従い、プレゼンテーション待機中のセマフォ再利用を防ぐためスワップチェーン画像ごとに保持します。
  std::vector<vk::raii::Semaphore> render_finished_semaphores_;
  /// @brief 前フレームのGPU描画完了をCPU側で待機するためのフェンス（in-flightフレームごと、要素数は kMaxFramesInFlight）。
  std::vector<vk::raii::Fence> in_flight_fences_;

  /// @brief 現在処理中のin-flightフレームのインデックス（0 〜 kMaxFramesInFlight - 1）。
  uint32_t current_frame_ = 0;
};
