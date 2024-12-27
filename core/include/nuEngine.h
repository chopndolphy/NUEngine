#pragma once

#include <volk.h>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include "nuWindow.h"
#include "nuDeletionQueue.h"

class nuEngine {
    public:
        void init();
        void run();
        void cleanup();
    private:
        struct SDL_Window* _window{ nullptr };
        VkExtent2D _windowExtent{ 1700, 900 };

        VkInstance _instance;
        VkDebugUtilsMessengerEXT _debugMessenger;
        VkPhysicalDevice _physicalDevice;
        VkDevice _device;
        VkSurfaceKHR _surface;
        VmaAllocator _allocator;
        VkQueue _graphicsQueue;
        uint32_t _graphicsQueueFamily;
        VkQueue _asyncComputeQueue;
        uint32_t _asyncComputeQueueFamily;

        VkSwapchainKHR _swapchain;
        VkFormat _swapchainImageFormat;
        VkImage _swapchainImages[2];
        VkImageView _swapchainImageViews[2];
        VkExtent2D _swapchainExtent;
        nuDeletionQueue _swapchainDeletionQueue;




        VkPhysicalDeviceProperties _deviceProperties1{};
        VkPhysicalDeviceProperties2 _deviceProperties2{};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR _rtProperties{};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR _asProperties{};

        void init_sdl();
        void init_swapchain();
        void init_device_properties();
};
