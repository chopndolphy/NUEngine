
#pragma once 

#include <volk.h>
#include "vk_types.h"

namespace vkutil {

	void transition_image(VkCommandBuffer cmd, VkImage image, VkImageMemoryBarrier2 imageBarrier);
	void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
	void generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
};