// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vector>
#include <vk_types.h>

#include "vk_mem_alloc.h"

struct FrameData
{
	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	VkSemaphore swapchainSemaphore, renderSemaphore;
	VkFence renderFence;

	DeletionQueue frameDeletionQueue;
};

constexpr int FRAME_OVERLAP = 2;

class VulkanEngine
{
public:
	bool isInitialized{ false };
	int frameNumber{ 0 };

	VkExtent2D windowExtent{ 1700 , 900 };

	struct SDL_Window* window{ nullptr };

	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkPhysicalDevice chosenGpu;
	VkDevice device;
	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;
	VkFormat swapchainImageFormat;

	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkExtent2D swapchainExtent;

	FrameData frames[FRAME_OVERLAP];
	FrameData& get_current_frame();

	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;

	DeletionQueue mainDeletionQueue;

	VmaAllocator allocator;

	AllocatedImage drawImage;
	VkExtent2D drawImageExtent;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();

	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();

	void draw_background(VkCommandBuffer cmd) const;
};
