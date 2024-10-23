#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>

struct nuInstanceBuild_ret {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;
    VmaAllocator allocator;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    VkQueue async_compute_queue;
    uint32_t async_compute_queue_family;
};

class nuInstanceBuilder {
    public:
        nuInstanceBuild_ret build();
    private:

};
