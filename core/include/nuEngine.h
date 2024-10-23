#pragma once

#include <volk.h>
#include <vulkan/vulkan_core.h>

#include "nuWindow.h"

class nuEngine {
    public:
        void init();
        void run();
        void cleanup();
    private:
        VkPhysicalDeviceProperties _deviceProperties1{};
        VkPhysicalDeviceProperties2 _deviceProperties2{};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR _rtProperties{};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR _asProperties{};

        void init_sdl();
        void init_device_properties();
};
