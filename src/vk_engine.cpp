#include "vk_engine.h"

#include <iostream>
#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_images.h>

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

constexpr bool bUseValidationLayers = true;

FrameData& VulkanEngine::get_current_frame()
{
	return frames[frameNumber % FRAME_OVERLAP];
}


void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	constexpr auto windowFlags = SDL_WINDOW_VULKAN;

	window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowExtent.width,
		windowExtent.height,
		windowFlags
	);

	init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();

	//everything went fine
	isInitialized = true;
}

void VulkanEngine::cleanup()
{
	if (isInitialized)
	{
		vkDeviceWaitIdle(device);

		mainDeletionQueue.flush();

		for (const auto& frame : frames)
		{
			vkDestroyCommandPool(device, frame.commandPool, nullptr);

			// destroy sync objects
			vkDestroySemaphore(device, frame.swapchainSemaphore, nullptr);
			vkDestroySemaphore(device, frame.renderSemaphore, nullptr);
			vkDestroyFence(device, frame.renderFence, nullptr);
		}

		destroy_swapchain();

		vkDestroySurfaceKHR(instance, surface, nullptr);
		vkDestroyDevice(device, nullptr);

		vkb::destroy_debug_utils_messenger(instance, debugMessenger);
		vkDestroyInstance(instance, nullptr);

		SDL_DestroyWindow(window);
	}
}

constexpr int OPERATION_TIMEOUT = 1000000000;

void VulkanEngine::draw()
{
	auto currentFrame = get_current_frame();

	VK_CHECK(vkWaitForFences(device, 1, &currentFrame.renderFence, true, OPERATION_TIMEOUT));
	currentFrame.frameDeletionQueue.flush();
	VK_CHECK(vkResetFences(device, 1, &currentFrame.renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(
		device,
		swapchain,
		OPERATION_TIMEOUT,
		currentFrame.swapchainSemaphore,
		nullptr,
		&swapchainImageIndex));

	const auto currentSwapchainImage = swapchainImages[swapchainImageIndex];

	const auto cmd = currentFrame.mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	const auto cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	draw_background(cmd);

	vkutil::transition_image(
		cmd,
		drawImage.image,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transition_image(
		cmd,
		currentSwapchainImage,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	vkutil::copy_image_to_image(
		cmd,
		drawImage.image,
		currentSwapchainImage,
		VkExtent2D{ drawImage.imageExtent.width , drawImage.imageExtent.height },
		swapchainExtent);

	vkutil::transition_image(
		cmd,
		currentSwapchainImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	auto cmdInfo = vkinit::command_buffer_submit_info(cmd);

	auto waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
	                                              currentFrame.swapchainSemaphore);
	auto signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, currentFrame.renderSemaphore);

	const auto submitInfo = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, currentFrame.renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo));
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	//main loop
	while (!bQuit)
	{
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT) bQuit = true;

			if (e.type == SDL_KEYDOWN)
			{
				if (e.key.keysym.sym == SDLK_h)
				{
					std::cout << "Pressed H" << std::endl;
				}

				if (e.key.keysym.sym == SDLK_q)
				{
					std::cout << "Quitting..." << std::endl;
					bQuit = true;
				}
			}
		}

		draw();

		frameNumber ++;
	}
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto instRet = builder
	               .set_app_name("My Vulkan App")
	               .request_validation_layers(bUseValidationLayers)
	               .use_default_debug_messenger()
	               .require_api_version(1, 3, 0)
	               .build();

	const auto vkbInst = instRet.value();

	// grab values
	instance = vkbInst.instance;
	debugMessenger = vkbInst.debug_messenger;

	SDL_Vulkan_CreateSurface(window, instance, &surface);

	// VK 1.3 features
	VkPhysicalDeviceVulkan13Features features13{};
	features13.dynamicRendering = true;
	features13.synchronization2 = true;

	// VK 1.2 features
	VkPhysicalDeviceVulkan12Features features12{};
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	VkPhysicalDeviceFeatures deviceFeatures{};

	// Select gpu
	vkb::PhysicalDeviceSelector selector{ vkbInst };
	vkb::PhysicalDevice physicalDevice = selector
	                                     .set_minimum_version(1, 3)
	                                     .set_surface(surface)
	                                     .set_required_features_12(features12)
	                                     .set_required_features_13(features13)
	                                     .select()
	                                     .value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();

	// get values
	device = vkbDevice.device;
	chosenGpu = physicalDevice.physical_device;

	// get graphics queue
	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = chosenGpu;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &allocator);

	mainDeletionQueue.push_function([&]()
	{
		vmaDestroyAllocator(allocator);
	});
}

void VulkanEngine::init_swapchain()
{
	create_swapchain(windowExtent.width, windowExtent.height);

	VkExtent3D drawImageExtent = {
		windowExtent.width ,
		windowExtent.height ,
		1
	};

	drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	drawImage.imageExtent = drawImageExtent;

	constexpr auto drawImageUsages =
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	auto renderImageInfo = vkinit::image_create_info(drawImage.imageFormat, drawImageUsages, drawImageExtent);

	VmaAllocationCreateInfo renderImageAllocInfo = {};
	renderImageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	renderImageAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	vmaCreateImage(
		allocator,
		&renderImageInfo,
		&renderImageAllocInfo,
		&drawImage.image,
		&drawImage.allocation,
		nullptr);

	const auto renderViewInfo = vkinit::imageview_create_info(drawImage.imageFormat, drawImage.image,
	                                                          VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(device, &renderViewInfo, nullptr, &drawImage.imageView));

	mainDeletionQueue.push_function([=]()
	{
		vkDestroyImageView(device, drawImage.imageView, nullptr);
		vmaDestroyImage(allocator, drawImage.image, drawImage.allocation);
	});
}

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo commandPoolInfo =
		vkinit::command_pool_create_info(
			graphicsQueueFamily,
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (auto& frame : frames)
	{
		VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frame.commandPool));

		VkCommandBufferAllocateInfo commandBufferInfo =
			vkinit::command_buffer_allocate_info(frame.commandPool);

		VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferInfo, &frame.mainCommandBuffer));
	}
}

void VulkanEngine::init_sync_structures()
{
	const VkSemaphoreCreateInfo semaphoreInfo = vkinit::semaphore_create_info();
	const VkFenceCreateInfo fenceInfo = vkinit::fence_create_info(true);

	for (auto& frame : frames)
	{
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.renderSemaphore));

		VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence));
	}
}

void VulkanEngine::draw_background(const VkCommandBuffer cmd) const
{
	const float flash = abs(sin(static_cast<float>(frameNumber) / 120.f));
	const VkClearColorValue clearValue = { { 0.0f , 0.0f , flash , 1.0f } };

	const auto clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ chosenGpu , device , surface };

	auto vkbSwapchain = swapchainBuilder
	                    .use_default_format_selection()
	                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
	                    .set_desired_extent(width, height)
	                    .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	                    .build()
	                    .value();

	// get values
	swapchainExtent = vkbSwapchain.extent;
	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
	swapchainImageFormat = vkbSwapchain.image_format;
}

void VulkanEngine::destroy_swapchain()
{
	vkDestroySwapchainKHR(device, swapchain, nullptr);

	// destroy image views
	for (auto imageView : swapchainImageViews)
	{
		vkDestroyImageView(device, imageView, nullptr);
	}
}
