// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <deque>
#include <functional>
#include <vulkan/vulkan.h>

#include "vk_mem_alloc.h"

//we will add our main reusable types here

#define VK_CHECK(x) \
	do { \
		VkResult err = x; \
		if (err) { \
			std::cout << "Detected Vulkan error: " << err << std::endl; \
			abort(); \
		} \
	} while (0)

#define GATE(signal, base) ((base)?(signal):0)

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;
	void push_function(std::function<void()>&& function);
	void flush();
};

struct AllocatedImage
{
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};
