// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_loader.h"
#include "vk_pipelines.h"
#include "camera.h"
#include "interprocess.h"

#include <vk_mem_alloc.h>

#include <span>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <unordered_map>
#include <memory>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#define AMBF_BUILD_DIR @AMBF_BUILD_DIR@

struct EngineStats {
	float frame_time;
	int triangle_count;
	int draw_call_count;
	float scene_update_time;
	float mesh_draw_time;
	glm::vec3 camera_location;
};

struct GUITransform {
	float guiTransform[3];
	float guiSunDir[3];
};

struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};

struct DeletionQueue {
	
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) // inefficient at scale... store arrays of vulkan handles of various types, then delete from loop
	{
		deletors.push_back(function);
	}

	void flush()
	{
		for (auto func : deletors)
		{
			func();
		}

		deletors.clear();
	}
};

struct FrameData {

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
	VkSemaphore _swapchainSemaphore;
	VkSemaphore _renderSemaphore;
	VkFence _renderFence;
	DeletionQueue _deletionQueue;
	DescriptorAllocatorGrowable _frameDescriptors;
};

struct MeshNode : public Node {

	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};

struct RenderObject {
	uint32_t indexCount;
	uint32_t firstIndex;
	VkBuffer indexBuffer;

	MaterialInstance* material;
	Bounds bounds;
	glm::mat4 transform;
	VkDeviceAddress vertexBufferAddress;
};

struct DrawContext {
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
};

struct GLTFMetallic_Roughness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;
	VkPipelineLayout gltfPipelineLayout;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		//padding, we need it anyway for uniform buffers.. struct should be 256 bytes for gpu alignment
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		AllocatedImage normalImage;
		VkSampler normalSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void build_pipelines(VulkanEngine* engine);
	void clear_resources(VkDevice device);

	MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};

struct BLASInput {
	std::vector<VkAccelerationStructureGeometryKHR> geom;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfo;
	VkBuildAccelerationStructureFlagsKHR flags{0};
	std::string name;
};
struct ASBuildData {
	VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
	std::vector<VkAccelerationStructureGeometryKHR> geom;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfo;
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	AllocatedAS as;
	AllocatedAS cleanupAS;
};

struct MeshInstance {
	glm::mat4 transform;
	uint32_t meshIndex{0};
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber {0};
	bool _freeze_rendering{ false };
	bool _resize_requested;
	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr }; 

	static VulkanEngine& Get();

	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };
 
	//initializes everything in the engine
	void init(); 
	//shuts down the engine
	void cleanup(); 
	//draw loop
	void draw(); 
	//run main loop
	void run();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	void async_compute_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkDevice _device;
	VkSurfaceKHR _surface;
	VkPhysicalDeviceProperties _gpuProperties;
	VkSampleCountFlagBits _msaaSampleCount;
	
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	FrameData _frames[FRAME_OVERLAP];
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	VkQueue _asyncComputeQueue;
	uint32_t _asyncComputeQueueFamily;
	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;

	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;
	float _renderScale = 1.0f;
	
	DescriptorAllocatorGrowable _globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	VkFence _asyncComputeFence;
	VkCommandBuffer _asyncComputeCommandBuffer;
	VkCommandPool _asyncComputeCommandPool;

	std::vector<ComputeEffect> _backgroundEffects;
	int _currentBackgroundEffect{ 1 };
 
	GPUSceneData _sceneData;

	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

	VkDescriptorSetLayout _postProcessingDescriptorLayout;
	VkPipelineLayout _postProcessingPipelineLayout;
	VkPipeline _postProcessingPipeline;
	AllocatedImage _postProcessingImage;
	VkDescriptorSet _postProcessingDescriptors;

	AllocatedImage _msaaDrawImage;
	AllocatedImage _msaaDepthImage;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;
 
	GLTFMetallic_Roughness _metalRoughMaterial;

	DrawContext _mainDrawContext;

	Camera _mainCamera;

	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> _loadedScenes;

	EngineStats _stats;

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR _rtProperties{};
	VkPhysicalDeviceAccelerationStructurePropertiesKHR _asProperties{};
	std::vector<AllocatedAS> _blas;
	std::vector<MeshInstance> _instances;
	std::unordered_map<std::string, uint32_t> _nodeNameToInstanceIndexMap;
	VkDescriptorSetLayout _rtDescriptorSetLayout;
	AllocatedImage _rtDrawImage;
	AllocatedImage _rtDepthImage;
	float lightColor[3];
	float lightCutoffRad;
	float lightOuterCutoffRad;
	float lightPos[3];

	GUITransform _guiTransform{};
	std::shared_ptr<Interprocess> _interprocess;

	std::shared_ptr<ImGuiIO> _io;

	std::vector<GPUMeshBuffers> meshesToDelete;

	VkPushConstantRange _computePushConstantRange{};

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlagBits allocFlags);
    void destroy_buffer(const AllocatedBuffer &buffer);
    AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);
	AllocatedAS create_accel_struct(const VkAccelerationStructureCreateInfoKHR& accel);
	void destroy_accel_struct(const AllocatedAS& accel);
	

private:
	
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_async_compute_commands();
	void init_sync_structures();
	void init_async_compute_sync_structures();
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();
	void init_imgui();
	void init_default_data();
	void init_renderables();
	void init_post_process_pipelines();

	void init_ray_tracing();
	void cleanup_ray_tracing();
	BLASInput mesh_to_vk_geometry(const MeshAsset &obj);
	void create_bottom_level_as();
	VkDescriptorSet create_top_level_as();
	void create_rt_descriptor_set();

	void init_interprocess();


	void create_swapchain(uint32_t width, uint32_t hegiht);
	void destroy_swapchain();
	void resize_swapchain();
	
	void draw_main(VkCommandBuffer cmd);
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
	void draw_geometry(VkCommandBuffer cmd);


	void update_scene();

	VkSampleCountFlagBits getMaxUsableSampleCount();
};
namespace vkutil {
	bool is_visible(const RenderObject& obj, const glm::mat4& viewProj);
	VkTransformMatrixKHR toTransformMatrixKHR(const glm::mat4& matrix);
}