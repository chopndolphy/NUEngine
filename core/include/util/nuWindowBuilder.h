#pragma once
#include <volk.h>

struct nuWindowBuild_ret {
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkImage swapchain_images[2];
    VkImageView swapchain_image_views[2];
    VkExtent2D swapchain_extent;
};

class nuWindowBuilder {
    public:
        nuWindowBuild_ret build();

    private:

};
