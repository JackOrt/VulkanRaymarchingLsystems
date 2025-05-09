#pragma once
/*  Vulkan backend for VulkanRaymarchApp
    ------------------------------------
    This header adds no new declarations; it simply exists so you can keep
    every compilation-unit that implements low-level Vulkan code behind a
    single include.

    Include it **once** in any translation-unit that needs direct access to
    these helpers (normally only `vulkanbackend.cpp`).                    */

#include "src/VulkanRaymarchApp.hpp"
