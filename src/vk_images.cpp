#include "vk_images.h"

#include <iostream>
#include <ostream>

#include "vk_initializers.h"

void vkutil::transition_image(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageLayout currentLayout,
	VkImageLayout newLayout)
{
	VkImageMemoryBarrier2 imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageBarrier.pNext = nullptr;

	imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

	imageBarrier.oldLayout = currentLayout;
	imageBarrier.newLayout = newLayout;

	imageBarrier.image = image;
	VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
		                                ? VK_IMAGE_ASPECT_DEPTH_BIT
		                                : VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
	
	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.pNext = nullptr;
	depInfo.imageMemoryBarrierCount = 1;
	depInfo.pImageMemoryBarriers = &imageBarrier;

	vkCmdPipelineBarrier2(cmd, &depInfo);
}

void vkutil::copy_image_to_image(
	VkCommandBuffer cmd, 
	VkImage source, 
	VkImage destination, 
	const VkExtent2D srcSize,
    const VkExtent2D destSize)
{
	VkImageBlit2 blitRegion{};
	blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
	blitRegion.pNext = nullptr;
	
	blitRegion.srcOffsets[0] = VkOffset3D{ 0 , 0 , 0 };
	blitRegion.dstOffsets[0] = VkOffset3D{ 0 , 0 , 0 };

	blitRegion.srcOffsets[1].x = static_cast<int>(srcSize.width);
	blitRegion.srcOffsets[1].y = static_cast<int>(srcSize.height);
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = static_cast<int>(destSize.width);
	blitRegion.dstOffsets[1].y = static_cast<int>(destSize.height);
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{};
	blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
	blitInfo.pNext = nullptr;

	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}

void DeletionQueue::push_function(std::function<void()>&& function)
{
	deletors.push_back(function);
}

void DeletionQueue::flush()
{
	//reverse iterate the deletion queue to execute all the functions
	for (auto it = deletors.rbegin(); it != deletors.rend(); ++it)
	{
		(*it)(); //call functions
	}
	deletors.clear();
}

