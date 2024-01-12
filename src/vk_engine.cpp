#include "vk_engine.h"

#include <iostream>
#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_images.h>
#include <vk_descriptors.h>
#include <vk_pipelines.h>

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

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
	init_descriptors();
	init_pipelines();
	init_imgui();

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

		// imgui new frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(window);
		ImGui::NewFrame();

		//some imgui UI to test
		ImGui::ShowDemoWindow();

		//make imgui calculate internal draw structures
		ImGui::Render();

		draw();

		frameNumber ++;
	}
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const
{
	VK_CHECK(vkResetFences(device, 1, &immFence));
	VK_CHECK(vkResetCommandBuffer(immCommandBuffer, 0));

	const VkCommandBuffer cmd = immCommandBuffer;

	const VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	const VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, immFence));

	VK_CHECK(vkWaitForFences(device, 1, &immFence, true, OPERATION_TIMEOUT));
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

	VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &immCommandPool));

	const auto cmdAllocInfo = vkinit::command_buffer_allocate_info(immCommandPool);

	VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &immCommandBuffer));

	mainDeletionQueue.push_function([=]()
	{
		vkDestroyCommandPool(device, immCommandPool, nullptr);
	});
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

	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &immFence));
	mainDeletionQueue.push_function([=]()
	{
		vkDestroyFence(device, immFence, nullptr);
	});
}

void VulkanEngine::init_descriptors()
{
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE , 1 }
	};

	globalDescriptorAllocator.init_pool(device, 10, sizes);

	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		drawImageDescriptorLayout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	drawImageDescriptors = globalDescriptorAllocator.allocate(device, drawImageDescriptorLayout);

	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageView = drawImage.imageView;
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet drawImageWrite{};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;

	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = drawImageDescriptors;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(device, 1, &drawImageWrite, 0, nullptr);
}

void VulkanEngine::init_pipelines()
{
	init_background_pipelines();
}

void VulkanEngine::init_background_pipelines()
{
	VkPipelineLayoutCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.flags = 0;
	createInfo.setLayoutCount = 1;
	createInfo.pSetLayouts = &drawImageDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(device, &createInfo, nullptr, &gradientPipelineLayout));

	VkShaderModule shaderModule;
	VK_CHECK(vkutil::load_shader_module("../../shaders/gradient.comp.spv", device, &shaderModule));

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.flags = 0;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = shaderModule;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineCreateInfo{};
	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.pNext = nullptr;
	pipelineCreateInfo.flags = 0;
	pipelineCreateInfo.stage = stageInfo;
	pipelineCreateInfo.layout = gradientPipelineLayout;
	pipelineCreateInfo.basePipelineHandle = nullptr;
	pipelineCreateInfo.basePipelineIndex = 0;


	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &gradientPipeline));

	vkDestroyShaderModule(device, shaderModule, nullptr);

	mainDeletionQueue.push_function([=]()
	{
		vkDestroyPipelineLayout(device, gradientPipelineLayout, nullptr);
		vkDestroyPipeline(device, gradientPipeline, nullptr);
	});
}

void VulkanEngine::init_imgui()
{
	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC , 1000 } ,
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT , 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = instance;
	init_info.PhysicalDevice = chosenGpu;
	init_info.Device = device;
	init_info.Queue = graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	//init_info.UseDynamicRendering = true;
	//init_info.ColorAttachmentFormat = swapchainImageFormat;

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);

	// execute a gpu command to upload imgui font textures
	immediate_submit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });

	// clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	// add the destroy the imgui created structures
	mainDeletionQueue.push_function([=]()
	{
		vkDestroyDescriptorPool(device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
	});
}

void VulkanEngine::draw_background(const VkCommandBuffer cmd) const
{
	//const float flash = abs(sin(static_cast<float>(frameNumber) / 120.f));
	//const VkClearColorValue clearValue = { { 0.0f , 0.0f , flash , 1.0f } };
	//
	//const auto clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
	//
	//vkCmdClearColorImage(cmd, drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	vkCmdBindPipeline(
		cmd,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		gradientPipeline);

	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		gradientPipelineLayout,
		0,
		1,
		&drawImageDescriptors,
		0,
		nullptr);

	vkCmdDispatch(
		cmd,
		ceil_divide<uint32_t>(drawImage.imageExtent.width, 16),
		ceil_divide<uint32_t>(drawImage.imageExtent.height, 16),
		1);
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
