#include "VulkanRaymarchAppInit.hpp"
#include "FileUtils.hpp"

#include <set>
#include <vector>
#include <cstring>
#include <iostream>

/*  NOTE:  This file is 99 % identical to the original boiler-plate you had
           before the refactor – only very small tweaks (push-constant size,
           compute-queue family, etc.) were applied to stay in sync.          */

           /* ------------------------------------------------------------------------- */
           /*  Forward helpers                                                          */
           /* ------------------------------------------------------------------------- */
static VkSurfaceFormatKHR chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& av)
{
    for (const auto& f : av)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
            return f;
    return av[0];
}
static VkPresentModeKHR choosePresentMode(
    const std::vector<VkPresentModeKHR>& av)
{
    for (auto m : av) if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

/* ======================================================================== */
/*  initVulkan – calls the sub-steps in order                               */
/* ======================================================================== */
void initVulkan(VulkanRaymarchApp& A)
{
    createInstance(A);
    setupDebugMessenger(A);
    createSurface(A);
    pickPhysicalDevice(A);
    createLogicalDevice(A);
    createSwapChain(A);
    createSwapChainImageViews(A);
    createCommandPool(A);
    createComputeResources(A);
    createDescriptorSetLayout(A);
    A.createComputePipeline();                 // method lives in main file
    createDescriptorPoolAndSets(A);
    createCommandBuffers(A);
    createSyncObjects(A);
}

/* ======================================================================== */
/*  Instance / validation                                                   */
/* ======================================================================== */
void createInstance(VulkanRaymarchApp& A)
{
    if (A.m_enableValidation && !checkValidationLayerSupport(A))
        throw std::runtime_error("Validation layers unavailable");

    VkApplicationInfo ai{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.pApplicationName = "StaticPlantViewer";
    ai.apiVersion = VK_API_VERSION_1_2;

    auto exts = getRequiredExtensions(A);

    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    if (A.m_enableValidation) {
        ci.enabledLayerCount = (uint32_t)A.m_validationLayers.size();
        ci.ppEnabledLayerNames = A.m_validationLayers.data();
        VkDebugUtilsMessengerCreateInfoEXT dbg{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbg.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = VulkanRaymarchApp::debugCallback;
        ci.pNext = &dbg;
    }
    if (vkCreateInstance(&ci, nullptr, &A.m_instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance");
}
void setupDebugMessenger(VulkanRaymarchApp& A)
{
    if (!A.m_enableValidation) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = VulkanRaymarchApp::debugCallback;
    if (A.createDebugUtilsMessengerEXT(A.m_instance, &ci, nullptr, &A.m_debugMessenger) != VK_SUCCESS)
        throw std::runtime_error("Debug messenger");
}

/* ======================================================================== */
/*  Surface                                                                 */
/* ======================================================================== */
void createSurface(VulkanRaymarchApp& A)
{
    if (glfwCreateWindowSurface(A.m_instance, A.m_window, nullptr, &A.m_surface) != VK_SUCCESS)
        throw std::runtime_error("Window surface");
}

/* ======================================================================== */
/*  Physical device & queue families                                        */
/* ======================================================================== */
void pickPhysicalDevice(VulkanRaymarchApp& A)
{
    uint32_t n; vkEnumeratePhysicalDevices(A.m_instance, &n, nullptr);
    if (n == 0) throw std::runtime_error("No Vulkan-capable GPU");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(A.m_instance, &n, devs.data());

    for (auto d : devs) {
        uint32_t qCount; vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());

        bool c = false, g = false, p = false;
        for (uint32_t i = 0; i < qCount; ++i) {
            if (!c && (qProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                c = true; A.m_computeQFamily = i;
            }
            if (!g && (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                g = true; A.m_graphicsQFamily = i;
            }
            VkBool32 sup = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, A.m_surface, &sup);
            if (sup && !p) { p = true; A.m_presentQFamily = i; }
        }
        if (c && g && p) { A.m_physicalDevice = d; break; }
    }
    if (!A.m_physicalDevice) throw std::runtime_error("No suitable GPU found");
}

/* ======================================================================== */
/*  Logical device                                                          */
/* ======================================================================== */
void createLogicalDevice(VulkanRaymarchApp& A)
{
    std::set<uint32_t> fams{ A.m_computeQFamily,A.m_graphicsQFamily,A.m_presentQFamily };
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qs;
    for (uint32_t f : fams) {
        VkDeviceQueueCreateInfo q{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        q.queueFamilyIndex = f; q.queueCount = 1; q.pQueuePriorities = &prio;
        qs.push_back(q);
    }

    VkPhysicalDeviceFeatures feats{};
    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    ci.queueCreateInfoCount = (uint32_t)qs.size();
    ci.pQueueCreateInfos = qs.data();
    ci.pEnabledFeatures = &feats;
    ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = &ext;
    if (A.m_enableValidation) {
        ci.enabledLayerCount = (uint32_t)A.m_validationLayers.size();
        ci.ppEnabledLayerNames = A.m_validationLayers.data();
    }
    if (vkCreateDevice(A.m_physicalDevice, &ci, nullptr, &A.m_device) != VK_SUCCESS)
        throw std::runtime_error("Logical device");
    vkGetDeviceQueue(A.m_device, A.m_computeQFamily, 0, &A.m_computeQueue);
    vkGetDeviceQueue(A.m_device, A.m_graphicsQFamily, 0, &A.m_graphicsQueue);
    vkGetDeviceQueue(A.m_device, A.m_presentQFamily, 0, &A.m_presentQueue);
}

/* ======================================================================== */
/*  Swap-chain                                                              */
/* ======================================================================== */
void createSwapChain(VulkanRaymarchApp& A)
{
    int w, h; glfwGetFramebufferSize(A.m_window, &w, &h);
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(A.m_physicalDevice, A.m_surface, &caps);

    uint32_t fN; vkGetPhysicalDeviceSurfaceFormatsKHR(A.m_physicalDevice, A.m_surface, &fN, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fN);
    vkGetPhysicalDeviceSurfaceFormatsKHR(A.m_physicalDevice, A.m_surface, &fN, fmts.data());
    auto fmt = chooseSurfaceFormat(fmts);

    uint32_t pN; vkGetPhysicalDeviceSurfacePresentModesKHR(A.m_physicalDevice, A.m_surface, &pN, nullptr);
    std::vector<VkPresentModeKHR> pms(pN);
    vkGetPhysicalDeviceSurfacePresentModesKHR(A.m_physicalDevice, A.m_surface, &pN, pms.data());
    auto pm = choosePresentMode(pms);

    VkExtent2D ext = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent :
        VkExtent2D{ (uint32_t)w,(uint32_t)h };

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface = A.m_surface;
    ci.minImageCount = imgCount;
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (A.m_graphicsQFamily != A.m_presentQFamily) {
        uint32_t idx[2] = { A.m_graphicsQFamily,A.m_presentQFamily };
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = idx;
    }
    else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pm;
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(A.m_device, &ci, nullptr, &A.m_swapChain) != VK_SUCCESS)
        throw std::runtime_error("Swapchain");

    vkGetSwapchainImagesKHR(A.m_device, A.m_swapChain, &imgCount, nullptr);
    A.m_swapChainImages.resize(imgCount);
    vkGetSwapchainImagesKHR(A.m_device, A.m_swapChain, &imgCount, A.m_swapChainImages.data());
    A.m_swapChainFormat = fmt.format;
    A.m_swapChainExtent = ext;
}
void createSwapChainImageViews(VulkanRaymarchApp& A)
{
    A.m_swapChainViews.resize(A.m_swapChainImages.size());
    for (size_t i = 0; i < A.m_swapChainImages.size(); ++i) {
        VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ci.image = A.m_swapChainImages[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = A.m_swapChainFormat;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(A.m_device, &ci, nullptr, &A.m_swapChainViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Image view");
    }
}

/* ======================================================================== */
/*  Command pool                                                            */
/* ======================================================================== */
void createCommandPool(VulkanRaymarchApp& A)
{
    VkCommandPoolCreateInfo ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    ci.queueFamilyIndex = A.m_computeQFamily;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(A.m_device, &ci, nullptr, &A.m_cmdPool) != VK_SUCCESS)
        throw std::runtime_error("CmdPool");
}

/* ======================================================================== */
/*  Compute resources (storage image)                                       */
/* ======================================================================== */
void createComputeResources(VulkanRaymarchApp& A) { createStorageImage(A); }
void createStorageImage(VulkanRaymarchApp& A)
{
    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = { A.m_swapChainExtent.width,A.m_swapChainExtent.height,1 };
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(A.m_device, &ci, nullptr, &A.m_storageImage) != VK_SUCCESS)
        throw std::runtime_error("Storage image");

    VkMemoryRequirements req; vkGetImageMemoryRequirements(A.m_device, A.m_storageImage, &req);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(A.m_physicalDevice, &mp);
    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            idx = i; break;
        }
    }
    if (idx == UINT32_MAX) throw std::runtime_error("Storage memtype");
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size; ai.memoryTypeIndex = idx;
    if (vkAllocateMemory(A.m_device, &ai, nullptr, &A.m_storageMem) != VK_SUCCESS)
        throw std::runtime_error("Storage mem alloc");
    vkBindImageMemory(A.m_device, A.m_storageImage, A.m_storageMem, 0);

    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = A.m_storageImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(A.m_device, &vi, nullptr, &A.m_storageView) != VK_SUCCESS)
        throw std::runtime_error("Storage view");
}

/* ======================================================================== */
/*  Descriptor set layout / pool / sets                                     */
/* ======================================================================== */
void createDescriptorSetLayout(VulkanRaymarchApp& A)
{
    VkDescriptorSetLayoutBinding s0{}, s1{};
    s0.binding = 0; s0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    s0.descriptorCount = 1; s0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    s1.binding = 1; s1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    s1.descriptorCount = 1; s1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding arr[2] = { s0,s1 };
    VkDescriptorSetLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    ci.bindingCount = 2; ci.pBindings = arr;
    if (vkCreateDescriptorSetLayout(A.m_device, &ci, nullptr, &A.m_setLayout) != VK_SUCCESS)
        throw std::runtime_error("SetLayout");
}
void createDescriptorPoolAndSets(VulkanRaymarchApp& A)
{
    uint32_t n = (uint32_t)A.m_swapChainImages.size();
    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  sizes[0].descriptorCount = n;
    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sizes[1].descriptorCount = n;

    VkDescriptorPoolCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    ci.poolSizeCount = 2; ci.pPoolSizes = sizes; ci.maxSets = n;
    if (vkCreateDescriptorPool(A.m_device, &ci, nullptr, &A.m_descPool) != VK_SUCCESS)
        throw std::runtime_error("DescPool");

    A.m_descSets.resize(n);
    std::vector<VkDescriptorSetLayout> layouts(n, A.m_setLayout);
    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = A.m_descPool;
    ai.descriptorSetCount = n;
    ai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(A.m_device, &ai, A.m_descSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Alloc sets");

    for (size_t i = 0; i < n; ++i) {
        VkDescriptorImageInfo ii{ VK_NULL_HANDLE,A.m_storageView,VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = A.m_descSets[i]; w.dstBinding = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.descriptorCount = 1; w.pImageInfo = &ii;
        vkUpdateDescriptorSets(A.m_device, 1, &w, 0, nullptr);
    }
}
void createCommandBuffers(VulkanRaymarchApp& A)
{
    A.m_cmdBufs.resize(A.m_swapChainImages.size());
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = A.m_cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)A.m_cmdBufs.size();
    if (vkAllocateCommandBuffers(A.m_device, &ai, A.m_cmdBufs.data()) != VK_SUCCESS)
        throw std::runtime_error("Cmd buffers");
}
void createSyncObjects(VulkanRaymarchApp& A)
{
    size_t n = A.m_swapChainImages.size();
    A.m_imgAvailSems.resize(n);
    A.m_renderDoneSems.resize(n);
    A.m_inFlight.resize(n);
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < n; ++i) {
        if (vkCreateSemaphore(A.m_device, &si, nullptr, &A.m_imgAvailSems[i]) != VK_SUCCESS ||
            vkCreateSemaphore(A.m_device, &si, nullptr, &A.m_renderDoneSems[i]) != VK_SUCCESS ||
            vkCreateFence(A.m_device, &fi, nullptr, &A.m_inFlight[i]) != VK_SUCCESS)
            throw std::runtime_error("Sync objects");
    }
}

/* ======================================================================== */
/*  Swap-chain recreation / cleanup                                         */
/* ======================================================================== */
void cleanupSwapChain(VulkanRaymarchApp& A)
{
    for (auto v : A.m_swapChainViews) vkDestroyImageView(A.m_device, v, nullptr);
    vkDestroySwapchainKHR(A.m_device, A.m_swapChain, nullptr);
}
void recreateSwapChain(VulkanRaymarchApp& A)
{
    int w, h; glfwGetFramebufferSize(A.m_window, &w, &h);
    while (w == 0 || h == 0) { glfwGetFramebufferSize(A.m_window, &w, &h); glfwWaitEvents(); }
    vkDeviceWaitIdle(A.m_device);

    cleanupSwapChain(A);
    createSwapChain(A);
    createSwapChainImageViews(A);

    vkDestroyImage(A.m_device, A.m_storageImage, nullptr);
    vkDestroyImageView(A.m_device, A.m_storageView, nullptr);
    vkFreeMemory(A.m_device, A.m_storageMem, nullptr);
    createStorageImage(A);

    vkDestroyDescriptorPool(A.m_device, A.m_descPool, nullptr);
    createDescriptorPoolAndSets(A);
    A.updateDescriptorSetsWithBranchBuffer();

    vkFreeCommandBuffers(A.m_device, A.m_cmdPool, (uint32_t)A.m_cmdBufs.size(), A.m_cmdBufs.data());
    createCommandBuffers(A);
}

/* ======================================================================== */
/*  Validation helpers                                                      */
/* ======================================================================== */
bool checkValidationLayerSupport(const VulkanRaymarchApp& A)
{
    uint32_t n; vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> props(n);
    vkEnumerateInstanceLayerProperties(&n, props.data());
    for (auto req : A.m_validationLayers) {
        bool found = false;
        for (auto& p : props) if (std::strcmp(p.layerName, req) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}
std::vector<const char*> getRequiredExtensions(const VulkanRaymarchApp& A)
{
    uint32_t n; const char** ext = glfwGetRequiredInstanceExtensions(&n);
    std::vector<const char*> res(ext, ext + n);
    if (A.m_enableValidation) res.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return res;
}
