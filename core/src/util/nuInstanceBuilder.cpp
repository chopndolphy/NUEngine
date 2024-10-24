#include "util/nuInstanceBuilder.h"

#include <SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vulkan/vulkan_core.h>

nuInstanceBuilder::nuInstanceBuilder(SDL_Window* window) {
    _window = window;
}

nuInstanceBuild_ret nuInstanceBuilder::build() {
    vkb::InstanceBuilder builder(vkGetInstanceProcAddr);

    auto instRet = builder
        .set_app_name("NU-Engine")
#ifdef USE_VALIDATION_LAYERS
        .request_validation_layers(true)
#else
        .request_validation_layers(false)
#endif
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkbInst = instRet.value();

    _build.instance = vkbInst.instance;
    volkLoadInstance(_build.instance);
    _build.debug_messenger = vkbInst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _build.instance, &_build.surface);

    VkPhysicalDeviceVulkan13Features feat13{};
    feat13.dynamicRendering = true;
    feat13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features feat12{};
    feat12.bufferDeviceAddress = true;
    feat12.descriptorIndexing = true;
    feat12.uniformAndStorageBuffer8BitAccess = true;
    feat12.hostQueryReset = true;

    VkPhysicalDeviceFeatures feat10{};
    feat10.samplerAnisotropy = true;
    feat10.sampleRateShading = true;

    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pdlmFeat{};
    pdlmFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT;
    pdlmFeat.pageableDeviceLocalMemory = true;

    vkb::PhysicalDeviceSelector selector{ vkbInst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(feat13)
        .set_required_features_12(feat12)
        .set_required_features(feat10)
        .set_surface(_build.surface)
        .add_desired_extension("VK_KHR_deferred_host_operations")
        .add_desired_extension("VK_KHR_acceleration_structure")
        .add_desired_extension("VK_KHR_ray_query")
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    _build.device = vkbDevice.device;
    volkLoadDevice(_build.device);
    _build.physical_device = physicalDevice.physical_device;

    _build.graphics_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _build.graphics_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    _build.async_compute_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _build.async_compute_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaVulkanFunctions vmaVulkanFunc{};
    vmaVulkanFunc.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaVulkanFunc.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocInfo = {};
    allocInfo.physicalDevice = _build.physical_device;
    allocInfo.device = _build.device;
    allocInfo.instance = _build.instance;
    allocInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocInfo.pVulkanFunctions = &vmaVulkanFunc;
    vmaCreateAllocator(&allocInfo, &_build.allocator);

    return _build;
}
