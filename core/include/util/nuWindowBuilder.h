#pragma once
#include <volk.h>
#include <vector>

struct nuSwapchainBuild_ret {
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkExtent2D swapchain_extent;
};

class nuWindowBuilder {

    public:
        nuWindowBuilder(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkExtent2D windowExtent);
        nuSwapchainBuild_ret buildSwapchain();


    private:
        nuSwapchainBuild_ret _swapchainBuild{};
        VkPhysicalDevice _physicalDevice;
        VkDevice _device;
        VkSurfaceKHR _surface;
        VkExtent2D _windowExtent;

};
