#include "vk_pipelines.h"

#include <iostream>
#include <fstream>
#include <vector>


VkResult vkutil::load_shader_module(const char* filePath,
                                    VkDevice device,
                                    VkShaderModule* outShaderModule)
{
	// open the file. With cursor at the end
	std::ifstream file(filePath, std::_Iosb<int>::ate | std::_Iosb<int>::binary);

	if (!file.is_open())
	{
		return VK_ERROR_UNKNOWN;
	}

	// find what the size of the file is by looking up the location of the cursor
	// because the cursor is at the end, it gives the size directly in bytes
	const auto fileSize = static_cast<std::streamsize>(file.tellg());

	// spirv expects the buffer to be on uint32, so make sure to reserve an int
	// vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	// put file cursor at beginning
	file.seekg(0);

	// load the entire file into the buffer
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

	// now that the file is loaded into the buffer, we can close it
	file.close();

	// create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	// codeSize has to be in bytes, so multiply the ints in the buffer by size of
	// int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	// check that the creation goes well.
	VkShaderModule shaderModule;
	const auto result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
	
	*outShaderModule = shaderModule;
	return result;
}
