#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include <chrono>

// CPUBranch: BFS depth in the last float (bfsDepth). 
// We'll do overlapping BFS growth so the trunk keeps extending while 
// child branches appear, etc.
struct CPUBranch
{
    float startX, startY, startZ;
    float endX, endY, endZ;
    float radius;
    float bfsDepth; // BFS level
};

class VulkanRaymarchApp
{
public:
    VulkanRaymarchApp(uint32_t width, uint32_t height, const std::string& title);
    ~VulkanRaymarchApp();

    void run();

private:
    // Window
    void initWindow();
    void mainLoop();
    void cleanupWindow();

    // Vulkan
    void initVulkan();
    void createInstance();                // <-- was missing
    void setupDebugMessenger();           // <-- was missing
    void pickPhysicalDevice();            // <-- was missing
    void createLogicalDevice();           // <-- was missing
    void createSurface();                 // <-- was missing
    void createSwapChain();               // <-- was missing
    void createSwapChainImageViews();     // <-- was missing
    void createCommandPool();             // <-- was missing
    void createComputeResources();        // <-- was missing
    void createStorageImage();            // <-- was missing
    void createDescriptorSetLayout();     // <-- was missing
    void createComputePipeline();         // <-- was missing
    void createDescriptorPoolAndSets();   // <-- was missing
    void createCommandBuffers();          // <-- was missing
    void createSyncObjects();             // <-- was missing
    void cleanupSwapChain();
    void recreateSwapChain();

    // BFS generation
    std::vector<CPUBranch> generateRandomBFSSystem();

    // GPU buffer for branches
    void createBranchBuffer(const std::vector<CPUBranch>& branches,
        VkBuffer& outBuffer,
        VkDeviceMemory& outMemory,
        uint32_t& numBranches);

    void updateDescriptorSetsWithBranchBuffers();

    // 5-second BFS overlap cycle
    void checkAnimation();

    // main render
    void drawFrame();
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    void updateUniforms();

    // utility
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user);

    VkResult createDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);

    void destroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator);

private:
    // Basic fields
    uint32_t m_width, m_height;
    std::string m_windowTitle;
    GLFWwindow* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = 0;
    uint32_t m_presentQueueFamily = 0;
    uint32_t m_computeQueueFamily = 0;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapChainImages;
    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapChainExtent;
    std::vector<VkImageView> m_swapChainImageViews;

    VkImage m_storageImage = VK_NULL_HANDLE;
    VkDeviceMemory m_storageImageMemory = VK_NULL_HANDLE;
    VkImageView m_storageImageView = VK_NULL_HANDLE;

    std::vector<CPUBranch> m_cpuBranchesOld;
    uint32_t m_numBranchesOld = 0;
    VkBuffer m_branchBufferOld = VK_NULL_HANDLE;
    VkDeviceMemory m_branchBufferOldMem = VK_NULL_HANDLE;

    std::vector<CPUBranch> m_cpuBranchesNew;
    uint32_t m_numBranchesNew = 0;
    VkBuffer m_branchBufferNew = VK_NULL_HANDLE;
    VkDeviceMemory m_branchBufferNewMem = VK_NULL_HANDLE;

    float m_maxBFS = 0.f; // BFS maximum for continuous overlap

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    size_t m_currentFrame = 0;
    bool m_framebufferResized = false;

    float m_cycleStart = 0.f;
    float m_alpha = 0.f;

    std::chrono::steady_clock::time_point m_startTime;

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif
};
