/*****************************************************************************************
 * VulkanRaymarchApp.cpp – front?end logic
 *   • Interactive viewer  (Mode::Interactive)
 *   • Dataset generator   (Mode::Dataset)
 *   • Random cross?breeding on ‘H’ key and in dataset mode
 *****************************************************************************************/
#include "VulkanRaymarchApp.hpp"
#include "FileUtils.hpp"
#include "vulkanbackend.hpp"      // low?level functions (unchanged)
#include "LSystem3D.hpp" 

 /* ---------- std / utility ---------- */
#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <random>

/* ---------- stb_image_write for PNG output ---------- */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

/* ---------- GLM experimental helpers ---------- */
#define GLM_ENABLE_EXPERIMENTAL

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/quaternion.hpp>   
#include <glm/gtx/norm.hpp>


#include <algorithm>
#include <filesystem>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <array>

/* ---------- tiny helpers that fix glm::min/max overload trouble ---------- */
static inline glm::vec3 vmin(glm::vec3 a, glm::vec3 b)
{
    return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
}
static inline glm::vec3 vmax(glm::vec3 a, glm::vec3 b)
{
    return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
}

 /* ????????? helpers ????????? */
static glm::vec3 rotAxis(glm::vec3 v, float a, glm::vec3 ax) {
    return glm::vec3(glm::rotate(glm::mat4(1), a, ax) * glm::vec4(v, 0));
}
static std::mt19937& rng() {
    static thread_local std::mt19937 gen{ std::random_device{}() };
    return gen;
}
/*==============================================================*/
/*              C T O R S  /  D T O R                           */
/*==============================================================*/
VulkanRaymarchApp::VulkanRaymarchApp(uint32_t w, uint32_t h,
    const std::string& title)
    :m_width(w), m_height(h), m_windowTitle(title), m_mode(Mode::Interactive)
{
    m_presets = loadParametricPresets(true);
    initWindow();
    initVulkan();
    maybeRegeneratePlant(true);
    m_startTime = std::chrono::steady_clock::now();
}
VulkanRaymarchApp::VulkanRaymarchApp(uint32_t w, uint32_t h,
    const std::string& outDir,
    uint32_t numSamples)
    :m_width(w), m_height(h),
    m_windowTitle("Dataset Builder"),
    m_mode(Mode::Dataset),
    m_datasetDir(outDir),
    m_datasetSamples(numSamples)
{
    m_presets = loadParametricPresets();
    std::filesystem::create_directories(outDir);
    initWindow();      /* invisible window is fine – off?screen rendering */
    initVulkan();
    maybeRegeneratePlant(true);        /* first sample */
}
VulkanRaymarchApp::~VulkanRaymarchApp()
{
    vkDeviceWaitIdle(m_device);
    cleanupSwapChain();
    if (m_branchBuffer) vkDestroyBuffer(m_device, m_branchBuffer, nullptr);
    if (m_branchMem)    vkFreeMemory(m_device, m_branchMem, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipeLayout, nullptr);
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    vkDestroyImage(m_device, m_storageImage, nullptr);
    vkDestroyImageView(m_device, m_storageView, nullptr);
    vkFreeMemory(m_device, m_storageMem, nullptr);
    vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    for (size_t i = 0; i < m_imgAvailSems.size(); ++i) {
        vkDestroySemaphore(m_device, m_imgAvailSems[i], nullptr);
        vkDestroySemaphore(m_device, m_renderDoneSems[i], nullptr);
        vkDestroyFence(m_device, m_inFlight[i], nullptr);
    }
    vkDestroyDevice(m_device, nullptr);
    destroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    cleanupWindow();
}
/*==============================================================*/
/*                        P U B L I C                           */
/*==============================================================*/
void VulkanRaymarchApp::run()
{
    if (m_mode == Mode::Interactive) interactiveLoop();
    else                           datasetLoop();
}
/*==============================================================*/
/*                  W I N D O W  /  I N P U T                    */
/*==============================================================*/
void VulkanRaymarchApp::initWindow()
{
    glfwInit(); glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_width, m_height,
        m_windowTitle.c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(m_window, this);
    /* framebuffer resize flag */
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int, int) {
        static_cast<VulkanRaymarchApp*>(glfwGetWindowUserPointer(w))->m_fbResized = true; });
    if (m_mode == Mode::Dataset) return;      /* headless ? no extra callbacks */

    /* mouse & key only in interactive mode */
    glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double y) {
        auto* a = static_cast<VulkanRaymarchApp*>(glfwGetWindowUserPointer(w));
        a->m_camDist = std::clamp(a->m_camDist - float(y) * 0.3f, 1.f, 100.f); });
    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int b, int act, int) {
        auto* a = static_cast<VulkanRaymarchApp*>(glfwGetWindowUserPointer(w));
        if (b == GLFW_MOUSE_BUTTON_LEFT) {
            if (act == GLFW_PRESS) { a->m_dragging = true; glfwGetCursorPos(w, &a->m_lastX, &a->m_lastY); }
            else a->m_dragging = false;
        }});
        glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
            auto* a = static_cast<VulkanRaymarchApp*>(glfwGetWindowUserPointer(w));
            if (!a->m_dragging) return;
            double dx = x - a->m_lastX, dy = y - a->m_lastY;
            a->m_lastX = x; a->m_lastY = y;
            const float s = 0.3f;
            a->m_yaw += float(dx) * s;
            a->m_pitch = std::clamp(a->m_pitch - float(dy) * s, -89.f, 89.f); });
        glfwSetKeyCallback(m_window, [](GLFWwindow* w, int key, int, int act, int) {
            if (act != GLFW_PRESS) return;
            auto* a = static_cast<VulkanRaymarchApp*>(glfwGetWindowUserPointer(w));
            if (key == GLFW_KEY_D) a->m_debugColoring = !a->m_debugColoring;
            if (key == GLFW_KEY_C) a->maybeRegeneratePlant(true);
            if (key == GLFW_KEY_H) {   /* random hybrid on H */
                LSystemPreset h = randomHybrid(
                    [&] { std::vector<LSystemPreset> vec;
                for (auto& p : a->m_presets) vec.push_back(p.second); return vec; }(),
                    std::uniform_real_distribution<float>(0.f, 1.f)(rng()));
                a->m_cpuBranches = generateLSystem(h);
                a->maybeRegeneratePlant(true);
            }
            });
}
void VulkanRaymarchApp::cleanupWindow()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}
/*==============================================================*/
void VulkanRaymarchApp::interactiveLoop()
{
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        drawFrame();
    }
    vkDeviceWaitIdle(m_device);
}
/*==============================================================*/
/*              D A T A S E T   L O O P                         */
/*==============================================================*/
void VulkanRaymarchApp::datasetLoop()
{
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    std::vector<LSystemPreset> pool;
    for (auto& p : m_presets) pool.push_back(p.second);

    for (m_datasetIdx = 0; m_datasetIdx < m_datasetSamples; ++m_datasetIdx)
    {
        /* pick a fresh random hybrid ----------------------------------- */
        float alpha = uni(rng());
        LSystemPreset H = randomHybrid(pool, alpha, rng()());
        m_cpuBranches = generateLSystem(H);
        maybeRegeneratePlant(true);

        /* directory …/plant_000## */
        makeDatasetDirs(m_datasetIdx);

        /* six conditioning cameras + one target ------------------------ */
        const float elev[2] = { 20.f,-20.f };
        const float az[3] = { 0.f,120.f,240.f };
        int imgID = 0;
        for (int e = 0; e < 2; ++e)
            for (int a = 0; a < 3; ++a) {
                m_pitch = elev[e]; m_yaw = az[a];
                drawFrame();
                std::ostringstream ss; ss << m_datasetDir << "/plant_" << std::setw(5)
                    << std::setfill('0') << m_datasetIdx << "/cond_" << imgID++ << ".png";
                captureFrameToPNG(ss.str());
            }
        /* target view straight front */
        m_pitch = 0.f; m_yaw = 0.f;
        drawFrame();
        std::ostringstream ss;
        ss << m_datasetDir << "/plant_" << std::setw(5) << std::setfill('0')
            << m_datasetIdx << "/target_0.png";
        captureFrameToPNG(ss.str());

        /* progress */
        std::cout << "[" << m_datasetIdx + 1 << "/" << m_datasetSamples << "] done\n";
    }
    /* write valid_paths.json */
    {
        std::ofstream jf(m_datasetDir + "/valid_paths.json");
        jf << "[\n";
        for (uint32_t i = 0; i < m_datasetSamples; ++i) {
            jf << "  \"plant_" << std::setw(5) << std::setfill('0') << i << "\"";
            if (i + 1 < m_datasetSamples) jf << ",";
            jf << "\n";
        }
        jf << "]\n";
    }
}
/* ============= helpers for dataset output =================== */
void VulkanRaymarchApp::makeDatasetDirs(uint32_t idx)
{
    namespace fs = std::filesystem;
    std::ostringstream ss;
    ss << m_datasetDir << "/plant_" << std::setw(5) << std::setfill('0') << idx;
    fs::create_directories(ss.str());
}
void VulkanRaymarchApp::captureFrameToPNG(const std::string& fileName)
{
    /* read back storage image via a host?visible buffer ---------------- */
    VkDeviceSize bytes = m_swapChainExtent.width * m_swapChainExtent.height * 4;
    VkBuffer staging; VkDeviceMemory stagingMem;

    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = bytes; bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkCreateBuffer(m_device, &bc, nullptr, &staging);
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(m_device, staging, &req);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);
    uint32_t idx = 0; for (; idx < mp.memoryTypeCount; ++idx)
        if ((req.memoryTypeBits & (1u << idx)) &&
            (mp.memoryTypes[idx].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) break;
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size; ai.memoryTypeIndex = idx;
    vkAllocateMemory(m_device, &ai, nullptr, &stagingMem);
    vkBindBufferMemory(m_device, staging, stagingMem, 0);

    /* one?shot command buffer ----------------------------------------- */
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = m_cmdPool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferImageCopy region{};
    region.imageExtent = { m_swapChainExtent.width,m_swapChainExtent.height,1 };
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;

    vkCmdCopyImageToBuffer(cmd, m_storageImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_computeQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_computeQueue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);

    /* save PNG --------------------------------------------------------- */
    void* data; vkMapMemory(m_device, stagingMem, 0, bytes, 0, &data);
    stbi_write_png(fileName.c_str(), m_swapChainExtent.width,
        m_swapChainExtent.height, 4, data,
        m_swapChainExtent.width * 4);
    vkUnmapMemory(m_device, stagingMem);

    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);
}

/* =======================================================================
   SECTION 6 :  plant regeneration   – scale * auto-fit camera
   =======================================================================*/

template<typename T>
inline T lerpT(T a, T b, float t)      // t in [0,1]
{
    return a * (1.0f - t) + b * t;
}

/*???????????????????????????????????????????????????????????????????????????*/
/*  shortest?arc quaternion that rotates v0 ? v1 (both assumed normalised)  */
static glm::quat rotationBetween(const glm::vec3& v0, const glm::vec3& v1)
{
    float  d = glm::dot(v0, v1);
    if (d > 0.9999f)                 // almost identical
        return glm::quat(1, 0, 0, 0);
    if (d < -0.9999f) {              // opposite – pick any orthogonal axis
        glm::vec3 axis = glm::cross(v0, glm::vec3(1, 0, 0));
        if (glm::length2(axis) < 1e-6f)
            axis = glm::cross(v0, glm::vec3(0, 1, 0));
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    glm::vec3 axis = glm::cross(v0, v1);
    float s = std::sqrt((1 + d) * 2);
    float invs = 1.f / s;
    return glm::quat(s * 0.5f, axis * invs);
}

void VulkanRaymarchApp::maybeRegeneratePlant(bool force)
{
    const float now = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - m_startTime).count();

    if (!force && (now - m_cycleStart) < 2.f) return;
    m_cycleStart = now;

    /* ---------------- choose preset ---------------- */
    static bool datasetMode = false;                 /* toggle elsewhere */
    if (!datasetMode)
        m_speciesIndex = (m_speciesIndex + 1) % m_presets.size();

    const LSystemPreset& P0 = m_presets[m_speciesIndex].second;
    LSystemPreset P = P0;

    /* ---------- data?set mode : make a random hybrid ------------------ */
    if (datasetMode) {
        static std::mt19937& gen = rng();
        std::uniform_int_distribution<size_t> pick(0, m_presets.size() - 1);
        std::uniform_real_distribution<float>  uf(0.f, 1.f);

        const LSystemPreset& P0 = m_presets[m_speciesIndex].second;
        const LSystemPreset& P1 = m_presets[pick(gen)].second;
        float w = uf(gen);          // random weight 0..1

        LSystemPreset hybrid = crossbreed(P0, P1, w, uint32_t(gen()));
        m_cpuBranches = generateLSystem(hybrid);
    }
    else {
        const auto& named = m_presets[m_speciesIndex];
        debugPrintPreset(named.first, named.second);
        m_cpuBranches = generateLSystem(m_presets[m_speciesIndex].second);
    }

    /* ---------------- build tree ------------------- */
    m_cpuBranches = generateLSystem(P);

    for (auto& b : m_cpuBranches) {                  /* global shrink */
        b.startX *= .40f; b.endX *= .40f;
        b.startY *= .40f; b.endY *= .40f;
        b.startZ *= .40f; b.endZ *= .40f;
    }

    /* ---------------- camera fit & orientation ------------------- */
    // (a) tight AABB for distance
/* ---------------- camera fit & orientation ------------------- */
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const auto& br : m_cpuBranches) {
        mn = vmin(mn, { br.startX, br.startY, br.startZ });
        mn = vmin(mn, { br.endX,   br.endY,   br.endZ });
        mx = vmax(mx, { br.startX, br.startY, br.startZ });
        mx = vmax(mx, { br.endX,   br.endY,   br.endZ });
    }
    m_camCenterY = 0.5f * (mn.y + mx.y);
    m_camDist = 0.75f * glm::length(mx - mn);

    /* align camera so the very first branch points straight up */
    if (!m_cpuBranches.empty()) {
        glm::vec3 baseDir = glm::normalize(glm::vec3(
            m_cpuBranches[0].endX - m_cpuBranches[0].startX,
            m_cpuBranches[0].endY - m_cpuBranches[0].startY,
            m_cpuBranches[0].endZ - m_cpuBranches[0].startZ));

        glm::quat q = rotationBetween(baseDir, glm::vec3(0, 1, 0));

        m_camF = q * glm::vec3(0, 0, -1);   // look direction
        m_camU = q * glm::vec3(0, 1, 0);   // up
        m_camR = q * glm::vec3(1, 0, 0);   // right
    }
    m_camPos = glm::vec3(0, m_camCenterY, 0) - m_camF * m_camDist;

    /* ---------------- GPU upload ------------------- */
    vkDeviceWaitIdle(m_device);

    if (m_branchBuffer) { vkDestroyBuffer(m_device, m_branchBuffer, nullptr); m_branchBuffer = VK_NULL_HANDLE; }
    if (m_branchMem) { vkFreeMemory(m_device, m_branchMem, nullptr);   m_branchMem = VK_NULL_HANDLE; }

    createBranchBuffer(m_cpuBranches, m_branchBuffer, m_branchMem, m_numBranches);

    m_cachedBVH = buildBVH(m_cpuBranches);
    uploadBVH(m_cachedBVH);
    updateDescriptorSetsWithBranchBuffer();

    m_maxBFS = 0.f;
    for (const auto& br : m_cpuBranches) m_maxBFS = std::max(m_maxBFS, br.bfsDepth);
}


/* =======================================================================
   SECTION 7 :  drawFrame
   =======================================================================*/
void VulkanRaymarchApp::drawFrame()
{
    maybeRegeneratePlant();

    vkWaitForFences(m_device, 1, &m_inFlight[m_frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_inFlight[m_frameIndex]);

    uint32_t imgIndex = 0;
    auto res = vkAcquireNextImageKHR(
        m_device, m_swapChain, UINT64_MAX,
        m_imgAvailSems[m_frameIndex], VK_NULL_HANDLE, &imgIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(); return; }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR");

    vkResetCommandBuffer(m_cmdBufs[m_frameIndex], 0);
    recordCommandBuffer(m_cmdBufs[m_frameIndex], imgIndex);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &m_imgAvailSems[m_frameIndex];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_cmdBufs[m_frameIndex];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m_renderDoneSems[m_frameIndex];

    if (vkQueueSubmit(m_computeQueue, 1, &si, m_inFlight[m_frameIndex]) != VK_SUCCESS)
        throw std::runtime_error("vkQueueSubmit");

    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_renderDoneSems[m_frameIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapChain;
    pi.pImageIndices = &imgIndex;

    res = vkQueuePresentKHR(m_presentQueue, &pi);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || m_fbResized) {
        m_fbResized = false;
        recreateSwapChain();
    }
    else if (res != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR");
    }

    m_frameIndex = (m_frameIndex + 1) % m_swapChainImages.size();
}

/* =======================================================================
   SECTION 8 :  recordCommandBuffer
   =======================================================================*/
void VulkanRaymarchApp::recordCommandBuffer(VkCommandBuffer cmd,
    uint32_t imgIndex)
{
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);

    auto barrier = [&](VkImage img, VkImageLayout oldL, VkImageLayout newL,
        VkAccessFlags srcA, VkAccessFlags dstA,
        VkPipelineStageFlags srcS, VkPipelineStageFlags dstS)
        {
            VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            b.oldLayout = oldL; b.newLayout = newL;
            b.srcAccessMask = srcA; b.dstAccessMask = dstA;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = img;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount = b.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd, srcS, dstS, 0,
                0, nullptr, 0, nullptr, 1, &b);
        };

    barrier(m_storageImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeLayout, 0, 1, &m_descSets[imgIndex],
        0, nullptr);

    /* camera push block ------------------------------------------------- */
    float yawR = glm::radians(m_yaw);
    float pitchR = glm::radians(m_pitch);

    glm::vec3 fwd{ std::cos(pitchR) * std::cos(yawR),
                   std::sin(pitchR),
                   std::cos(pitchR) * std::sin(yawR) };

    glm::vec3 target{ 0, m_camCenterY, 0 };      
    glm::vec3 pos = target - fwd * m_camDist;

    glm::vec3 upW{ 0,1,0 };
    glm::vec3 right = glm::normalize(glm::cross(fwd, upW));
    glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    struct PC {
        glm::vec4 camPos, camR, camU, camF;
        glm::vec4 screen, flags;
    } pc;
    pc.camPos = glm::vec4(pos, 0);
    pc.camR = glm::vec4(right, 0);
    pc.camU = glm::vec4(up, 0);
    pc.camF = glm::vec4(glm::normalize(fwd), 0);
    pc.screen = glm::vec4(float(m_swapChainExtent.width),
        float(m_swapChainExtent.height),
        float(m_numBranches),
        m_maxBFS);
//    pc.flags = glm::vec4(m_debugColoring ? 1.f : 0.f, 0, 0, 0);
    pc.flags = glm::vec4(1.0f, 1.0f, 0, 0);

    vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(PC), &pc);

    uint32_t gx = (m_swapChainExtent.width + 7) / 8;
    uint32_t gy = (m_swapChainExtent.height + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    barrier(m_storageImage, VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    barrier(m_swapChainImages[imgIndex], VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = copy.dstSubresource.layerCount = 1;
    copy.extent = { m_swapChainExtent.width, m_swapChainExtent.height, 1 };

    vkCmdCopyImage(cmd,
        m_storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_swapChainImages[imgIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy);

    barrier(m_swapChainImages[imgIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cmd);
}
/* =======================================================================
   SECTION 9 :  branch buffer helpers
   =======================================================================*/
void VulkanRaymarchApp::createBranchBuffer(const std::vector<CPUBranch>& src,
    VkBuffer& buf, VkDeviceMemory& mem,
    uint32_t& count)
{
    std::vector<CPUBranch> data = src.empty() ? std::vector<CPUBranch>(1) : src;
    count = static_cast<uint32_t>(data.size());
    VkDeviceSize size = count * 9 * sizeof(float);

    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = size;
    bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bc, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("vkCreateBuffer (branch SSBO)");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, buf, &req);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);

    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
        {
            idx = i; break;
        }
    }
    if (idx == UINT32_MAX)
        throw std::runtime_error("No host-visible memory type for branch SSBO");

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = idx;

    if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateMemory (branch SSBO)");

    vkBindBufferMemory(m_device, buf, mem, 0);

    /* upload ------------------------------------------------------------ */
    void* ptr = nullptr;
    vkMapMemory(m_device, mem, 0, size, 0, &ptr);
    float* fp = static_cast<float*>(ptr);

    for (auto& b : data) {
        *fp++ = b.startX; *fp++ = b.startY; *fp++ = b.startZ;
        *fp++ = b.radius;
        *fp++ = b.endX;   *fp++ = b.endY;   *fp++ = b.endZ;
        *fp++ = b.bfsDepth;

        union { float f; uint32_t u; } conv;
        conv.u = (b.parentIndex < 0) ? 0xffffffffu : uint32_t(b.parentIndex);
        *fp++ = conv.f;
    }
    vkUnmapMemory(m_device, mem);
}

void VulkanRaymarchApp::updateDescriptorSetsWithBranchBuffer()
{
    if (m_branchBuffer == VK_NULL_HANDLE)          // nothing to bind
        return;

    VkDescriptorBufferInfo bi{};
    bi.buffer = m_branchBuffer;
    bi.offset = 0;
    bi.range = VK_WHOLE_SIZE;

    for (VkDescriptorSet ds : m_descSets)
    {
        VkWriteDescriptorSet wr{};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = ds;
        wr.dstBinding = 1;                       // **binding 1 ? BranchBuf**
        wr.dstArrayElement = 0;
        wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr.pBufferInfo = &bi;

        vkUpdateDescriptorSets(m_device, 1, &wr, 0, nullptr);
    }
}

void VulkanRaymarchApp::uploadBVH(const BuiltBVH& b)
{
    auto makeBuf = [&](VkBuffer& buf, VkDeviceMemory& mem,
        const void* src, size_t sz)
        {
            if (sz == 0) {                             
                static const uint32_t dummy = 0;       
                src = &dummy;  sz = sizeof(dummy);
            }
            if (buf) { vkDestroyBuffer(m_device, buf, nullptr); buf = VK_NULL_HANDLE; }
            if (mem) { vkFreeMemory(m_device, mem, nullptr);    mem = VK_NULL_HANDLE; }

            VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bc.size = sz;
            bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(m_device, &bc, nullptr, &buf) != VK_SUCCESS)
                throw std::runtime_error("vkCreateBuffer (BVH)");

            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(m_device, buf, &req);

            VkPhysicalDeviceMemoryProperties mp;
            vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);

            uint32_t idx = 0;
            for (; idx < mp.memoryTypeCount; ++idx)
                if ((req.memoryTypeBits & (1u << idx)) &&
                    (mp.memoryTypes[idx].propertyFlags &
                        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
                    break;

            VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = idx;

            vkAllocateMemory(m_device, &ai, nullptr, &mem);
            vkBindBufferMemory(m_device, buf, mem, 0);

            void* ptr = nullptr;
            vkMapMemory(m_device, mem, 0, sz, 0, &ptr);
            std::memcpy(ptr, src, sz);
            vkUnmapMemory(m_device, mem);
        };

    makeBuf(m_bvhNodeBuf, m_bvhNodeMem, b.nodes.data(), b.nodes.size() * sizeof(BvhNode));
    makeBuf(m_bvhLeafBuf, m_bvhLeafMem, b.leafIdx.data(), b.leafIdx.size() * sizeof(uint32_t));

    for (auto ds : m_descSets) {
        VkDescriptorBufferInfo ni{ m_bvhNodeBuf, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo li{ m_bvhLeafBuf, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet w[2]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = ds;
        w[0].dstBinding = 2;                         /* BVH nodes */
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[0].descriptorCount = 1;
        w[0].pBufferInfo = &ni;

        w[1] = w[0];
        w[1].dstBinding = 3;                       /* BVH leaves */
        w[1].pBufferInfo = &li;

        vkUpdateDescriptorSets(m_device, 2, w, 0, nullptr);
    }
}

/* -----------------------------------------------------------------------
   Vulkan debug utils helpers – single definition so the linker is happy
   -----------------------------------------------------------------------*/
VKAPI_ATTR VkBool32 VKAPI_CALL
VulkanRaymarchApp::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT       /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Validation] " << data->pMessage << "\n";
    return VK_FALSE;
}

VkResult VulkanRaymarchApp::createDebugUtilsMessengerEXT(
    VkInstance                                  inst,
    const VkDebugUtilsMessengerCreateInfoEXT* info,
    const VkAllocationCallbacks* alloc,
    VkDebugUtilsMessengerEXT* out)
{
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT"));
    return fn ? fn(inst, info, alloc, out)
        : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VulkanRaymarchApp::destroyDebugUtilsMessengerEXT(
    VkInstance                       inst,
    VkDebugUtilsMessengerEXT         dbg,
    const VkAllocationCallbacks* alloc)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn) fn(inst, dbg, alloc);
}

/* =======================================================================
   Sections 10 & 11 (low-level Vulkan plumbing + misc helpers)
   were moved to vulkanbackend.cpp – no duplicates here.
   =======================================================================*/
