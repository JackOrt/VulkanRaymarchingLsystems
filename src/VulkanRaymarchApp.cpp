#include "VulkanRaymarchApp.hpp"

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <set>
#include <cmath>
#include <algorithm>
#include <random> // BFS expansions

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

//-----------------------------------------------------------
// readFile => load SPIR-V
//-----------------------------------------------------------
static std::vector<char> readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

//===========================================================
// BFS generation => store BFS depth in CPUBranch::bfsDepth
// We'll define child branches up to depth=5
//===========================================================
std::vector<CPUBranch> VulkanRaymarchApp::generateRandomBFSSystem()
{
    std::vector<CPUBranch> results;
    results.reserve(1024);

    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_real_distribution<float> angleDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> signDist(0.f, 1.f);
    std::uniform_real_distribution<float> branchProb(0.f, 1.f);

    struct Node {
        float sx, sy, sz;
        float ex, ey, ez;
        float radius;
        float bfsD;
        int depth;
    };
    std::vector<Node> stack;
    stack.reserve(512);

    // trunk BFS=0
    stack.push_back({ 0.f,-1.f,0.f, 0.f,0.f,0.f, 0.06f, 0.f, 5 });

    auto rotZ = [&](float& x, float& y, float a) {
        float c = cos(a), s = sin(a);
        float rx = c * x - s * y;
        float ry = s * x + c * y;
        x = rx; y = ry;
        };
    auto rotX = [&](float& y, float& z, float a) {
        float c = cos(a), s = sin(a);
        float ry = c * y - s * z;
        float rz = s * y + c * z;
        y = ry; z = rz;
        };

    while (!stack.empty()) {
        auto n = stack.back();
        stack.pop_back();

        CPUBranch br{};
        br.startX = n.sx; br.startY = n.sy; br.startZ = n.sz;
        br.endX = n.ex; br.endY = n.ey;   br.endZ = n.ez;
        br.radius = n.radius;
        br.bfsDepth = n.bfsD;
        results.push_back(br);

        if (n.depth > 1) {
            float p = branchProb(rng);
            int childCount = (p < 0.3f) ? 1 : 2;

            float vx = n.ex - n.sx;
            float vy = n.ey - n.sy;
            float vz = n.ez - n.sz;
            float length = std::sqrt(vx * vx + vy * vy + vz * vz);
            if (length < 1e-6f) continue;
            vx /= length; vy /= length; vz /= length;

            float childLen = 0.8f * length; // bigger for visibility
            float newRad = n.radius * 0.7f;

            for (int i = 0;i < childCount;i++) {
                float a1 = angleDist(rng);
                float a2 = angleDist(rng);
                if (signDist(rng) > 0.5f) a1 = -a1;
                if (signDist(rng) > 0.5f) a2 = -a2;

                float cx = vx, cy = vy, cz = vz;
                rotZ(cx, cy, a1);
                rotX(cy, cz, a2);

                Node child;
                child.sx = n.ex; child.sy = n.ey; child.sz = n.ez;
                child.ex = n.ex + cx * childLen;
                child.ey = n.ey + cy * childLen;
                child.ez = n.ez + cz * childLen;
                child.radius = newRad;
                child.bfsD = n.bfsD + 1.f;
                child.depth = n.depth - 1;
                stack.push_back(child);
            }
        }
    }
    return results;
}

//===========================================================
// createBranchBuffer => 8 floats each
//===========================================================
void VulkanRaymarchApp::createBranchBuffer(const std::vector<CPUBranch>& branches,
    VkBuffer& outBuffer,
    VkDeviceMemory& outMemory,
    uint32_t& numBranches)
{
    std::vector<CPUBranch> safe = branches;
    if (safe.empty()) {
        safe.resize(1);
    }
    numBranches = (uint32_t)safe.size();
    VkDeviceSize bufSize = numBranches * (8 * sizeof(float));

    if (outBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
    }
    if (outMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, outMemory, nullptr);
        outMemory = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bufSize;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bci, nullptr, &outBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create branch buffer");
    }
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, outBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReq.size;

    bool found = false;
    for (uint32_t i = 0;i < mp.memoryTypeCount;i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            ((mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
        {
            mai.memoryTypeIndex = i;
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("No suitable memory type for branch buffer!");
    if (vkAllocateMemory(m_device, &mai, nullptr, &outMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to alloc branch buffer mem");
    }
    if (vkBindBufferMemory(m_device, outBuffer, outMemory, 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to bind branch buffer mem");
    }

    // upload
    void* dataPtr = nullptr;
    vkMapMemory(m_device, outMemory, 0, bufSize, 0, &dataPtr);
    float* fPtr = (float*)dataPtr;
    for (auto& b : safe) {
        *fPtr++ = b.startX;
        *fPtr++ = b.startY;
        *fPtr++ = b.startZ;
        *fPtr++ = b.radius;
        *fPtr++ = b.endX;
        *fPtr++ = b.endY;
        *fPtr++ = b.endZ;
        *fPtr++ = b.bfsDepth;
    }
    vkUnmapMemory(m_device, outMemory);
}

void VulkanRaymarchApp::updateDescriptorSetsWithBranchBuffers()
{
    for (auto ds : m_descriptorSets) {
        VkDescriptorBufferInfo oldBI{};
        oldBI.buffer = m_branchBufferOld;
        oldBI.offset = 0;
        oldBI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo newBI{};
        newBI.buffer = m_branchBufferNew;
        newBI.offset = 0;
        newBI.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet w1{};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = ds;
        w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w1.descriptorCount = 1;
        w1.pBufferInfo = &oldBI;

        VkWriteDescriptorSet w2{};
        w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w2.dstSet = ds;
        w2.dstBinding = 2;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w2.descriptorCount = 1;
        w2.pBufferInfo = &newBI;

        VkWriteDescriptorSet wr[2] = { w1,w2 };
        vkUpdateDescriptorSets(m_device, 2, wr, 0, nullptr);
    }
}

//===========================================================
// checkAnimation => each 5-second BFS cycle
// We'll do overlapping BFS growth in the shader
// also compute BFS max
//===========================================================
void VulkanRaymarchApp::checkAnimation()
{
    float now = (float)std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_startTime).count();
    float elapsed = now - m_cycleStart;
    const float DURATION = 5.f;

    if (elapsed >= DURATION) {
        m_cycleStart = now;
        elapsed = 0.f;

        // old => empty
        m_cpuBranchesOld.clear();
        createBranchBuffer(m_cpuBranchesOld, m_branchBufferOld, m_branchBufferOldMem, m_numBranchesOld);

        // new => BFS
        m_cpuBranchesNew = generateRandomBFSSystem();
        createBranchBuffer(m_cpuBranchesNew, m_branchBufferNew, m_branchBufferNewMem, m_numBranchesNew);

        updateDescriptorSetsWithBranchBuffers();
    }

    m_alpha = std::min(elapsed / DURATION, 1.0f);

    // BFS max
    float maxB = 0.f;
    for (auto& b : m_cpuBranchesNew) {
        if (b.bfsDepth > maxB) maxB = b.bfsDepth;
    }
    m_maxBFS = maxB;
}

//===========================================================
// Constructor / Destructor
//===========================================================
VulkanRaymarchApp::VulkanRaymarchApp(uint32_t width, uint32_t height, const std::string& title)
    : m_width(width), m_height(height), m_windowTitle(title)
{
    initWindow();
    initVulkan();
    m_startTime = std::chrono::steady_clock::now();

    // old => empty
    m_cpuBranchesOld.clear();
    createBranchBuffer(m_cpuBranchesOld, m_branchBufferOld, m_branchBufferOldMem, m_numBranchesOld);

    // new => BFS final
    m_cpuBranchesNew = generateRandomBFSSystem();
    createBranchBuffer(m_cpuBranchesNew, m_branchBufferNew, m_branchBufferNewMem, m_numBranchesNew);

    updateDescriptorSetsWithBranchBuffers();

    m_cycleStart = 0.f;
    m_alpha = 0.f;
    m_maxBFS = 0.f;
}

VulkanRaymarchApp::~VulkanRaymarchApp()
{
    vkDeviceWaitIdle(m_device);

    cleanupSwapChain();

    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    vkDestroyPipeline(m_device, m_computePipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_computePipelineLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    if (m_branchBufferOld != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_branchBufferOld, nullptr);
        vkFreeMemory(m_device, m_branchBufferOldMem, nullptr);
    }
    if (m_branchBufferNew != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_branchBufferNew, nullptr);
        vkFreeMemory(m_device, m_branchBufferNewMem, nullptr);
    }

    vkDestroyImage(m_device, m_storageImage, nullptr);
    vkDestroyImageView(m_device, m_storageImageView, nullptr);
    vkFreeMemory(m_device, m_storageImageMemory, nullptr);

    vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    for (size_t i = 0;i < m_swapChainImages.size();i++) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    vkDestroyDevice(m_device, nullptr);

    if (m_enableValidationLayers) {
        destroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);

    cleanupWindow();
}

//===========================================================
// run-> mainLoop
//===========================================================
void VulkanRaymarchApp::run()
{
    mainLoop();
}

void VulkanRaymarchApp::mainLoop()
{
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        drawFrame();
    }
    vkDeviceWaitIdle(m_device);
}

//===========================================================
// Window
//===========================================================
void VulkanRaymarchApp::initWindow()
{
    if (!glfwInit()) {
        throw std::runtime_error("failed to init GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(m_width, m_height, m_windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) {
        throw std::runtime_error("failed to create GLFW window");
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* wnd, int w, int h) {
        auto app = reinterpret_cast<VulkanRaymarchApp*>(glfwGetWindowUserPointer(wnd));
        app->m_framebufferResized = true;
        });
}

void VulkanRaymarchApp::cleanupWindow()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

//===========================================================
// initVulkan => calls all the missing methods
//===========================================================
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

//===========================================================
// drawFrame => BFS approach
//===========================================================
void VulkanRaymarchApp::drawFrame()
{
    checkAnimation();

    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    uint32_t imageIndex;
    VkResult r = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSem[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = waitSem;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSem[] = { m_renderFinishedSemaphores[m_currentFrame] };
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = signalSem;

    if (vkQueueSubmit(m_computeQueue, 1, &si, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit compute queue");
    }

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = signalSem;
    VkSwapchainKHR scArr[] = { m_swapChain };
    pi.swapchainCount = 1;
    pi.pSwapchains = scArr;
    pi.pImageIndices = &imageIndex;

    r = vkQueuePresentKHR(m_presentQueue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapChain();
    }
    else if (r != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    m_currentFrame = (m_currentFrame + 1) % m_swapChainImages.size();
}

//===========================================================
// recordCommandBuffer => we read BFS data, do compute pass
//===========================================================
void VulkanRaymarchApp::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // 1) storage => GENERAL
    {
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = m_storageImage;
        bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bar.subresourceRange.levelCount = 1;
        bar.subresourceRange.layerCount = 1;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    // 2) bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_computePipelineLayout, 0, 1,
        &m_descriptorSets[imageIndex],
        0, nullptr);

    updateUniforms();

    float now = (float)std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_startTime).count();

    // row0 => (time, alpha, width, height)
    // row1 => (#old, #new, maxBFS, 0)
    float pushC[8] = {
        now, m_alpha,
        (float)m_swapChainExtent.width, (float)m_swapChainExtent.height,
        (float)m_numBranchesOld, (float)m_numBranchesNew,
        m_maxBFS, 0.f
    };
    vkCmdPushConstants(cmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(pushC), pushC);

    uint32_t gx = (m_swapChainExtent.width + 7) / 8;
    uint32_t gy = (m_swapChainExtent.height + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    // 3) => TRANSFER_SRC
    {
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = m_storageImage;
        bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bar.subresourceRange.levelCount = 1;
        bar.subresourceRange.layerCount = 1;
        bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    // 4) => TRANSFER_DST
    {
        VkImage swapImg = m_swapChainImages[imageIndex];
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = swapImg;
        bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bar.subresourceRange.levelCount = 1;
        bar.subresourceRange.layerCount = 1;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    // 5) copy
    {
        VkImageCopy ic{};
        ic.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ic.srcSubresource.layerCount = 1;
        ic.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ic.dstSubresource.layerCount = 1;
        ic.extent.width = m_swapChainExtent.width;
        ic.extent.height = m_swapChainExtent.height;
        ic.extent.depth = 1;
        vkCmdCopyImage(cmd,
            m_storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &ic);
    }

    // 6) => PRESENT_SRC
    {
        VkImage swapImg = m_swapChainImages[imageIndex];
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = swapImg;
        bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bar.subresourceRange.levelCount = 1;
        bar.subresourceRange.layerCount = 1;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = 0;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    vkEndCommandBuffer(cmd);
}

void VulkanRaymarchApp::updateUniforms()
{
    // push constants only
}

//==========================================================
// cleanupSwapChain-> recreateSwapChain
//==========================================================
void VulkanRaymarchApp::cleanupSwapChain()
{
    for (auto iv : m_swapChainImageViews) {
        vkDestroyImageView(m_device, iv, nullptr);
    }
    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
}

void VulkanRaymarchApp::recreateSwapChain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(m_window, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(m_device);

    cleanupSwapChain();
    createSwapChain();
    createSwapChainImageViews();

    vkDestroyImage(m_device, m_storageImage, nullptr);
    vkDestroyImageView(m_device, m_storageImageView, nullptr);
    vkFreeMemory(m_device, m_storageImageMemory, nullptr);
    createStorageImage();

    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    createDescriptorPoolAndSets();
    updateDescriptorSetsWithBranchBuffers();

    vkFreeCommandBuffers(m_device, m_commandPool, (uint32_t)m_commandBuffers.size(),
        m_commandBuffers.data());
    createCommandBuffers();
}

//==========================================================
// The standard missing Vulkan methods
//==========================================================
bool VulkanRaymarchApp::checkValidationLayerSupport()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    for (auto& req : m_validationLayers) {
        bool found = false;
        for (auto& l : layers) {
            if (std::strcmp(l.layerName, req) == 0) {
                found = true; break;
            }
        }
        if (!found) return false;
    }
    return true;
}

std::vector<const char*> VulkanRaymarchApp::getRequiredExtensions()
{
    uint32_t glfwCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> ret(glfwExt, glfwExt + glfwCount);
    if (m_enableValidationLayers) {
        ret.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return ret;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRaymarchApp::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* user)
{
    std::cerr << "[Validation] " << data->pMessage << "\n";
    return VK_FALSE;
}

VkResult VulkanRaymarchApp::createDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VulkanRaymarchApp::destroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT dbg,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func) {
        func(instance, dbg, pAllocator);
    }
}

void VulkanRaymarchApp::createInstance()
{
    if (m_enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested but not available!");
    }

    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Continuous BFS Overlap Growth";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName = "NoEngine";
    ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_2;

    auto exts = getRequiredExtensions();

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    if (m_enableValidationLayers) {
        ci.enabledLayerCount = (uint32_t)m_validationLayers.size();
        ci.ppEnabledLayerNames = m_validationLayers.data();
    }
    else {
        ci.enabledLayerCount = 0;
    }

    VkDebugUtilsMessengerCreateInfoEXT dbg{};
    if (m_enableValidationLayers) {
        dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = debugCallback;
        ci.pNext = &dbg;
    }

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan instance");
    }
}

void VulkanRaymarchApp::setupDebugMessenger()
{
    if (!m_enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;

    if (createDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger");
    }
}

void VulkanRaymarchApp::pickPhysicalDevice()
{
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &devCount, nullptr);
    if (devCount == 0) {
        throw std::runtime_error("No GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devs(devCount);
    vkEnumeratePhysicalDevices(m_instance, &devCount, devs.data());

    for (auto d : devs) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());
        bool foundCompute = false, foundGraphics = false, foundPresent = false;
        int i = 0;
        for (auto& qp : qProps) {
            if (!foundCompute && (qp.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                foundCompute = true; m_computeQueueFamily = i;
            }
            if (!foundGraphics && (qp.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                foundGraphics = true; m_graphicsQueueFamily = i;
            }
            VkBool32 pres = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &pres);
            if (pres && !foundPresent) {
                foundPresent = true; m_presentQueueFamily = i;
            }
            i++;
        }
        if (foundCompute && foundGraphics && foundPresent) {
            m_physicalDevice = d;
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU");
    }
}

void VulkanRaymarchApp::createLogicalDevice()
{
    std::set<uint32_t> fams{ m_graphicsQueueFamily,m_presentQueueFamily,m_computeQueueFamily };
    float qp = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qCIs;
    for (auto f : fams) {
        VkDeviceQueueCreateInfo dqi{};
        dqi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        dqi.queueFamilyIndex = f;
        dqi.queueCount = 1;
        dqi.pQueuePriorities = &qp;
        qCIs.push_back(dqi);
    }

    VkPhysicalDeviceFeatures feats{};
    std::vector<const char*> devExt = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = (uint32_t)qCIs.size();
    di.pQueueCreateInfos = qCIs.data();
    di.pEnabledFeatures = &feats;
    di.enabledExtensionCount = (uint32_t)devExt.size();
    di.ppEnabledExtensionNames = devExt.data();
    if (m_enableValidationLayers) {
        di.enabledLayerCount = (uint32_t)m_validationLayers.size();
        di.ppEnabledLayerNames = m_validationLayers.data();
    }
    else {
        di.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &di, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }
    vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
}

void VulkanRaymarchApp::createSurface()
{
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}

void VulkanRaymarchApp::createSwapChain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(m_window, &w, &h);
        glfwWaitEvents();
    }

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, fmts.data());
    if (fmts.empty()) {
        throw std::runtime_error("No surface formats available");
    }
    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            chosen = f; break;
        }
    }

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, pms.data());
    VkPresentModeKHR chosenPM = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& pm : pms) {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenPM = pm; break;
        }
    }

    VkExtent2D ext;
    if (caps.currentExtent.width != UINT32_MAX) {
        ext = caps.currentExtent;
    }
    else {
        ext.width = (uint32_t)w;
        ext.height = (uint32_t)h;
    }

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) {
        imgCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = m_surface;
    sci.minImageCount = imgCount;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (m_graphicsQueueFamily != m_presentQueueFamily) {
        uint32_t indices[2] = { m_graphicsQueueFamily,m_presentQueueFamily };
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = indices;
    }
    else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = chosenPM;
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imgCount, nullptr);
    m_swapChainImages.resize(imgCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imgCount, m_swapChainImages.data());
    m_swapChainImageFormat = chosen.format;
    m_swapChainExtent = ext;
}

void VulkanRaymarchApp::createSwapChainImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (size_t i = 0;i < m_swapChainImages.size();i++) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_swapChainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = m_swapChainImageFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &ivci, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swapchain image view");
        }
    }
}

void VulkanRaymarchApp::createCommandPool()
{
    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.queueFamilyIndex = m_computeQueueFamily;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &cpi, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void VulkanRaymarchApp::createComputeResources()
{
    createStorageImage();
}

void VulkanRaymarchApp::createStorageImage()
{
    int w = m_swapChainExtent.width;
    int h = m_swapChainExtent.height;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = w;
    ici.extent.height = h;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &ici, nullptr, &m_storageImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create storage image");
    }
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, m_storageImage, &memReq);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReq.size;
    bool found = false;
    for (uint32_t i = 0;i < mp.memoryTypeCount;i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            ((mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            mai.memoryTypeIndex = i;
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("No device local memory for storage image");
    if (vkAllocateMemory(m_device, &mai, nullptr, &m_storageImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate storage image memory");
    }
    if (vkBindImageMemory(m_device, m_storageImage, m_storageImageMemory, 0) != VK_SUCCESS) {
        throw std::runtime_error("failed to bind storage image memory");
    }

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = m_storageImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &ivci, nullptr, &m_storageImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create storage image view");
    }
}

void VulkanRaymarchApp::createDescriptorSetLayout()
{
    // binding=0 => storage image
    // binding=1 => old buffer
    // binding=2 => new buffer
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0;
    b0.descriptorCount = 1;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b1{};
    b1.binding = 1;
    b1.descriptorCount = 1;
    b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b2{};
    b2.binding = 2;
    b2.descriptorCount = 1;
    b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b2.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding binds[3] = { b0,b1,b2 };

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 3;
    ci.pBindings = binds;

    if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }
}

void VulkanRaymarchApp::createComputePipeline()
{
    auto spv = readFile("shaders/raymarch_comp.spv");
    VkShaderModuleCreateInfo smCI{};
    smCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCI.codeSize = spv.size();
    smCI.pCode = reinterpret_cast<const uint32_t*>(spv.data());

    VkShaderModule compMod;
    if (vkCreateShaderModule(m_device, &smCI, nullptr, &compMod) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute shader module");
    }

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = 32; // 2x vec4

    VkPipelineLayoutCreateInfo plCI{};
    plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &m_descriptorSetLayout;
    plCI.pushConstantRangeCount = 1;
    plCI.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plCI, nullptr, &m_computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    VkPipelineShaderStageCreateInfo stgCI{};
    stgCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stgCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stgCI.module = compMod;
    stgCI.pName = "main";

    VkComputePipelineCreateInfo cCI{};
    cCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cCI.stage = stgCI;
    cCI.layout = m_computePipelineLayout;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cCI, nullptr, &m_computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline");
    }
    vkDestroyShaderModule(m_device, compMod, nullptr);
}

void VulkanRaymarchApp::createDescriptorPoolAndSets()
{
    uint32_t scCount = (uint32_t)m_swapChainImages.size();
    if (scCount == 0) throw std::runtime_error("swapchain=0 => can't create desc sets");

    VkDescriptorPoolSize ps[3]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ps[0].descriptorCount = scCount;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[1].descriptorCount = scCount;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[2].descriptorCount = scCount;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 3;
    pi.pPoolSizes = ps;
    pi.maxSets = scCount;

    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }

    m_descriptorSets.resize(scCount);

    std::vector<VkDescriptorSetLayout> layouts(scCount, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPool;
    ai.descriptorSetCount = scCount;
    ai.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(m_device, &ai, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets");
    }

    // for each set => binding=0 => storage image
    for (size_t i = 0;i < m_descriptorSets.size();i++) {
        VkDescriptorImageInfo imInfo{};
        imInfo.imageView = m_storageImageView;
        imInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = m_descriptorSets[i];
        w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w0.descriptorCount = 1;
        w0.pImageInfo = &imInfo;

        vkUpdateDescriptorSets(m_device, 1, &w0, 0, nullptr);
    }
}

void VulkanRaymarchApp::createCommandBuffers()
{
    uint32_t scCount = (uint32_t)m_swapChainImages.size();
    m_commandBuffers.resize(scCount);

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = scCount;
    if (vkAllocateCommandBuffers(m_device, &ai, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
}

void VulkanRaymarchApp::createSyncObjects()
{
    uint32_t scCount = (uint32_t)m_swapChainImages.size();
    m_imageAvailableSemaphores.resize(scCount);
    m_renderFinishedSemaphores.resize(scCount);
    m_inFlightFences.resize(scCount);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0;i < scCount;i++) {
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &si, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fi, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create sync objects");
        }
    }
}
