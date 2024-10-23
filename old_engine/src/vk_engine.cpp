//> includes
#include "vk_engine.h"
#include "vk_types.h"
#include "vk_images.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"
#include "vk_loader.h"
#include "vk_descriptors.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <boost/unordered_map.hpp>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <vulkan/vulkan_core.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
 
#include <VkBootstrap.h>


#include <chrono>
#include <thread>
#include <array>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <SDL2/SDL_mouse.h>


VulkanEngine* loadedEngine = nullptr;

// #define NDEBUG

#ifdef NDEBUG
constexpr bool bUseValidationLayers = false;
#else
constexpr bool bUseValidationLayers = true;
#endif

const std::string sceneString = "da_vinci.glb";

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; } 

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

void VulkanEngine::init()
{
    volkInitialize();
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    SDL_DisplayMode DM;
    SDL_GetCurrentDisplayMode(0, &DM);
    _windowExtent.width = DM.w * 0.8f;
    _windowExtent.height = DM.h * 0.8f;
 
    _window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _windowExtent.width,
        _windowExtent.height,
        window_flags);

    SDL_SetRelativeMouseMode(SDL_TRUE);

    init_vulkan();
    init_swapchain();
    init_commands();
    init_async_compute_commands();
    init_sync_structures();
    init_async_compute_sync_structures();
    init_descriptors();
    init_pipelines();
    init_default_data();
    init_renderables();
#ifndef AVI_DISABLE_INTERCHANGE
    init_interprocess();
#endif // AVI_DISABLE_INTERCHANGE
    init_ray_tracing();
    init_imgui();


    _mainCamera.velocity = glm::vec3(0.0f);
    _mainCamera.position = glm::vec3(0.03f, 1.0f, -0.5f);

    _mainCamera.pitch = 0;
    _mainCamera.yaw = 0;

    _sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    // _sceneData.sunlightColor = glm::vec4(0.9647f, 0.8039f, 0.5451f, 1.0f);
    _sceneData.sunlightColor = glm::vec4(1.0f);
    _sceneData.sunlightDirection = glm::vec4(0.0f, 0.002f, 0.0f, 1.0f);
    // _sceneData.sunlightColor = glm::vec4(0.8f);

    lightCutoffRad = 40.0f;
    lightOuterCutoffRad = 50.0f;
    lightPos[0] = 0.0f;
    lightPos[1] = 0.002f;
    lightPos[2] = 0.0f;
    _sceneData.lightIntensity = 0.4f;
    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::cleanup()
{
    if (_isInitialized) {
        vkDeviceWaitIdle(_device);

        cleanup_ray_tracing();

        for (auto& scene : _loadedScenes) {
            scene.second->clearAll();
        }
        for (auto& meshBuffers : meshesToDelete) {
            destroy_buffer(meshBuffers.vertexBuffer);
            destroy_buffer(meshBuffers.indexBuffer);
        }
        _interprocess->destroy(); 
        _loadedScenes.clear();
        
        for (auto& frame : _frames) {
            frame._deletionQueue.flush();
        }

        _mainDeletionQueue.flush();

        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);

        vmaDestroyAllocator(_allocator);
 
        vkDestroyDevice(_device, nullptr);

        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);
        SDL_DestroyWindow(_window);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw()
{

    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);

    uint32_t swapchainImageIndex;

    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
        _resize_requested = true;
        return;
    }

    _drawExtent.width = std::min(_windowExtent.width, _drawImage.imageExtent.width) * _renderScale;
    _drawExtent.height = std::min(_windowExtent.height, _drawImage.imageExtent.height) * _renderScale;

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
 
    VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));

    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
 
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
 
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkutil::transition_image(cmd, _drawImage.image, imageBarrier);
    }
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        vkutil::transition_image(cmd, _depthImage.image, imageBarrier);
    }
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkutil::transition_image(cmd, _msaaDrawImage.image, imageBarrier);
    }
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT ;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        vkutil::transition_image(cmd, _msaaDepthImage.image, imageBarrier);
    }
    draw_main(cmd);
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkutil::transition_image(cmd, _postProcessingImage.image, imageBarrier);
    }
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT; 
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], imageBarrier);
    }
    vkutil::copy_image_to_image(cmd, _postProcessingImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT; 
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], imageBarrier);
    }
    draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], imageBarrier);
    }
    if (_io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

    }
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);

    VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    VkPresentInfoKHR presentInfo = vkinit::present_info();
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        _resize_requested = true;
        return;
    }

    _frameNumber++;
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        auto start = std::chrono::system_clock::now();
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT) bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    _resize_requested = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    _freeze_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    _freeze_rendering = false;
                }
            }
            
            _mainCamera.processSDLEvent(e);
            ImGui_ImplSDL2_ProcessEvent(&e);
        }
        
        if (_mainCamera.shouldUnfocus) {
            SDL_SetRelativeMouseMode(SDL_FALSE); // probably should be an event system
            SDL_SetWindowMouseGrab(_window, SDL_FALSE);
        }
        else {
            SDL_SetRelativeMouseMode(SDL_TRUE); // probably should be an event system 
            SDL_SetWindowMouseGrab(_window, SDL_TRUE);
        }

        if (_mainCamera.shouldClose) {
            bQuit = true;
        }

        // do not draw if we are minimized
        if (_freeze_rendering) continue;

        if (_resize_requested) {
            resize_swapchain();
        }
        
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Stats"))
        {
            ImGui::Text("frame time: %f ms", _stats.frame_time);
            ImGui::Text("draw time: %f ms", _stats.mesh_draw_time);
            ImGui::Text("update time: %f ms", _stats.scene_update_time);
            ImGui::Text("triangle count: %i", _stats.triangle_count);
            ImGui::Text("draw call count: %i", _stats.draw_call_count);
            ImGui::Text("camera positon.x: %f", _stats.camera_location.x);
            ImGui::Text("camera positon.y: %f", _stats.camera_location.y);
            ImGui::Text("camera positon.z: %f", _stats.camera_location.z);

            ImGui::End();
        }

        lightColor[0] = _sceneData.sunlightColor.r;
        lightColor[1] = _sceneData.sunlightColor.g;
        lightColor[2] = _sceneData.sunlightColor.b;

        if (ImGui::Begin("Scene"))
        {
            ImGui::ColorPicker3("Spotlight Color", lightColor);
            ImGui::InputFloat("Light Cutoff", &lightCutoffRad);
            ImGui::InputFloat("Light Outer Cutoff", &lightOuterCutoffRad);
            ImGui::InputFloat("Light Intensity", &_sceneData.lightIntensity);
            ImGui::InputFloat3("Spotlight Position", lightPos);
            ImGui::Text("Light Location x: %f", _sceneData.sunlightDirection[0]);
            ImGui::Text("Light Location y: %f", _sceneData.sunlightDirection[1]);
            ImGui::Text("Light Location z: %f", _sceneData.sunlightDirection[2]);
            ImGui::Text("Light Location w: %f", _sceneData.sunlightDirection[3]);

            ImGui::End();
        }
        _sceneData.sunlightColor = glm::vec4(lightColor[0], lightColor[1], lightColor[2], 1.0f);
        _sceneData.sunlightDirection = glm::vec4(glm::normalize(glm::vec3(_mainCamera.getViewMatrix()[1])) * lightPos[1], 1.0f);

        ImGui::Render();
        
        update_scene();

        draw();

        auto end = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        _stats.frame_time = elapsed.count() / 1000.0f;
    }
}
void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer cmd = _immCommandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    function(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}
void VulkanEngine::async_compute_submit(std::function<void(VkCommandBuffer cmd)> &&function) {
    VK_CHECK(vkResetFences(_device, 1, &_asyncComputeFence));
    VK_CHECK(vkResetCommandBuffer(_asyncComputeCommandBuffer, 0));

    VkCommandBuffer cmd = _asyncComputeCommandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    function(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(_asyncComputeQueue, 1, &submit, _asyncComputeFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_asyncComputeFence, true, 9999999999));
}
GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    newSurface.vertexBuffer = create_buffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT 
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    VkBufferDeviceAddressInfo deviceVertexAddressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = newSurface.vertexBuffer.buffer
    };
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceVertexAddressInfo);

    newSurface.indexBuffer = create_buffer(
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    AllocatedBuffer staging = create_buffer(
        vertexBufferSize + indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    void* data = staging.allocation->GetMappedData();

    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediate_submit([&](VkCommandBuffer cmd) { // usually put on a background thread that solely executes uploads, deleting/reusing the stageing buffers
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
    }); // otherwise the cpu is waiting until the gpu is done 

    destroy_buffer(staging);

    return newSurface;
}

void VulkanEngine::init_vulkan()
{
    vkb::InstanceBuilder builder(vkGetInstanceProcAddr);

    auto inst_ret = builder
        .set_app_name("Vulkan Application")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkb_inst = inst_ret.value();

    _instance = vkb_inst.instance;
    volkLoadInstance(_instance);
    _debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    VkPhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.uniformAndStorageBuffer8BitAccess = true;
    features12.hostQueryReset = true;

    VkPhysicalDeviceFeatures features10{};
    features10.samplerAnisotropy = true;
    features10.sampleRateShading = true;

    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pdlmFeatures{};
    pdlmFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT;
    pdlmFeatures.pageableDeviceLocalMemory = true;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStrucFeatures{};
    accelStrucFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStrucFeatures.accelerationStructure = true;
    accelStrucFeatures.accelerationStructureCaptureReplay = true;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQuerFeatures{};
    rayQuerFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQuerFeatures.rayQuery = true;

    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rtMaintFeatures{};
    rtMaintFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR;
    rtMaintFeatures.rayTracingMaintenance1 = true;
    rtMaintFeatures.rayTracingPipelineTraceRaysIndirect2 = true;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeFeatures {};
    rtPipeFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtPipeFeatures.rayTracingPipeline = true;
    rtPipeFeatures.rayTracingPipelineTraceRaysIndirect = true;
    rtPipeFeatures.rayTraversalPrimitiveCulling = true;
    
    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR rtPosFetchFeatures{};
    rtPosFetchFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR;
    rtPosFetchFeatures.rayTracingPositionFetch = true;

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_required_features(features10)
        .set_surface(_surface)
        .add_desired_extension("VK_KHR_deferred_host_operations")
        // .add_desired_extension("VK_EXT_pageable_device_local_memory")
        // .add_desired_extension("VK_EXT_memory_priority")
        .add_desired_extension("VK_KHR_acceleration_structure")
        .add_desired_extension("VK_KHR_ray_query")
        // .add_desired_extension("VK_KHR_ray_tracing_maintenance1")
        // .add_desired_extension("VK_KHR_ray_tracing_pipeline")
        // .add_desired_extension("VK_KHR_ray_tracing_position_fetch")
        // .add_required_extension_features(pdlmFeatures)
        .add_required_extension_features(accelStrucFeatures)
        .add_required_extension_features(rayQuerFeatures)
        // .add_required_extension_features(rtMaintFeatures)
        // .add_required_extension_features(rtPipeFeatures)
        // .add_required_extension_features(rtPosFetchFeatures)
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    _device = vkbDevice.device;
    volkLoadDevice(_device);
    _chosenGPU = physicalDevice.physical_device; 

    vkGetPhysicalDeviceProperties(_chosenGPU, &_gpuProperties);
    _msaaSampleCount = getMaxUsableSampleCount();

    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    _asyncComputeQueue = vkbDevice.get_queue(vkb::QueueType::compute).value();
    _asyncComputeQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::compute).value();

    _rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    _rtProperties.pNext = &_asProperties;
    _asProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 prop2{};
    prop2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    prop2.pNext = &_rtProperties;
    vkGetPhysicalDeviceProperties2(_chosenGPU, &prop2);

    VmaVulkanFunctions vmaVulkanFunc{};
    vmaVulkanFunc.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaVulkanFunc.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.pVulkanFunctions = &vmaVulkanFunc;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
}
void VulkanEngine::init_swapchain()
{ 
    create_swapchain(_windowExtent.width, _windowExtent.height);


    VkExtent3D drawImageExtent = {
        //DM.w,
        _windowExtent.width,
        //DM.h,
        _windowExtent.height,
        1
    };

    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent; 
    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent); 
    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 
    vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr); 
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT); 
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));
 
    _msaaDrawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _msaaDrawImage.imageExtent = drawImageExtent; 
    VkImageUsageFlags msaaDrawImageUsages{};
    msaaDrawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    msaaDrawImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    VkImageCreateInfo mrimg_info = vkinit::image_create_info(_msaaDrawImage.imageFormat, msaaDrawImageUsages, drawImageExtent); 
    mrimg_info.samples = _msaaSampleCount;
    VmaAllocationCreateInfo mrimg_allocinfo = {};
    mrimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    mrimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 
    VK_CHECK(vmaCreateImage(_allocator, &mrimg_info, &mrimg_allocinfo, &_msaaDrawImage.image, &_msaaDrawImage.allocation, nullptr));
    VkImageViewCreateInfo mrview_info = vkinit::imageview_create_info(_msaaDrawImage.imageFormat, _msaaDrawImage.image, VK_IMAGE_ASPECT_COLOR_BIT); 
    mrview_info.components.r = VK_COMPONENT_SWIZZLE_R;
    mrview_info.components.g = VK_COMPONENT_SWIZZLE_G;
    mrview_info.components.b = VK_COMPONENT_SWIZZLE_B;
    mrview_info.components.a = VK_COMPONENT_SWIZZLE_A;
    VK_CHECK(vkCreateImageView(_device, &mrview_info, nullptr, &_msaaDrawImage.imageView));

    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; 
    VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent); 
    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 
    vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr); 
    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT); 
    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));

    _msaaDepthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _msaaDepthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags msaaDepthImageUsages{};
    msaaDepthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; 
    msaaDepthImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT; 
    VkImageCreateInfo mdimg_info = vkinit::image_create_info(_msaaDepthImage.imageFormat, msaaDepthImageUsages, drawImageExtent); 
    mdimg_info.samples = _msaaSampleCount;
    VmaAllocationCreateInfo mdimg_allocinfo = {};
    mdimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    mdimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 
    vmaCreateImage(_allocator, &mdimg_info, &mdimg_allocinfo, &_msaaDepthImage.image, &_msaaDepthImage.allocation, nullptr); 
    VkImageViewCreateInfo mdview_info = vkinit::imageview_create_info(_msaaDepthImage.imageFormat, _msaaDepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT); 
    mdview_info.components.r = VK_COMPONENT_SWIZZLE_R;
    mdview_info.components.g = VK_COMPONENT_SWIZZLE_G;
    mdview_info.components.b = VK_COMPONENT_SWIZZLE_B;
    mdview_info.components.a = VK_COMPONENT_SWIZZLE_A;
    VK_CHECK(vkCreateImageView(_device, &mdview_info, nullptr, &_msaaDepthImage.imageView));
 
    _postProcessingImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _postProcessingImage.imageExtent = drawImageExtent; 
    VkImageUsageFlags postProcessingImageUsages{};
    postProcessingImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    postProcessingImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    postProcessingImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo pimg_info = vkinit::image_create_info(_postProcessingImage.imageFormat, postProcessingImageUsages, _postProcessingImage.imageExtent); 
    VmaAllocationCreateInfo pimg_allocinfo = {};
    pimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    pimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ); 
    vmaCreateImage(_allocator, &pimg_info, &pimg_allocinfo, &_postProcessingImage.image, &_postProcessingImage.allocation, nullptr); 
    VkImageViewCreateInfo pview_info = vkinit::imageview_create_info(_postProcessingImage.imageFormat, _postProcessingImage.image, VK_IMAGE_ASPECT_COLOR_BIT); 
    VK_CHECK(vkCreateImageView(_device, &pview_info, nullptr, &_postProcessingImage.imageView));

    _rtDrawImage.imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    _rtDrawImage.imageExtent = drawImageExtent; 
    VkImageUsageFlags rtDrawImageUsages{};
    rtDrawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    rtDrawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    rtDrawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    rtDrawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateInfo rtrimg_info = vkinit::image_create_info(_rtDrawImage.imageFormat, rtDrawImageUsages, drawImageExtent); 
    VmaAllocationCreateInfo rtrimg_allocinfo = {};
    rtrimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    rtrimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 
    vmaCreateImage(_allocator, &rtrimg_info, &rtrimg_allocinfo, &_rtDrawImage.image, &_rtDrawImage.allocation, nullptr); 
    VkImageViewCreateInfo rtrview_info = vkinit::imageview_create_info(_rtDrawImage.imageFormat, _rtDrawImage.image, VK_IMAGE_ASPECT_COLOR_BIT); 
    VK_CHECK(vkCreateImageView(_device, &rtrview_info, nullptr, &_rtDrawImage.imageView));

    _rtDepthImage.imageFormat = VK_FORMAT_X8_D24_UNORM_PACK32;
    _rtDepthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags rtDepthImageUsages{};
    rtDepthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; 
    VkImageCreateInfo rtdimg_info = vkinit::image_create_info(_rtDepthImage.imageFormat, rtDepthImageUsages, drawImageExtent); 
    VmaAllocationCreateInfo rtdimg_allocinfo = {};
    rtdimg_allocinfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
    rtdimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 
    vmaCreateImage(_allocator, &rtdimg_info, &rtdimg_allocinfo, &_rtDepthImage.image, &_rtDepthImage.allocation, nullptr); 
    VkImageViewCreateInfo rtdview_info = vkinit::imageview_create_info(_rtDepthImage.imageFormat, _rtDepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT); 
    VK_CHECK(vkCreateImageView(_device, &rtdview_info, nullptr, &_rtDepthImage.imageView));

    _mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);

        vkDestroyImageView(_device, _msaaDrawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _msaaDrawImage.image, _msaaDrawImage.allocation);

        vkDestroyImageView(_device, _msaaDepthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _msaaDepthImage.image, _msaaDepthImage.allocation);

        vkDestroyImageView(_device, _postProcessingImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _postProcessingImage.image, _postProcessingImage.allocation);

        vkDestroyImageView(_device, _rtDrawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _rtDrawImage.image, _rtDrawImage.allocation);

        vkDestroyImageView(_device, _rtDepthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _rtDepthImage.image, _rtDepthImage.allocation);
    });
}
void VulkanEngine::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(
        _graphicsQueueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

        _mainDeletionQueue.push_function([=]() {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
        });
    }

    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);

    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.push_function([=]() {
        vkDestroyCommandPool(_device, _immCommandPool, nullptr);
    });
}
void VulkanEngine::init_async_compute_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(
        _asyncComputeQueueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_asyncComputeCommandPool));
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_asyncComputeCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_asyncComputeCommandBuffer));

    _mainDeletionQueue.push_function([=]() {
        vkDestroyCommandPool(_device, _asyncComputeCommandPool, nullptr);
    });
}
void VulkanEngine::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

        _mainDeletionQueue.push_function([=]() {
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
        });
    }

    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.push_function([=]() {
        vkDestroyFence(_device, _immFence, nullptr);
    });
}
void VulkanEngine::init_async_compute_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_asyncComputeFence));
    _mainDeletionQueue.push_function([=]() {
        vkDestroyFence(_device, _asyncComputeFence, nullptr);
    });
}
void VulkanEngine::init_descriptors()
{
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1 },
    };

    _globalDescriptorAllocator.init(_device, 10, sizes);
    _mainDeletionQueue.push_function([&]() {
        _globalDescriptorAllocator.destroy_pools(_device);
    });

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _gpuSceneDataDescriptorLayout = builder.build(
            _device,
            VK_SHADER_STAGE_VERTEX_BIT
            | VK_SHADER_STAGE_FRAGMENT_BIT
            | VK_SHADER_STAGE_RAYGEN_BIT_KHR 
            | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        );
    }

    _mainDeletionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
    });

    _drawImageDescriptors = _globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);
    
    {
        DescriptorWriter writer;
        writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        writer.update_set(_device, _drawImageDescriptors);
    }

    for (int i = 0; i < FRAME_OVERLAP; i++) {

        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1 }
        };

        _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
        _frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);
 _mainDeletionQueue.push_function([&, i]() {
            _frames[i]._frameDescriptors.destroy_pools(_device);
        });
    } 

    create_rt_descriptor_set();
}
void VulkanEngine::init_pipelines()
{
    init_background_pipelines();

    init_post_process_pipelines();

    _metalRoughMaterial.build_pipelines(this);

    _mainDeletionQueue.push_function([&]() {
        _metalRoughMaterial.clear_resources(_device); 
    });
}
void VulkanEngine::init_background_pipelines()
{
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;



    _computePushConstantRange.offset = 0;
    _computePushConstantRange.size = sizeof(ComputePushConstants);
    _computePushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pPushConstantRanges = &_computePushConstantRange;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));
    VkShaderModule gradientShader{};
    if (!vkutil::load_shader_module("../shaders/gradient_color.comp.spv", _device, &gradientShader))
    {
        std::cout << "Error when building the gradient shader" << std::endl;
    }

    VkShaderModule skyShader{};
    if (!vkutil::load_shader_module("../shaders/sky.comp.spv", _device, &skyShader))
    {
        std::cout << "Error when building the sky shader" << std::endl;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext = nullptr;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = gradientShader;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageInfo;

    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};

    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);
 
    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    computePipelineCreateInfo.stage.module = skyShader;

    ComputeEffect sky;
    sky.layout = _gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};

    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);


    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    _backgroundEffects.push_back(gradient);
    _backgroundEffects.push_back(sky);

    vkDestroyShaderModule(_device, gradientShader, nullptr);
    vkDestroyShaderModule(_device, skyShader, nullptr);
    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
        for (auto& effect : _backgroundEffects) {
            vkDestroyPipeline(_device, effect.pipeline, nullptr);
        }
    });
}
void VulkanEngine::init_imgui()
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    _io = std::make_shared<ImGuiIO>(ImGui::GetIO());
    _io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    _io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    _io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    _io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui_ImplSDL2_InitForVulkan(_window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _instance;
    init_info.PhysicalDevice = _chosenGPU;
    init_info.Device = _device;
    init_info.QueueFamily = _graphicsQueueFamily;
    init_info.Queue = _graphicsQueue;
    init_info.DescriptorPool = imguiPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = _allocator->GetAllocationCallbacks();
    init_info.CheckVkResultFn = check_vk_result;
    init_info.UseDynamicRendering = true;

    VkPipelineRenderingCreateInfo pipeInfo{}; 
    pipeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeInfo.pColorAttachmentFormats = &_swapchainImageFormat;
    pipeInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo = pipeInfo;

    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture();

    // ImGui_ImplVulkan_LoadFunctions([](const char *function_name, void *vulkan_instance) {
    //     return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance *>(vulkan_instance)), function_name);
    // }, &_instance);
    // ImGui_ImplVulkan_LoadFunctions([](const char *function_name, void *vulkan_device) {
    //     return vkGetDeviceProcAddr(*(reinterpret_cast<VkDevice *>(vulkan_device)), function_name);
    // }, &_instance);

    _mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
    });
}
 
void VulkanEngine::init_default_data()
{ 
    uint32_t white = 0xFFFFFFFF;
    _whiteImage = create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
 
    uint32_t grey = 0xFFAAAAAA;
    _greyImage = create_image((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = 0xFF000000;
    _blackImage = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t magenta = 0xFFFF00FF;
    std::array<uint32_t, 16 * 16> pixels;
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(_chosenGPU, &properties);

    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    sampl.anisotropyEnable = VK_TRUE;
    sampl.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest); 

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    sampl.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampl.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampl.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampl.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampl.anisotropyEnable = VK_TRUE;
    sampl.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

    _mainDeletionQueue.push_function([&]() {
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
        destroy_image(_errorCheckerboardImage);
        destroy_image(_blackImage);
        destroy_image(_greyImage);
        destroy_image(_whiteImage);
    });
}

void VulkanEngine::init_renderables()
{
    std::string scenePath = { "../assets/" + sceneString };
    auto sceneFile = vkutil::load_gltf(this, scenePath);

    assert(sceneFile.has_value());

    _loadedScenes[sceneString] = *sceneFile;
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };

    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    _swapchainExtent = vkbSwapchain.extent;
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}
void VulkanEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    for (int i = 0; i < _swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
}
void VulkanEngine::resize_swapchain()
{
    vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    _resize_requested = false;
}
AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = memoryUsage;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

    return newBuffer;
}
AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlagBits allocFlags)
{
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = memoryUsage;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | allocFlags;
    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

    return newBuffer;
}
void VulkanEngine::destroy_buffer(const AllocatedBuffer &buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}
void VulkanEngine::draw_main(VkCommandBuffer cmd)
{ 

    ComputeEffect& effect = _backgroundEffects[_currentBackgroundEffect];

    //vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    //vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    //vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    //vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
 
    //VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL); 
    //VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL); 
    //VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);

    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_msaaDrawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL); 
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = _drawImage.imageView;
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_msaaDepthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL); 
    depthAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    depthAttachment.resolveImageView = _depthImage.imageView;
    depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);

    vkCmdBeginRendering(cmd, &renderInfo); 
    auto start = std::chrono::system_clock::now();

    draw_geometry(cmd);

    auto end = std::chrono::system_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    _stats.mesh_draw_time = elapsed.count() / 1000.0f;

    vkCmdEndRendering(cmd);
 
    VkRenderingAttachmentInfo postAttachment = vkinit::attachment_info(_postProcessingImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo postInfo = vkinit::rendering_info(_drawExtent, &postAttachment, nullptr);
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkutil::transition_image(cmd, _postProcessingImage.image, imageBarrier);
    }
    {
        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkutil::transition_image(cmd, _drawImage.image, imageBarrier);
    }
    vkCmdBeginRendering(cmd, &postInfo);

    struct UniformBlock {
        glm::vec2 inverseScreenSize;
    } uniformBlock;

    uniformBlock.inverseScreenSize = glm::vec2(1 / _drawImage.imageExtent.width, 1/ _drawImage.imageExtent.height);

    AllocatedBuffer postProcessingBuffer = create_buffer(sizeof(UniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
 
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(postProcessingBuffer);
    });
    
    UniformBlock* uniformBlockSubmit = (UniformBlock*)postProcessingBuffer.allocation->GetMappedData();
    *uniformBlockSubmit = uniformBlock;

    VkDescriptorSet postProcessingDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _postProcessingDescriptorLayout);

    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    writer.write_sampler(1, _defaultSamplerLinear, VK_DESCRIPTOR_TYPE_SAMPLER);
    writer.write_buffer(2, postProcessingBuffer.buffer, sizeof(UniformBlock), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, postProcessingDescriptor);


	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _postProcessingPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _postProcessingPipelineLayout,
		0, 1, &postProcessingDescriptor, 0, nullptr);

	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = (float)_drawExtent.width;
	viewport.height = (float)_drawExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = _drawExtent.width;
	scissor.extent.height = _drawExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmd)
{
    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(_mainDrawContext.OpaqueSurfaces.size());

    for (uint32_t i = 0; i < _mainDrawContext.OpaqueSurfaces.size(); i++) {
        // if (vkutil::is_visible(_mainDrawContext.OpaqueSurfaces[i], _sceneData.viewproj)) {
            opaque_draws.push_back(i); 
        // }
    }

    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
        const RenderObject& A = _mainDrawContext.OpaqueSurfaces[iA];
        const RenderObject& B = _mainDrawContext.OpaqueSurfaces[iB];
        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        }
        else {
            return A.material < B.material;
        }
    });


    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
    });

    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = _sceneData;
 
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.update_set(_device, globalDescriptor);

    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE; 

    VkDescriptorSet rtDescriptorSet = create_top_level_as();

    auto draw = [&](const RenderObject& r) {
        if (r.material != lastMaterial) {

            lastMaterial = r.material;

            if (r.material->pipeline != lastPipeline) {

                lastPipeline = r.material->pipeline;

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout,
                    0, 1, &globalDescriptor, 0, nullptr);
                
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout,
                    2, 1, &rtDescriptorSet, 0, nullptr);

                VkViewport viewport = {};
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = (float)_drawExtent.width;
                viewport.height = (float)_drawExtent.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;

                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = _drawExtent.width;
                scissor.extent.height = _drawExtent.height;

                vkCmdSetScissor(cmd, 0, 1, &scissor);
            }

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout,
                1, 1, &r.material->materialSet, 0, nullptr);
        }

        if (r.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32); 
        }

        GPUDrawPushConstants pushConstants;
        pushConstants.worldMatrix = r.transform;
        pushConstants.vertexBuffer = r.vertexBufferAddress;
        vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);

        _stats.draw_call_count++;
        _stats.triangle_count += r.indexCount / 3;
    };

    _stats.draw_call_count = 0;
    _stats.triangle_count = 0;

    for (auto& r : opaque_draws) {
        draw(_mainDrawContext.OpaqueSurfaces[r]);
    }

    for (auto& r : _mainDrawContext.TransparentSurfaces) {
        draw(r);
    }
 
    _mainDrawContext.OpaqueSurfaces.clear();
    _mainDrawContext.TransparentSurfaces.clear(); 
}

AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{

    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped) {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }
 
    if (format == VK_FORMAT_D32_SFLOAT) {
        img_info.samples = VK_SAMPLE_COUNT_4_BIT;
    }

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocInfo, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = size.depth * size.width * size.height * 4;
    AllocatedBuffer uploadBuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(uploadBuffer.info.pMappedData, data, data_size);

    AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
        {
            VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            imageBarrier.pNext = nullptr;
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
            imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT; 
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkutil::transition_image(cmd, new_image.image, imageBarrier);
        }
        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;
        vkCmdCopyBufferToImage(cmd, uploadBuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (mipmapped) {
            vkutil::generate_mipmaps(cmd, new_image.image, VkExtent2D{ new_image.imageExtent.width, new_image.imageExtent.height });
        }
        else {
            VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            imageBarrier.pNext = nullptr;
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
            imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT; 
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; 
            imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkutil::transition_image(cmd, new_image.image, imageBarrier); 
        } 
    }); 
    destroy_buffer(uploadBuffer);

    return new_image;
}

AllocatedAS VulkanEngine::create_accel_struct(const VkAccelerationStructureCreateInfoKHR &accel)
{
    AllocatedAS as;

    as.buffer = create_buffer(
        accel.size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    VkAccelerationStructureCreateInfoKHR createInfo = accel;
    createInfo.buffer = as.buffer.buffer;

    vkCreateAccelerationStructureKHR(_device, &createInfo, nullptr, &as.accel);

    if (vkGetAccelerationStructureDeviceAddressKHR != nullptr) {
        VkAccelerationStructureDeviceAddressInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        info.accelerationStructure = as.accel;
        as.address = vkGetAccelerationStructureDeviceAddressKHR(_device, &info);
    }

    return as;
}

void VulkanEngine::destroy_accel_struct(const AllocatedAS &accel) {
    vkDestroyAccelerationStructureKHR(_device, accel.accel, nullptr);
    destroy_buffer(accel.buffer);
}

void VulkanEngine::destroy_image(const AllocatedImage& img)
{
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}

void VulkanEngine::update_scene()
{
    auto start = std::chrono::system_clock::now();

    _mainCamera.update();

    _stats.camera_location = _mainCamera.position;
    _sceneData.view = _mainCamera.getViewMatrix();

#ifndef AVI_DISABLE_INTERCHANGE 
    ShmemString cameraName("CameraBasis", _interprocess->_segment.get_allocator<ShmemString>());
    Transform cameraBasisTrans = _interprocess->_map->at(cameraName);
    glm::mat4 cameraBasis = glm::make_mat4(cameraBasisTrans.array);
    _sceneData.view *= cameraBasis;
#endif // AVI_DISABLE_INTERCHANGE

    glm::mat4 inverse = glm::inverse(_sceneData.view);
    glm::vec3 front = glm::normalize(glm::vec3(inverse[2]));
    _sceneData.cameraPos = inverse[3];
    _sceneData.lightCutoff = glm::cos(glm::radians(lightCutoffRad)); 
    _sceneData.lightOuterCutoff = glm::cos(glm::radians(lightOuterCutoffRad)); 
    _sceneData.cameraDir = glm::vec4(glm::normalize(front), 1.0f);

    float aspectRatio = (float)_drawExtent.width / (float)_drawExtent.height;
    if (aspectRatio != aspectRatio) return;
    _sceneData.proj = glm::perspective(glm::radians(70.0f), aspectRatio, 10000.0f, 0.001f);
    _sceneData.proj[1][1] *= 1; //might need to change to -1
    _sceneData.viewproj = _sceneData.proj * _sceneData.view;

#ifndef AVI_DISABLE_INTERCHANGE
    for (auto node : _loadedScenes[sceneString]->nodes) {
        ShmemString name(node.first.c_str(), _interprocess->_segment.get_allocator<ShmemString>());
        Transform trans = _interprocess->_map->at(name);
        glm::mat4 transform = glm::make_mat4(trans.array);
        node.second->worldTransform = transform;
        _instances[_nodeNameToInstanceIndexMap[node.first]].transform = node.second->worldTransform; // move back into first loop when proper change of basis matrix
    } 
#endif // AVI_DISABLE_INTERCHANGE

    _loadedScenes[sceneString]->Draw(glm::mat4{ 1.0f }, _mainDrawContext);

    auto end = std::chrono::system_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    _stats.scene_update_time = elapsed.count() / 1000.0f;
}

VkSampleCountFlagBits VulkanEngine::getMaxUsableSampleCount()
{
    VkSampleCountFlags counts = _gpuProperties.limits.framebufferColorSampleCounts & _gpuProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
}

void VulkanEngine::init_post_process_pipelines()
{
    VkShaderModule postFragShader; 
    if (!vkutil::load_shader_module("../shaders/post_process.frag.spv", _device, &postFragShader)) {
        std::cout << "Error when building the triangle fragment shader module" << std::endl;
    }

    VkShaderModule postVertShader;
    if (!vkutil::load_shader_module("../shaders/post_process.vert.spv", _device, &postVertShader)) {
        std::cout << "Error when building the triangle vertex shader module" << std::endl; 
    }

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER); 

    _postProcessingDescriptorLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineLayoutCreateInfo post_layout_info = vkinit::pipeline_layout_create_info();
    post_layout_info.setLayoutCount = 1;
    post_layout_info.pSetLayouts = &_postProcessingDescriptorLayout;

    VK_CHECK(vkCreatePipelineLayout(_device, &post_layout_info, nullptr, &_postProcessingPipelineLayout)); 

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(postVertShader, postFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depth_test();
    pipelineBuilder.set_color_attachment_format(_postProcessingImage.imageFormat);
    pipelineBuilder.set_depth_format(VK_FORMAT_UNDEFINED); 

    pipelineBuilder._pipelineLayout = _postProcessingPipelineLayout;

    VK_CHECK(pipelineBuilder.build_pipeline(_device, _postProcessingPipeline));

    vkDestroyShaderModule(_device, postFragShader, nullptr);
    vkDestroyShaderModule(_device, postVertShader, nullptr);
    
    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipeline(_device, _postProcessingPipeline, nullptr);
        vkDestroyPipelineLayout(_device, _postProcessingPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _postProcessingDescriptorLayout, nullptr);
    }); 
}

void VulkanEngine::init_ray_tracing()
{
    create_bottom_level_as();
}

void VulkanEngine::cleanup_ray_tracing()
{
    for (auto& b : _blas) {
        destroy_accel_struct(b);
    }
}

BLASInput VulkanEngine::mesh_to_vk_geometry(const MeshAsset &mesh)
{
    VkBufferDeviceAddressInfo indexAddressInfo{};
    indexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    indexAddressInfo.buffer = mesh.meshBuffers.indexBuffer.buffer;
    VkDeviceAddress indexAddress = vkGetBufferDeviceAddress(_device, &indexAddressInfo);
    VkDeviceAddress vertexAddress = mesh.meshBuffers.vertexBufferAddress;
    uint32_t maxPrimitiveCount = mesh.indexCount / 3;

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertexAddress;
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = mesh.vertexCount - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indexAddress;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.triangles = triangles;

    VkAccelerationStructureBuildRangeInfoKHR offset{}; // blas will always have only 1 geometry for now
    offset.firstVertex = 0;
    offset.primitiveCount = maxPrimitiveCount;
    offset.primitiveOffset = 0;
    offset.transformOffset = 0;

    BLASInput input;
    input.geom.emplace_back(geom); //vector size of 1 because of only 1 geometry per blas
    input.buildRangeInfo.emplace_back(offset); // vector size of 1 because of only 1 geometry per blas
    input.name = mesh.name;

    return input;
}

void VulkanEngine::create_bottom_level_as()
{
    std::vector<BLASInput> inputs;
    inputs.reserve(_loadedScenes[sceneString]->meshes.size());
    std::unordered_map<std::string, uint32_t> nameIndexMap;
    for (const auto& mesh : _loadedScenes[sceneString]->meshes) {
        BLASInput blas = mesh_to_vk_geometry(*mesh.second);
        //only one geometry per blas for now
        nameIndexMap[mesh.first] = inputs.size();
        inputs.emplace_back(blas);
    }
    VkDeviceSize allBlasMemSize{0};
    uint32_t compactionRequests{0};
    VkDeviceSize maxScratchSize{0};

    std::vector<ASBuildData> asBuilds(inputs.size());
    for (uint32_t i = 0; i < inputs.size(); i++) {
        asBuilds[i].buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        asBuilds[i].buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        asBuilds[i].buildInfo.flags = inputs[i].flags
                                    | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR // should eventually be per model
                                    // | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR; // should eventually be per model
                                    ;
        asBuilds[i].buildInfo.geometryCount = static_cast<uint32_t>(inputs[i].geom.size());
        asBuilds[i].buildInfo.pGeometries = inputs[i].geom.data();

        asBuilds[i].buildRangeInfo = inputs[i].buildRangeInfo;

        std::vector<uint32_t> maxPrimCount(inputs[i].buildRangeInfo.size());
        for (uint32_t j = 0; j < inputs[i].buildRangeInfo.size(); j++) {
            maxPrimCount[j] = inputs[i].buildRangeInfo[j].primitiveCount;
        }
        vkGetAccelerationStructureBuildSizesKHR(
            _device, 
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &asBuilds[i].buildInfo,
            maxPrimCount.data(), 
            &asBuilds[i].sizeInfo
        );

        allBlasMemSize += asBuilds[i].sizeInfo.accelerationStructureSize;
        maxScratchSize = std::max(maxScratchSize, asBuilds[i].sizeInfo.buildScratchSize);
        if (asBuilds[i].buildInfo.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) {
            compactionRequests++;
        }
    }
    if (maxScratchSize % _asProperties.minAccelerationStructureScratchOffsetAlignment != 0) {
        maxScratchSize += (maxScratchSize % _asProperties.minAccelerationStructureScratchOffsetAlignment);
    }
    AllocatedBuffer scratchBuffer = create_buffer(
        maxScratchSize, 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
        VMA_MEMORY_USAGE_GPU_ONLY,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    );
    VkBufferDeviceAddressInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, scratchBuffer.buffer};
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(_device, &bufferInfo);

    VkQueryPool queryPool{VK_NULL_HANDLE};
    if (compactionRequests > 0) {
        assert(compactionRequests == asBuilds.size());
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryCount = asBuilds.size();
        info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        vkCreateQueryPool(_device, &info, nullptr, &queryPool);
    }

    std::vector<uint32_t> indices;
    VkDeviceSize batchSize{0};
    VkDeviceSize batchLimit{256'000'000};
    for (uint32_t i = 0; i < asBuilds.size(); i++) {
        indices.push_back(i);
        batchSize += asBuilds[i].sizeInfo.accelerationStructureSize;
        if (batchSize >= batchLimit || i == asBuilds.size() - 1) {
            // build blas
            if (queryPool) {
                vkResetQueryPool(_device, queryPool, 0, static_cast<uint32_t>(indices.size()));
            }
            uint32_t queryCount{0};
            async_compute_submit([&](VkCommandBuffer cmd) {
                for (const auto& i : indices) {
                    VkAccelerationStructureCreateInfoKHR createInfo{};
                    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                    createInfo.size = asBuilds[i].sizeInfo.accelerationStructureSize;
                    asBuilds[i].as = create_accel_struct(createInfo);

                    asBuilds[i].buildInfo.dstAccelerationStructure = asBuilds[i].as.accel;
                    asBuilds[i].buildInfo.scratchData.deviceAddress = scratchAddress;

                    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> rangeData;
                    for (auto& data : asBuilds[i].buildRangeInfo) {
                        rangeData.emplace_back(&data);
                    }

                    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &asBuilds[i].buildInfo, rangeData.data());
                    VkMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        0, 1, &barrier, 0, nullptr, 0, nullptr
                    );
                    if (queryPool) {
                        vkCmdWriteAccelerationStructuresPropertiesKHR(
                            cmd, 1, &asBuilds[i].buildInfo.dstAccelerationStructure,
                            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, queryCount++
                        );
                    }
                }
            });
            if (queryPool) {
                async_compute_submit([&](VkCommandBuffer cmd) {
                // compact blas
                    uint32_t queryCount{0};
                    std::vector<VkDeviceSize> compactSizes(static_cast<uint32_t>(indices.size()));
                    vkGetQueryPoolResults(_device, queryPool, 0, (uint32_t)compactSizes.size(),
                        compactSizes.size() * sizeof(VkDeviceSize), compactSizes.data(), sizeof(VkDeviceSize),
                        VK_QUERY_RESULT_WAIT_BIT
                    );
                    for (auto i : indices) {
                        asBuilds[i].cleanupAS = asBuilds[i].as;
                        asBuilds[i].sizeInfo.accelerationStructureSize = compactSizes[queryCount++];

                        VkAccelerationStructureCreateInfoKHR createInfo{};
                        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                        createInfo.size = asBuilds[i].sizeInfo.accelerationStructureSize;
                        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                        asBuilds[i].as = create_accel_struct(createInfo);

                        VkCopyAccelerationStructureInfoKHR copyInfo{};
                        
                        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
                        copyInfo.src = asBuilds[i].buildInfo.dstAccelerationStructure;
                        copyInfo.dst = asBuilds[i].as.accel;
                        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                        
                    
                        vkCmdCopyAccelerationStructureKHR(cmd, &copyInfo);
                   }
                });
                // destroy noncompactblas
                for (auto& blas : asBuilds) {
                    destroy_accel_struct(blas.cleanupAS);
                }
            }
            batchSize = 0;
            indices.clear();
        }
    }
    for (uint32_t i = 0; i < asBuilds.size(); i++) {
        _blas.emplace_back(asBuilds[i].as);
    }
    for (auto node : _loadedScenes[sceneString]->meshNodes) {
        MeshNode* meshNode = static_cast<MeshNode*>(node.second.get());
        MeshInstance instance;
        instance.meshIndex = nameIndexMap[meshNode->mesh->name];
        instance.transform = meshNode->worldTransform;
        _nodeNameToInstanceIndexMap[node.first] = _instances.size();
        _instances.emplace_back(instance);
    }
    vkDestroyQueryPool(_device, queryPool, nullptr);
    destroy_buffer(scratchBuffer);
}

VkDescriptorSet VulkanEngine::create_top_level_as()
{
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    asInstances.reserve(_instances.size()); 
    for (uint32_t i = 0; i < _instances.size(); i++) {
        VkAccelerationStructureInstanceKHR rayInst{};
        rayInst.transform = vkutil::toTransformMatrixKHR(_instances[i].transform);
        rayInst.instanceCustomIndex = i;
        rayInst.accelerationStructureReference = _blas[_instances[i].meshIndex].address;
        rayInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        rayInst.mask = 0xFF;
        rayInst.instanceShaderBindingTableRecordOffset = 0; // all same hit group for now
        asInstances.emplace_back(rayInst);
    }

    uint32_t instanceCount = static_cast<uint32_t>(_instances.size());
    size_t instanceBufferSize = _instances.size()*sizeof(MeshInstance);
    AllocatedBuffer instancesBuffer = create_buffer(
        instanceBufferSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    AllocatedBuffer staging = create_buffer(
        instanceBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    void* data = staging.allocation->GetMappedData();

    memcpy(data, asInstances.data(), instanceBufferSize);

    async_compute_submit([&](VkCommandBuffer cmd) { // usually put on a background thread that solely executes uploads, deleting/reusing the stageing buffers
        VkBufferCopy bufferCopy{ 0 };
        bufferCopy.dstOffset = 0;
        bufferCopy.srcOffset = 0;
        bufferCopy.size = instanceBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, instancesBuffer.buffer, 1, &bufferCopy);
    }); // otherwise the cpu is waiting until the gpu is done 

    destroy_buffer(staging);

    VkBufferDeviceAddressInfo instancesBufferInfo{};
    instancesBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instancesBufferInfo.buffer = instancesBuffer.buffer;
    VkDeviceAddress instancesBufferAddress = vkGetBufferDeviceAddress(_device, &instancesBufferInfo);

    async_compute_submit([&](VkCommandBuffer cmd) {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr
        );
    });

    VkAccelerationStructureGeometryInstancesDataKHR geomInstances{};
    geomInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geomInstances.data.deviceAddress = instancesBufferAddress;

    VkAccelerationStructureGeometryKHR topASGeom{};
    topASGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    topASGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    topASGeom.geometry.instances = geomInstances;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &topASGeom;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &instanceCount, &sizeInfo
    );

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    createInfo.size = sizeInfo.accelerationStructureSize;
    AllocatedAS tlas = create_accel_struct(createInfo); // only one tlas for now
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_accel_struct(tlas);
    });

    AllocatedBuffer scratchBuffer = create_buffer(
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    );
    VkBufferDeviceAddressInfo scratchBufferInfo{};
    scratchBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferInfo.buffer = scratchBuffer.buffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(_device, &scratchBufferInfo);

    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = tlas.accel; // only one tlas for now
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{instanceCount, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

    async_compute_submit([&](VkCommandBuffer cmd) {
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pBuildOffsetInfo);
    });

    destroy_buffer(scratchBuffer);
    destroy_buffer(instancesBuffer);

    VkDescriptorSet rtDescriptorSet =  get_current_frame()._frameDescriptors.allocate(_device, _rtDescriptorSetLayout);

    DescriptorWriter writer;
    writer.write_accel_struct(0, tlas.accel);
    writer.write_image(1, _rtDrawImage.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(_device, rtDescriptorSet);
    return rtDescriptorSet;
}

void VulkanEngine::create_rt_descriptor_set()
{
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    _rtDescriptorSetLayout = builder.build(
        _device,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR
        | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        | VK_SHADER_STAGE_VERTEX_BIT
        | VK_SHADER_STAGE_FRAGMENT_BIT
    );
    _mainDeletionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(_device, _rtDescriptorSetLayout, nullptr);
    });
}
void VulkanEngine::init_interprocess()
{
    _interprocess = std::make_shared<Interprocess>(_loadedScenes[sceneString]->nodes);
}

void GLTFMetallic_Roughness::build_pipelines(VulkanEngine* engine)
{
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("../shaders/pbr.frag.spv", engine->_device, &meshFragShader)) {
        std::cout << "Error when building the triangle fragment shader module" << std::endl;
    }

    VkShaderModule meshVertShader;
    if (!vkutil::load_shader_module("../shaders/pbr.vert.spv", engine->_device, &meshVertShader)) {
        std::cout << "Error when building the triangle vertex shader module" << std::endl; 
    }

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(
        engine->_device, 
        VK_SHADER_STAGE_VERTEX_BIT
        | VK_SHADER_STAGE_FRAGMENT_BIT
        | VK_SHADER_STAGE_RAYGEN_BIT_KHR
        | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
    );

    // VkDescriptorSetLayout layouts[] = { engine->_gpuSceneDataDescriptorLayout, materialLayout};
    VkDescriptorSetLayout layouts[] = { engine->_gpuSceneDataDescriptorLayout, materialLayout, engine->_rtDescriptorSetLayout };

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    // mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.setLayoutCount = 3;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(engine->_device, &mesh_layout_info, nullptr, &gltfPipelineLayout));

    opaquePipeline.layout = gltfPipelineLayout;
    transparentPipeline.layout = gltfPipelineLayout;

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.enable_multisampling(engine->_msaaSampleCount);
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.set_color_attachment_format(engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(engine->_depthImage.imageFormat);

    pipelineBuilder._pipelineLayout = gltfPipelineLayout;

    VK_CHECK(pipelineBuilder.build_pipeline(engine->_device, opaquePipeline.pipeline));
    
    pipelineBuilder.enable_blending_additive(); 
    pipelineBuilder.enable_depth_test(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    VK_CHECK(pipelineBuilder.build_pipeline(engine->_device, transparentPipeline.pipeline));

    vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_device, meshVertShader, nullptr);
}

void GLTFMetallic_Roughness::clear_resources(VkDevice device)
{
    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
    vkDestroyPipelineLayout(device, gltfPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator)
{
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent) {
        matData.pipeline = &transparentPipeline;
    }
    else {
        matData.pipeline = &opaquePipeline;
    }
        matData.pipeline = &opaquePipeline; // DEBUGGGING PURPOSES

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(3, resources.normalImage.imageView, resources.normalSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    writer.update_set(device, matData.materialSet);

    return matData;
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto& s : mesh->surfaces) {
        RenderObject def;
        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        if (s.material->data.passType == MaterialPass::Transparent) {
            ctx.TransparentSurfaces.push_back(def);
        }
        else {
            ctx.OpaqueSurfaces.push_back(def); 
        }
    }

    Node::Draw(topMatrix, ctx);
}

bool vkutil::is_visible(const RenderObject& obj, const glm::mat4& viewProj)
{
    std::array<glm::vec3, 8> corners{
        glm::vec3 {  1,  1,  1 },
        glm::vec3 {  1,  1, -1 },
        glm::vec3 {  1, -1,  1 },
        glm::vec3 {  1, -1, -1 },
        glm::vec3 { -1,  1,  1 },
        glm::vec3 { -1,  1, -1 },
        glm::vec3 { -1, -1,  1 },
        glm::vec3 { -1, -1, -1 },
    };

    glm::mat4 matrix = viewProj * obj.transform;

    glm::vec3 min{ 1.5, 1.5, 1.5 };
    glm::vec3 max{ -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.0f);

        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(glm::vec3{ v.x, v.y, v.z }, min);
        max = glm::max(glm::vec3{ v.x, v.y, v.z }, max);
    }

    if (min.z > 1.0f || max.z < 0.0f || min.x > 1.0f || max.x < -1.0f || min.y > 1.0f || max.y < -1.0f) {
        return false;
    }
    else {
        return true;
    }
}

VkTransformMatrixKHR vkutil::toTransformMatrixKHR(const glm::mat4& matrix)
{
  // VkTransformMatrixKHR uses a row-major memory layout, while glm::mat4
  // uses a column-major memory layout. We transpose the matrix so we can
  // memcpy the matrix's data directly.
  glm::mat4 temp = glm::transpose(matrix);
  VkTransformMatrixKHR out_matrix;
  memcpy(&out_matrix, &temp, sizeof(VkTransformMatrixKHR));
  return out_matrix;
}
