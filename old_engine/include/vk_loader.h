#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <string>

class VulkanEngine;

struct Bounds
{
	glm::vec3 origin;
	float sphereRadius;
	glm::vec3 extents;
};

struct GLTFMaterial
{
	MaterialInstance data;
};

struct GeoSurface
{
	uint32_t startIndex;
	uint32_t count;
	Bounds bounds;
	std::shared_ptr<GLTFMaterial> material;
};

struct MeshNode;
struct MeshAsset
{
	std::string name;
	std::vector<GeoSurface> surfaces;
	GPUMeshBuffers meshBuffers;
	uint32_t vertexCount;
	uint32_t indexCount;
};

struct LoadedGLTF : public IRenderable
{
public:
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
	std::unordered_map<std::string, std::shared_ptr<Node>> meshNodes;
	std::vector<AllocatedImage> images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	std::vector<std::shared_ptr<Node>> topNodes;

	std::vector<VkSampler> samplers;

	DescriptorAllocatorGrowable descriptorPool;

	AllocatedBuffer materialDataBuffer;

	VulkanEngine *creator;

	virtual void Draw(const glm::mat4 &topMatrix, DrawContext &ctx);

	void clearAll();
};
namespace vkutil
{
	std::optional<std::shared_ptr<LoadedGLTF>> load_gltf(VulkanEngine *engine, std::string_view filePath);
	std::optional<AllocatedImage> load_image(VulkanEngine *engine, fastgltf::Asset &asset, fastgltf::Image &image);
	VkFilter extract_filter(fastgltf::Filter filter);
	VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);
}
