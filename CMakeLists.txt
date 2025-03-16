cmake_minimum_required(VERSION 3.10)
project(VulkanLSystem3D)

# Set C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Ensure MSVC uses the correct standard
if (MSVC)
    add_compile_options("/std:c++17")
endif()

# Find Vulkan
find_package(Vulkan REQUIRED)

# Find GLFW (installed via vcpkg)
find_package(glfw3 CONFIG REQUIRED)

# Find GLM (installed via vcpkg)
find_package(glm CONFIG REQUIRED)

# Add source files
set(SOURCES
    src/main.cpp
    src/VulkanApp.cpp
)

# Add executable
add_executable(VulkanLSystem3D ${SOURCES})

# Ensure it's a CONSOLE application, not a GUI application
set_target_properties(VulkanLSystem3D PROPERTIES
    WIN32_EXECUTABLE OFF  # Ensures main() is used instead of WinMain()
)

# Link libraries
target_link_libraries(VulkanLSystem3D PRIVATE Vulkan::Vulkan glfw glm::glm)
