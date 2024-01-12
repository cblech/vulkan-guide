

#include <vulkan/vulkan.h>

namespace vkutil
{
	VkResult load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
}
