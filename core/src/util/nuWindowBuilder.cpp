#include "util/nuWindowBuilder.h"

#include <VkBootstrap.h>
nuWindowBuilder::nuWindowBuilder(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkExtent2D windowExtent) {
    _physicalDevice = physicalDevice;
    _device = device;
    _surface = surface;
    _windowExtent = windowExtent;
}

nuSwapchainBuild_ret nuWindowBuilder::buildSwapchain() {
    vkb::SwapchainBuilder swapBuilder{ _physicalDevice, _device, _surface };

    _swapchainBuild.swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainBuild.swapchain_image_format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(_windowExtent.width, _windowExtent.height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    _swapchainBuild.swapchain_extent = vkbSwapchain.extent;
    _swapchainBuild.swapchain = vkbSwapchain.swapchain;
    _swapchainBuild.swapchain_images = vkbSwapchain.get_images().value();
    _swapchainBuild.swapchain_image_views = vkbSwapchain.get_image_views().value();

    return _swapchainBuild;
}
    
    VkExtent3D 




    
    

}
