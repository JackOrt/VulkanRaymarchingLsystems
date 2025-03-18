#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include "BFSSystem.hpp" // For CPUBranch definition

class VulkanRaymarchApp {
public:
    VulkanRaymarchApp(uint32_t width, uint32_t height, const std::string& title);
    ~VulkanRaymarchApp();

    void run();

private:
    // Window
    void initWindow();
    void mainLoop();
    void cleanupWindow();

    // Vulkan initialization
    void initVulkan();
    void createInstance();
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSurface();
    void createSwapChain();
    void createSwapChainImageViews();
    void createCommandPool();
    void createComputeResources();
    void createStorageImage();
    void createDescriptorSetLayout();
    void createComputePipeline();
    void createDescriptorPoolAndSets();
    void createCommandBuffers();
    void createSyncObjects();
    void cleanupSwapChain();
    void recreateSwapChain();

    // Branch (BFS) generation and buffer handling
    void createBranchBuffer(const std::vector<CPUBranch>& branches,
        VkBuffer& outBuffer,
        VkDeviceMemory& outMemory,
        uint32_t& numBranches);
    void updateDescriptorSetsWithBranchBuffers();

    // Animation: update branch growth (BFS cycle)
    void checkAnimation();

    // Rendering: record commands and update uniforms
    void drawFrame();
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    void updateUniforms();

    // Utility functions
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

    // Swapchain
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapChainImages;
    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapChainExtent;
    std::vector<VkImageView> m_swapChainImageViews;

    // Storage image for compute shader output
    VkImage m_storageImage = VK_NULL_HANDLE;
    VkDeviceMemory m_storageImageMemory = VK_NULL_HANDLE;
    VkImageView m_storageImageView = VK_NULL_HANDLE;

    // Branch buffers
    std::vector<CPUBranch> m_cpuBranchesOld;
    uint32_t m_numBranchesOld = 0;
    VkBuffer m_branchBufferOld = VK_NULL_HANDLE;
    VkDeviceMemory m_branchBufferOldMem = VK_NULL_HANDLE;

    std::vector<CPUBranch> m_cpuBranchesNew;
    uint32_t m_numBranchesNew = 0;
    VkBuffer m_branchBufferNew = VK_NULL_HANDLE;
    VkDeviceMemory m_branchBufferNewMem = VK_NULL_HANDLE;

    float m_maxBFS = 0.f;

    // Pipeline
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // Command buffers
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Synchronization objects
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    size_t m_currentFrame = 0;
    bool m_framebufferResized = false;

    float m_cycleStart = 0.f;
    float m_alpha = 0.f;
    std::chrono::steady_clock::time_point m_startTime;

    // Debug coloring toggle: true = debug coloring; false = normal shading.
    bool m_debugColoring = false;

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif
};
