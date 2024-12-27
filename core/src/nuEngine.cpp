#include "nuEngine.h"

#include <SDL_video.h>
#include <vk_mem_alloc.h>
#define VMA_IMPLEMENTATION

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "util/nuInstanceBuilder.h"
#include "util/nuWindowBuilder.h"

void nuEngine::init() {
    volkInitialize();

    init_sdl();

    {
        nuInstanceBuilder instBuilder(_window);
        nuInstanceBuild_ret build = instBuilder.build();
        _instance = build.instance;
        _debugMessenger = build.debug_messenger;
        _physicalDevice = build.physical_device;
        _device = build.device;
        _surface = build.surface;
        _allocator = build.allocator;
        _graphicsQueue = build.graphics_queue;
        _graphicsQueueFamily = build.graphics_queue_family;
        _asyncComputeQueue = build.async_compute_queue;
        _asyncComputeQueueFamily = build.async_compute_queue_family;
    }

    init_swapchain();
}

void nuEngine::run() {
    bool quit = false;

    uint64_t now = SDL_GetPerformanceCounter();
	uint64_t last = 0;

    while (!quit) {
        last = now;
		now = SDL_GetPerformanceCounter();

		float deltaTime = (float)((now - last) / (float)SDL_GetPerformanceFrequency());
        
        SDL_Event event = { 0 };

		while (SDL_PollEvent(&event))
        {
            switch (event.type) {
			case SDL_QUIT:
				quit = true;
				break;
            /*case SDL_WINDOWEVENT:
                switch (event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                    _windowExtent.width = event.window.data1;
                    _windowExtent.height = event.window.data2;
                    init_swapchain();
                }*/
            }
        }
    }
}

void nuEngine::cleanup() {

}

void nuEngine::init_sdl() {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    _windowExtent.width = dm.w * 0.8f;
    _windowExtent.height = dm.h * 0.8f;

    _window = SDL_CreateWindow(
        "NU-Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _windowExtent.width,
        _windowExtent.height,
        windowFlags
    );
    
    //SDL_SetRelativeMouseMode(SDL_TRUE);
}

void nuEngine::init_swapchain()
{
    nuWindowBuilder windowBuilder(_physicalDevice, _device, _surface, _windowExtent, &_swapchainDeletionQueue);
    nuSwapchainBuild_ret build = windowBuilder.buildSwapchain();
    _swapchain = build.swapchain;
    _swapchainImageFormat = build.swapchain_image_format;
    _swapchainImages[0] = build.swapchain_images[0];
    _swapchainImages[1] = build.swapchain_images[1];
    _swapchainImageViews[0] = build.swapchain_image_views[0];
    _swapchainImageViews[1] = build.swapchain_image_views[1];
    _swapchainExtent = build.swapchain_extent;
}

void nuEngine::init_device_properties() {

}
