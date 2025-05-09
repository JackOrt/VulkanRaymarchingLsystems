#pragma once
/*  Internal helpers – separated to keep VulkanRaymarchApp.cpp readable.      */

void initVulkan(VulkanRaymarchApp&);
void createInstance(VulkanRaymarchApp&);
void setupDebugMessenger(VulkanRaymarchApp&);
void pickPhysicalDevice(VulkanRaymarchApp&);
void createLogicalDevice(VulkanRaymarchApp&);
void createSurface(VulkanRaymarchApp&);
void createSwapChain(VulkanRaymarchApp&);
void createSwapChainImageViews(VulkanRaymarchApp&);
void createCommandPool(VulkanRaymarchApp&);
void createComputeResources(VulkanRaymarchApp&);
void createStorageImage(VulkanRaymarchApp&);
void createDescriptorSetLayout(VulkanRaymarchApp&);
void createDescriptorPoolAndSets(VulkanRaymarchApp&);
void createCommandBuffers(VulkanRaymarchApp&);
void createSyncObjects(VulkanRaymarchApp&);
void cleanupSwapChain(VulkanRaymarchApp&);
void recreateSwapChain(VulkanRaymarchApp&);

bool checkValidationLayerSupport(const VulkanRaymarchApp&);
std::vector<const char*> getRequiredExtensions(const VulkanRaymarchApp&);
