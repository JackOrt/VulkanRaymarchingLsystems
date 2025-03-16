#include "VulkanRaymarchApp.hpp"
#include <iostream>

int main()
{
    try
    {
        VulkanRaymarchApp app(800, 600, "Vulkan Raymarching - Rotating Cube");
        app.run();
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << "Runtime error: " << err.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
