#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "BFSSystem.hpp"
#include "BVH.hpp"
#include "LSystem3D.hpp"

#include <vector>
#include <string>
#include <chrono>

/*------------------------------------------------------------------------------
   VulkanRaymarchApp – front‑end (window, camera, UI, dataset writer)
------------------------------------------------------------------------------*/
class VulkanRaymarchApp
{
public:
    enum class Mode { Interactive, Dataset };

    /* Interactive constructor */
    VulkanRaymarchApp(uint32_t width,
        uint32_t height,
        const std::string& title);

    /* Dataset constructor – renders <numSamples> hybrids to <outDir> */
    VulkanRaymarchApp(uint32_t width,
        uint32_t height,
        const std::string& outDir,
        uint32_t numSamples);

    ~VulkanRaymarchApp();

    void run();                     /* master loop */

private:
    /* ---------- window / input ---------- */
    void initWindow();
    void cleanupWindow();
    void interactiveLoop();
    GLFWwindow* m_window = nullptr;
    bool        m_fbResized = false;

    /* ---------- dataset mode ----------- */
    void datasetLoop();
    void captureFrameToPNG(const std::string& fileName);
    void makeDatasetDirs(uint32_t sampleIdx);

    /* ---------- plant generation ---------- */
    void maybeRegeneratePlant(bool force = false);
    void uploadBVH(const BuiltBVH&);
    void createBranchBuffer(const std::vector<CPUBranch>& src,
        VkBuffer& buf, VkDeviceMemory& mem,
        uint32_t& count);
    void updateDescriptorSetsWithBranchBuffer();

    BuiltBVH m_cachedBVH;
    std::vector<CPUBranch>   m_cpuBranches;
    uint32_t                 m_numBranches = 0;
    float                    m_maxBFS = 0.f;

    /* ---------- Vulkan initialisation / plumbing ----------
       (all definitions live in VulkanBackend.cpp)            */
    void initVulkan();
    void createInstance();          void setupDebugMessenger();
    void pickPhysicalDevice();      void createLogicalDevice();
    void createSurface();           void createSwapChain();
    void createSwapChainImageViews();
    void createCommandPool();       void createComputeResources();
    void createStorageImage();      void createDescriptorSetLayout();
    void createComputePipeline();   void createDescriptorPoolAndSets();
    void createCommandBuffers();    void createSyncObjects();
    void cleanupSwapChain();        void recreateSwapChain();

    /* ---------- per‑frame ---------- */
    void drawFrame();                      /* interactive */
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

    /* ---------- helpers ---------- */
    bool  checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT*, void*);
    VkResult createDebugUtilsMessengerEXT(VkInstance,
        const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*,
        VkDebugUtilsMessengerEXT*);
    void destroyDebugUtilsMessengerEXT(
        VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks*);

    /* ---------- members ---------- */
    uint32_t     m_width = 0, m_height = 0;
    std::string  m_windowTitle;

    Mode         m_mode = Mode::Interactive;
    std::string  m_datasetDir;
    uint32_t     m_datasetSamples = 0;
    uint32_t     m_datasetIdx = 0;

    /* Vulkan handles */
    VkInstance               m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_device = VK_NULL_HANDLE;

    uint32_t  m_graphicsQFamily = 0, m_presentQFamily = 0, m_computeQFamily = 0;
    VkQueue   m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue   m_presentQueue = VK_NULL_HANDLE;
    VkQueue   m_computeQueue = VK_NULL_HANDLE;

    VkSwapchainKHR           m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapChainImages;
    VkFormat                 m_swapChainFormat{};
    VkExtent2D               m_swapChainExtent{};
    std::vector<VkImageView> m_swapChainViews;

    VkImage        m_storageImage = VK_NULL_HANDLE;
    VkDeviceMemory m_storageMem = VK_NULL_HANDLE;
    VkImageView    m_storageView = VK_NULL_HANDLE;

    /* branch + BVH SSBOs */
    VkBuffer       m_branchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_branchMem = VK_NULL_HANDLE;
    VkBuffer       m_bvhNodeBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_bvhNodeMem = VK_NULL_HANDLE;
    VkBuffer       m_bvhLeafBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_bvhLeafMem = VK_NULL_HANDLE;
    VkBuffer       m_leafIdxBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_leafIdxMem = VK_NULL_HANDLE;

    VkDescriptorSetLayout        m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout             m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline                   m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool             m_descPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descSets;

    VkCommandPool                m_cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_cmdBufs;

    std::vector<VkSemaphore>     m_imgAvailSems;
    std::vector<VkSemaphore>     m_renderDoneSems;
    std::vector<VkFence>         m_inFlight;
    size_t                       m_frameIndex = 0;

    /* camera */
    float  m_camDist = 3.f, m_yaw = 0.f, m_pitch = 15.f;
    float  m_camCenterY = 0.f;
    bool   m_dragging = false;
    double m_lastX = 0, m_lastY = 0;

    // ── live camera state ─────────────────────────────────────────────
    glm::vec3 m_camPos{ 0.0f, 0.0f, 4.0f };   // world‑space eye position
    glm::vec3 m_camR{ 1.0f, 0.0f, 0.0f };   // right   (orthonormal)
    glm::vec3 m_camU{ 0.0f, 1.0f, 0.0f };   // up
    glm::vec3 m_camF{ 0.0f, 0.0f,-1.0f };   // forward (‑view dir)


    /* book‑keeping */
    std::chrono::steady_clock::time_point m_startTime;
    float    m_cycleStart = 0.f;       /* preset auto‑cycle timer */
    size_t   m_speciesIndex = 0;
    bool     m_debugColoring = false;

    /* presets pool (loaded once from presets.json) */
    std::vector<std::pair<std::string, LSystemPreset>> m_presets;
    const std::vector<const char*> m_validationLayers = {
    "VK_LAYER_KHRONOS_validation"
    };

#ifdef NDEBUG
    static constexpr bool kEnableValidation = false;
#else
    static constexpr bool kEnableValidation = true;
#endif
};
