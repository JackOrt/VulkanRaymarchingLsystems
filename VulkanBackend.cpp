// ????????????????????????????????????????????????????????????????????????
//  vulkanbackend.cpp
//      Low-level Vulkan plumbing pulled out of the gigantic
//      VulkanRaymarchApp.cpp to keep the latter readable.
// ????????????????????????????????????????????????????????????????????????
#include "VulkanBackend.hpp"
#include "FileUtils.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

// ????????????????????????????????????????????????????????????????????????
//  Small helpers that were local lambdas in the old file
// ????????????????????????????????????????????????????????????????????????
namespace
{
    static VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& available)
    {
        for (const auto& f : available)
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
                return f;
        return available.empty() ? VkSurfaceFormatKHR{} : available[0];
    }

    static VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& available)
    {
        for (auto m : available)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                return m;
        return VK_PRESENT_MODE_FIFO_KHR;
    }
} // anonymous namespace


// ======================================================================
//  SECTION 10 : Vulkan initialisation
// ======================================================================

void VulkanRaymarchApp::initVulkan()
{
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createSwapChainImageViews();
    createCommandPool();
    createComputeResources();
    createDescriptorSetLayout();
    createComputePipeline();
    createDescriptorPoolAndSets();
    createCommandBuffers();
    createSyncObjects();
}

// ????????????????????????????????????????????????????????????????????????
//  Instance / validation-layers
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createInstance()
{
    if (kEnableValidation && !checkValidationLayerSupport())
        throw std::runtime_error("Validation layers requested but not available");

    VkApplicationInfo ai{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.pApplicationName = "StaticPlantViewer";
    ai.apiVersion = VK_API_VERSION_1_2;

    auto exts = getRequiredExtensions();

    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    VkDebugUtilsMessengerCreateInfoEXT dbg{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    if (kEnableValidation) {
        ci.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        ci.ppEnabledLayerNames = m_validationLayers.data();

        dbg.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = debugCallback;
        ci.pNext = &dbg;
    }

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");
}

// ????????????????????????????????????????????????????????????????????????
//  Debug-messenger
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::setupDebugMessenger()
{
    if (!kEnableValidation) return;

    VkDebugUtilsMessengerCreateInfoEXT ci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;

    if (createDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger) != VK_SUCCESS)
        throw std::runtime_error("failed to set up debug messenger");
}

// ????????????????????????????????????????????????????????????????????????
//  Surface (GLFW)
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createSurface()
{
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("failed to create window surface");
}

// ????????????????????????????????????????????????????????????????????????
//  Physical device & queue-families
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::pickPhysicalDevice()
{
    uint32_t nDev = 0;
    vkEnumeratePhysicalDevices(m_instance, &nDev, nullptr);
    if (nDev == 0) throw std::runtime_error("No GPUs with Vulkan support");

    std::vector<VkPhysicalDevice> devices(nDev);
    vkEnumeratePhysicalDevices(m_instance, &nDev, devices.data());

    for (auto d : devices)
    {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);

        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());

        bool g = false, c = false, p = false;
        for (uint32_t i = 0; i < qCount; ++i)
        {
            if (!g && (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                g = true; m_graphicsQFamily = i;
            }

            if (!c && (qProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
            {
                c = true; m_computeQFamily = i;
            }

            VkBool32 sup = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &sup);
            if (sup && !p)
            {
                p = true; m_presentQFamily = i;
            }
        }

        if (g && c && p) { m_physicalDevice = d; break; }
    }

    if (!m_physicalDevice)
        throw std::runtime_error("No suitable GPU found");
}

// ????????????????????????????????????????????????????????????????????????
//  Logical device & queues
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createLogicalDevice()
{
    std::set<uint32_t> uniqueFams{ m_graphicsQFamily,
                                   m_computeQFamily,
                                   m_presentQFamily };

    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32_t fam : uniqueFams)
    {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = fam;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        queues.push_back(qi);
    }

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures feats{};
    VkDeviceCreateInfo di{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    di.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    di.pQueueCreateInfos = queues.data();
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = devExts;
    di.pEnabledFeatures = &feats;

    if (kEnableValidation)
    {
        di.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        di.ppEnabledLayerNames = m_validationLayers.data();
    }

    if (vkCreateDevice(m_physicalDevice, &di, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Logical device creation failed");

    vkGetDeviceQueue(m_device, m_graphicsQFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_computeQFamily, 0, &m_computeQueue);
    vkGetDeviceQueue(m_device, m_presentQFamily, 0, &m_presentQueue);
}

// ????????????????????????????????????????????????????????????????????????
//  Swap-chain & image-views
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createSwapChain()
{
    int winW, winH;
    glfwGetFramebufferSize(m_window, &winW, &winH);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_physicalDevice, m_surface, &caps);

    uint32_t fmtN = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &fmtN, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtN);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &fmtN, fmts.data());
    auto chosenFmt = chooseSurfaceFormat(fmts);

    uint32_t pmN = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &pmN, nullptr);
    std::vector<VkPresentModeKHR> modes(pmN);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &pmN, modes.data());
    auto chosenPM = choosePresentMode(modes);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = std::clamp(static_cast<uint32_t>(winW),
            caps.minImageExtent.width,
            caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(winH),
            caps.minImageExtent.height,
            caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface = m_surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosenFmt.format;
    ci.imageColorSpace = chosenFmt.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (m_graphicsQFamily != m_presentQFamily)
    {
        uint32_t indices[2] = { m_graphicsQFamily, m_presentQFamily };
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = indices;
    }
    else
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = chosenPM;
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapChain) != VK_SUCCESS)
        throw std::runtime_error("Swap-chain creation failed");

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(
        m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainFormat = chosenFmt.format;
    m_swapChainExtent = extent;
}

void VulkanRaymarchApp::createSwapChainImageViews()
{
    m_swapChainViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); ++i)
    {
        VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ci.image = m_swapChainImages[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = m_swapChainFormat;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = ci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &ci, nullptr, &m_swapChainViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Swap-chain image-view creation failed");
    }
}

// ????????????????????????????????????????????????????????????????????????
//  Command-pool / compute resources
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createCommandPool()
{
    VkCommandPoolCreateInfo ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    ci.queueFamilyIndex = m_computeQFamily;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &ci, nullptr, &m_cmdPool) != VK_SUCCESS)
        throw std::runtime_error("Command-pool creation failed");
}

void VulkanRaymarchApp::createComputeResources()
{
    createStorageImage();
}

void VulkanRaymarchApp::createStorageImage()
{
    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = { m_swapChainExtent.width,
                       m_swapChainExtent.height,
                       1 };
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &ci, nullptr, &m_storageImage) != VK_SUCCESS)
        throw std::runtime_error("Storage image creation failed");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, m_storageImage, &req);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);

    uint32_t memIdx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            memIdx = i;
            break;
        }
    }
    if (memIdx == UINT32_MAX)
        throw std::runtime_error("Suitable memory type for storage image not found");

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memIdx;

    if (vkAllocateMemory(m_device, &ai, nullptr, &m_storageMem) != VK_SUCCESS)
        throw std::runtime_error("Storage image memory alloc failed");

    vkBindImageMemory(m_device, m_storageImage, m_storageMem, 0);

    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = m_storageImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = vi.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &vi, nullptr, &m_storageView) != VK_SUCCESS)
        throw std::runtime_error("Storage image-view creation failed");
}

// ????????????????????????????????????????????????????????????????????????
//  Descriptor-set layout / compute pipeline
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding b0{}, b1{}, b2{}, b3{};

    // binding 0 – storage image
    b0.binding = 0;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b0.descriptorCount = 1;
    b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 1 – branch SSBO
    b1 = b0;
    b1.binding = 1;
    b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    // binding 2 – BVH nodes
    b2 = b1; b2.binding = 2;

    // binding 3 – BVH leaf-indices
    b3 = b1; b3.binding = 3;

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{ b0,b1,b2,b3 };

    VkDescriptorSetLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_setLayout) != VK_SUCCESS)
        throw std::runtime_error("Descriptor-set layout creation failed");
}

void VulkanRaymarchApp::createComputePipeline()
{
    auto bin = readFile("shaders/raymarch_comp.spv");

    VkShaderModuleCreateInfo smci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = bin.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(bin.data());

    VkShaderModule shader;
    if (vkCreateShaderModule(m_device, &smci, nullptr, &shader) != VK_SUCCESS)
        throw std::runtime_error("Compute shader module creation failed");

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = 96;             // 6 * vec4

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pc;

    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipeLayout) != VK_SUCCESS)
        throw std::runtime_error("Pipeline-layout creation failed");

    VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";

    VkComputePipelineCreateInfo pci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pci.stage = stage;
    pci.layout = m_pipeLayout;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Compute pipeline creation failed");

    vkDestroyShaderModule(m_device, shader, nullptr);
}

// ????????????????????????????????????????????????????????????????????????
//  Descriptor pool + per-image descriptor-sets
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createDescriptorPoolAndSets()
{
    uint32_t swapImages = static_cast<uint32_t>(m_swapChainImages.size());
    if (swapImages == 0) throw std::runtime_error("Swap-chain not initialised");

    VkDescriptorPoolSize sizes[4]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;   // binding 0
    sizes[0].descriptorCount = swapImages;

    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;  // binding 1
    sizes[1].descriptorCount = swapImages;  // branches

    sizes[2] = sizes[1];                   // binding 2 – BVH nodes
    sizes[3] = sizes[1];                   // binding 3 – BVH leaves

    VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pci.poolSizeCount = 4;
    pci.pPoolSizes = sizes;
    pci.maxSets = swapImages;

    if (vkCreateDescriptorPool(m_device, &pci, nullptr, &m_descPool) != VK_SUCCESS)
        throw std::runtime_error("Descriptor-pool creation failed");

    // allocate one set per swap-chain image
    std::vector<VkDescriptorSetLayout> layouts(swapImages, m_setLayout);

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = swapImages;
    ai.pSetLayouts = layouts.data();

    m_descSets.resize(swapImages);
    if (vkAllocateDescriptorSets(m_device, &ai, m_descSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Descriptor-set allocation failed");

    // write binding 0 (storage image) – buffers are patched later
    for (uint32_t i = 0; i < swapImages; ++i)
    {
        VkDescriptorImageInfo ii{};
        ii.imageView = m_storageView;
        ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_descSets[i];
        w.dstBinding = 0;                                  // binding 0
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.descriptorCount = 1;
        w.pImageInfo = &ii;

        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
}

// ????????????????????????????????????????????????????????????????????????
//  Command buffers
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createCommandBuffers()
{
    m_cmdBufs.resize(m_swapChainImages.size());

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = m_cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(m_cmdBufs.size());

    if (vkAllocateCommandBuffers(m_device, &ai, m_cmdBufs.data()) != VK_SUCCESS)
        throw std::runtime_error("Command-buffer allocation failed");
}

// ????????????????????????????????????????????????????????????????????????
//  Synchronisation objects
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::createSyncObjects()
{
    size_t N = m_swapChainImages.size();
    m_imgAvailSems.resize(N);
    m_renderDoneSems.resize(N);
    m_inFlight.resize(N);

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < N; ++i)
    {
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_imgAvailSems[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &si, nullptr, &m_renderDoneSems[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fi, nullptr, &m_inFlight[i]) != VK_SUCCESS)
            throw std::runtime_error("Sync-object creation failed");
    }
}

// ????????????????????????????????????????????????????????????????????????
//  Swap-chain recreation / cleanup
// ????????????????????????????????????????????????????????????????????????
void VulkanRaymarchApp::cleanupSwapChain()
{
    for (auto v : m_swapChainViews)
        vkDestroyImageView(m_device, v, nullptr);

    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
}

void VulkanRaymarchApp::recreateSwapChain()
{
    int w = 0, h = 0;
    do
    {
        glfwGetFramebufferSize(m_window, &w, &h);
        glfwWaitEvents();
    } while (w == 0 || h == 0);

    vkDeviceWaitIdle(m_device);

    cleanupSwapChain();
    createSwapChain();
    createSwapChainImageViews();

    vkDestroyImage(m_device, m_storageImage, nullptr);
    vkDestroyImageView(m_device, m_storageView, nullptr);
    vkFreeMemory(m_device, m_storageMem, nullptr);
    createStorageImage();

    vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    createDescriptorPoolAndSets();
    updateDescriptorSetsWithBranchBuffer();

    vkFreeCommandBuffers(
        m_device, m_cmdPool,
        static_cast<uint32_t>(m_cmdBufs.size()), m_cmdBufs.data());
    createCommandBuffers();
}

// ????????????????????????????????????????????????????????????????????????
//  Validation-layer helpers (unchanged from original section 11)
// ????????????????????????????????????????????????????????????????????????
bool VulkanRaymarchApp::checkValidationLayerSupport()
{
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);

    std::vector<VkLayerProperties> props(n);
    vkEnumerateInstanceLayerProperties(&n, props.data());

    for (auto req : m_validationLayers)
    {
        bool found = false;
        for (auto& p : props)
            if (std::strcmp(p.layerName, req) == 0)
            {
                found = true; break;
            }

        if (!found) return false;
    }
    return true;
}

std::vector<const char*> VulkanRaymarchApp::getRequiredExtensions()
{
    uint32_t glfwCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);

    std::vector<const char*> out(glfwExt, glfwExt + glfwCount);
    if (kEnableValidation)
        out.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return out;
}
